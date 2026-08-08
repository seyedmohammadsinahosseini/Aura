#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <memory>
#include <string>
#include <atomic>

namespace face {

struct HttpServerConfig {
    int port = 8080;
    std::string dbPath = "attendance.db";
    std::string configPath = "core/config/config.json";
    // Directory that the /api/files endpoint may serve from (face crop images).
    std::string cropsDir = "crops";
};

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    bool start(const HttpServerConfig& config = {});
    void stop();
    bool isRunning() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
};

} // namespace face

#endif // HTTP_SERVER_H
