#include "core/config/Config.h"
#include "modules/attendance/AttendanceManager.h"
#include "modules/attendance/WorkSessionTracker.h"
#include "modules/camera/CameraDiscovery.h"
#include "modules/camera/CameraManager.h"
#include "services/camera/CameraOrchestrator.h"
#include "services/storage/FaceDatabase.h"
#include "services/stream/CameraPreviewHub.h"

#include "http/http_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <opencv2/opencv.hpp>
#include <sstream>
#include <thread>
#include <unordered_set>

static std::atomic<bool> g_running{true};

static void handleSignal(int) { g_running = false; }

static std::string timestamp() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
  localtime_r(&t, &tm);
  char buf[20];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

static void registerCameraInDb(face::FaceDatabase &db,
                               const camera::DeviceInfo &dev) {
  std::string uid = (dev.type == camera::ConnectionType::LOCAL) ? dev.devicePath
                                                                : dev.streamUri;
  if (uid.empty())
    uid = dev.devicePath;

  face::CameraConfig cam;
  cam.uid = uid;
  cam.name = dev.cardName;
  cam.streamEnabled = true;
  cam.aiEnabled = true;
  switch (dev.type) {
  case camera::ConnectionType::LOCAL:
    cam.connectionType = "LOCAL";
    break;
  case camera::ConnectionType::ONVIF:
    cam.connectionType = "ONVIF";
    break;
  default:
    cam.connectionType = "RTSP";
    break;
  }
  cam.streamUri = dev.streamUri;
  if (!dev.mediaProfiles.empty()) {
    cam.encoding = dev.mediaProfiles[0].encoding;
    cam.preferredWidth = dev.preferredWidth;
    cam.preferredHeight = dev.preferredHeight;
    cam.preferredFps = dev.preferredFps;
  }
  cam.onvifXaddrs = dev.xaddrs;

  std::string zoneName = (dev.type == camera::ConnectionType::LOCAL)
                             ? "Local Cameras"
                             : "Network Cameras";
  auto zones = db.getAllZones();
  int64_t zoneId = 0;
  for (const auto &z : zones) {
    if (z.name == zoneName) {
      zoneId = z.id;
      break;
    }
  }
  if (zoneId == 0) {
    face::ZoneInfo z;
    z.name = zoneName;
    z.description = "Auto-created zone";
    zoneId = db.addZone(z);
  }
  cam.zoneId = zoneId;

  db.upsertCamera(cam);
  std::cout << "    Registered: " << dev.cardName << " (" << uid << ")"
            << std::endl;
}

int main(int argc, char **argv) {
  signal(SIGINT, handleSignal);
  signal(SIGTERM, handleSignal);

  const std::string configPath = argc > 1 ? argv[1] : "core/config/config.json";
  const std::string dbPath = argc > 2 ? argv[2] : "attendance.db";

  try {
    std::cout << "[FaceAuthentication] Starting at " << timestamp()
              << std::endl;
    std::cout << "  Config: " << configPath << std::endl;
    std::cout << "  DB:     " << dbPath << std::endl;

    auto config = std::make_shared<face::FaceConfig>(configPath);
    if (!config->IsValid()) {
      std::cerr << "Invalid config" << std::endl;
      return 1;
    }
    std::cout << "  Config loaded." << std::endl;

    auto db = std::make_unique<face::FaceDatabase>(dbPath);
    std::cout << "  Database opened." << std::endl;

    auto workSessionTracker = std::make_unique<face::WorkSessionTracker>(*db);
    auto attendanceManager = std::make_unique<face::AttendanceManager>(
        *db, *workSessionTracker, config->Attendance());

    // Discovery + register in DB
    camera::CameraManager cameraManager;
    std::cout << "\n  Discovering cameras..." << std::endl;
    auto devices = cameraManager.discoverAllCameras(true);
    std::cout << "  Found " << devices.size() << " camera(s):" << std::endl;
    for (const auto &d : devices) {
      std::cout << "    " << d.cardName;
      switch (d.type) {
      case camera::ConnectionType::LOCAL:
        std::cout << " (LOCAL: " << d.devicePath << ")";
        break;
      case camera::ConnectionType::ONVIF:
        std::cout << " (ONVIF: " << d.streamUri << ")";
        break;
      case camera::ConnectionType::RTSP:
        std::cout << " (RTSP: " << d.streamUri << ")";
        break;
      }
      std::cout << std::endl;
      registerCameraInDb(*db, d);
    }
    cameraManager.closeAll();

    // Preview hub + orchestrator
    auto previewHub = std::make_unique<backend::CameraPreviewHub>();
    auto orchestrator = std::make_unique<backend::CameraOrchestrator>(
        config, db.get(), previewHub.get(), attendanceManager.get());

    // Subscribe to all cameras so orchestrator publishes frames
    auto allCameras = db->getAllCameras();
    for (const auto &c : allCameras) {
      if (c.streamEnabled) {
        previewHub->subscribe(c.uid);
      }
    }

    std::cout << "\n  Starting orchestrator..." << std::endl;
    orchestrator->start();
    std::cout << "  Orchestrator running. Press Ctrl+C to stop.\n" << std::endl;

    // Start HTTP admin API server
    face::HttpServer httpServer;
    face::HttpServerConfig httpCfg;
    httpCfg.port = 8080;
    httpCfg.dbPath = dbPath;
    httpCfg.configPath = configPath;
    httpCfg.cropsDir = config->Database().cropsDir;
    httpServer.start(httpCfg);

    // Display loop (skipped in headless/server mode)
    bool headless = (getenv("DISPLAY") == nullptr);
    if (headless) {
      std::cout << "  Headless mode — API server ready." << std::endl;
    } else {
      std::cout << "  Display mode — showing camera feeds." << std::endl;
    }

    while (g_running) {
      if (headless) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      } else {
        bool hadFrame = false;
        for (const auto &c : allCameras) {
          if (!c.streamEnabled || !g_running)
            continue;

          auto bgr = previewHub->getLatestBgr(c.uid);
          if (!bgr.has_value() || bgr->bgrBytes.empty())
            continue;

          cv::Mat frame(bgr->height, bgr->width, CV_8UC3,
                        const_cast<uint8_t *>(bgr->bgrBytes.data()), bgr->stride);
          cv::Mat display = frame.clone();

          std::ostringstream text;
          text << c.name << "  " << std::fixed << std::setprecision(1) << bgr->fps
               << " FPS";
          cv::putText(display, text.str(), cv::Point(10, 28),
                      cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(40, 255, 255), 2);

          cv::imshow(c.name, display);
          hadFrame = true;
        }

        int key = cv::waitKey(hadFrame ? 10 : 100);
        if (key == 27 || key == 'q')
          break;
      }
    }

    std::cout << "\n  Shutting down..." << std::endl;
    orchestrator->stop();
    attendanceManager.reset();
    workSessionTracker.reset();
    previewHub.reset();
    db.reset();
    config.reset();
    if (!headless) cv::destroyAllWindows();
    std::cout << "  Done." << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
