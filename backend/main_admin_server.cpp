#include "http/http_server.h"
#include <iostream>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

static void handleSignal(int) {
    g_running = false;
}

int main(int argc, char** argv) {
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    http_server::HttpServer::Config cfg;
    if (argc > 1) cfg.configPath = argv[1];
    if (argc > 2) cfg.dbPath = argv[2];
    if (argc > 3) cfg.port = std::stoi(argv[3]);

    std::cout << "[AdminServer] Starting on port " << cfg.port << "\n";
    std::cout << "[AdminServer] Config: " << cfg.configPath << "\n";
    std::cout << "[AdminServer] DB: " << cfg.dbPath << "\n";

    http_server::HttpServer server(cfg);
    if (!server.start()) {
        std::cerr << "[AdminServer] Failed to start\n";
        return 1;
    }

    std::cout << "[AdminServer] Running. Press Ctrl+C to stop.\n";

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[AdminServer] Shutting down...\n";
    server.stop();
    std::cout << "[AdminServer] Done.\n";
    return 0;
}
