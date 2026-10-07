#include "core/config/Config.h"
#include "ffi/face_ffi.h"
#include "http/http_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#ifndef FACEAUTH_VERSION_STRING
#define FACEAUTH_VERSION_STRING "dev"
#endif

static std::atomic<bool> g_running{true};

static void handleSignal(int) {
    g_running = false;
}

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_r(&t, &tm);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: faceauth-admin [config.json] [attendance.db]\n"
                      << "\n"
                      << "Starts the HTTP admin API only; cameras are added through\n"
                      << "POST /api/cameras and start automatically.\n"
                      << "\n"
                      << "Options:\n"
                      << "  -v, --version   print version and exit\n"
                      << "  -h, --help      print this help and exit\n";
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            std::cout << "faceauth-admin " << FACEAUTH_VERSION_STRING << "\n";
            return 0;
        }
    }

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    const std::string configPath = argc > 1 ? argv[1] : "core/config/config.json";
    const std::string dbPath = argc > 2 ? argv[2] : "attendance.db";

    try {
        std::cout << "[faceauth-admin] " << FACEAUTH_VERSION_STRING << " starting at "
                  << timestamp() << std::endl;
        std::cout << "  Config: " << configPath << std::endl;
        std::cout << "  DB:     " << dbPath << std::endl;

        // Init FFI layer — creates g_previewHub + g_orchestrator
        // The orchestrator will pick up cameras as they are added via the API.
        if (face_init(configPath.c_str(), dbPath.c_str()) != 0) {
            std::cerr << "face_init failed: " << face_last_error() << std::endl;
            return 1;
        }
        std::cout << "  FFI initialized (preview hub + orchestrator ready)." << std::endl;

        // Start HTTP admin API
        face::HttpServer httpServer;
        face::HttpServerConfig httpCfg;
        httpCfg.port = 8080;
        httpCfg.dbPath = dbPath;
        httpCfg.configPath = configPath;
        httpCfg.cropsDir = face::FaceConfig(configPath).Database().cropsDir;
        httpServer.start(httpCfg);

        std::cout << "\n  Admin API ready on http://0.0.0.0:" << httpCfg.port << "/api"
                  << std::endl;
        std::cout << "  Use the UI to add cameras — they will start automatically." << std::endl;
        std::cout << "  Press Ctrl+C to stop.\n" << std::endl;

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "\n  Shutting down..." << std::endl;
        httpServer.stop();
        face_cleanup();
        std::cout << "  Done." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
