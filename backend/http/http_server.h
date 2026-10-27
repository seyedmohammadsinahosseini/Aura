#pragma once

#include "services/storage/FaceDatabase.h"
#include "services/camera/CameraOrchestrator.h"
#include "services/stream/CameraPreviewHub.h"
#include "modules/attendance/AttendanceManager.h"
#include "modules/attendance/WorkSessionTracker.h"
#include "core/config/Config.h"

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

namespace httplib { class Server; }

namespace http_server {

class HttpServer {
public:
    struct Config {
        std::string host = "0.0.0.0";
        int port = 8080;
        std::string configPath = "core/config/config.json";
        std::string dbPath = "attendance.db";
    };

    explicit HttpServer(const Config& cfg);
    ~HttpServer();

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    Config config_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> serverThread_;

    // Backend services (owned by this server)
    std::shared_ptr<face::FaceConfig> faceConfig_;
    std::unique_ptr<face::FaceDatabase> db_;
    std::unique_ptr<face::WorkSessionTracker> workSessionTracker_;
    std::unique_ptr<face::AttendanceManager> attendanceManager_;
    std::unique_ptr<backend::CameraPreviewHub> previewHub_;
    std::unique_ptr<backend::CameraOrchestrator> orchestrator_;
    std::mutex mutex_;

    void setupRoutes(httplib::Server& svr);
    void runServer();

    httplib::Server* svr_ = nullptr;  // owned, set in runServer
};

} // namespace http_server
