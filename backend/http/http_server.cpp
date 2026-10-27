#include "http_server.h"
#include "modules/camera/CameraManager.h"
#include "httplib.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <sstream>
#include <iostream>
#include <optional>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace http_server {

static const char* SERVER_VERSION = "1.0.0";

// ── helpers ──────────────────────────────────────────────────────────────

static json parseBody(const httplib::Request& req, httplib::Response& res) {
    try {
        return json::parse(req.body);
    } catch (const std::exception& e) {
        res.status = 400;
        res.set_content(R"({"error":")" + std::string(e.what()) + R"("})", "application/json");
        return nullptr;
    }
}

static void setCors(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.set_header("Access-Control-Expose-Headers", "X-Width, X-Height, X-Fps");
}

static bool isNull(const json& j) { return j.is_null(); }

static std::string base64Decode(const std::string& input) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(input.size() * 3 / 4);
    int val = 0;
    int bits = -8;
    for (const unsigned char c : input) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        const auto pos = chars.find(c);
        if (pos == std::string::npos) return {};
        val = (val << 6) + static_cast<int>(pos);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// Resolve a stored image path against every plausible location.
// Crops may have been written by processes with a different CWD or config,
// so we check: the path as-is, absolute, the configured crops dir,
// config-adjacent dirs, and the server launch dir.
static std::filesystem::path resolveImagePath(const std::string& imagePath,
                                              const std::string& cropsDir,
                                              const std::string& configPath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path given(imagePath);
    const fs::path filename = given.filename();

    std::vector<fs::path> candidates;
    if (given.is_absolute()) {
        candidates.push_back(given);
    } else {
        candidates.push_back(given);
        candidates.push_back(fs::absolute(given, ec));
        const fs::path configDir = fs::absolute(fs::path(configPath), ec).parent_path();
        candidates.push_back(configDir / given);
    }
    if (!filename.empty()) {
        candidates.push_back(fs::path(cropsDir) / filename);
        const fs::path configDir = fs::absolute(fs::path(configPath), ec).parent_path();
        candidates.push_back(configDir / "crops" / filename);
        candidates.push_back(configDir / "data" / "crops" / filename);
        candidates.push_back(configDir.parent_path() / "data" / "crops" / filename);
        candidates.push_back(fs::current_path(ec) / "crops" / filename);
        candidates.push_back(fs::current_path(ec) / "data" / "crops" / filename);
    }
    // Absolute path written by another host/container (e.g. "/app/data/crops/x.jpg"
    // vs local "<project>/backend/crops/x.jpg"): re-anchor after the "crops/" segment.
    if (given.is_absolute() && !filename.empty()) {
        const std::string s = imagePath;
        const auto pos = s.rfind("crops/");
        if (pos != std::string::npos) {
            candidates.push_back(fs::path(cropsDir) / s.substr(pos + 6));
            candidates.push_back(fs::current_path(ec) / "crops" / s.substr(pos + 6));
            candidates.push_back(fs::current_path(ec) / "data" / "crops" / s.substr(pos + 6));
        }
    }
    for (const auto& c : candidates) {
        if (!c.empty() && fs::exists(c, ec) && fs::is_regular_file(c, ec)) {
            return c;
        }
    }
    return {};
}

static bool serveImageFile(const std::filesystem::path& path, httplib::Response& res) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        res.status = 500;
        res.set_content(R"({"error":"Failed to read image"})", "application/json");
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto extension = path.extension().string();
    const std::string contentType = extension == ".png" ? "image/png" : "image/jpeg";
    res.set_header("Cache-Control", "private, max-age=60");
    res.set_content(std::move(bytes), contentType);
    return true;
}


static std::string normalizeCameraUid(const std::string& raw) {
    if (raw.empty()) return raw;
    // If it looks like a device path segment (e.g. "dev/video0"),
    // restore the leading slash
    if (raw.rfind("dev/", 0) == 0) {
        return "/" + raw;
    }
    return raw;
}

// ── ctor / dtor ──────────────────────────────────────────────────────────

HttpServer::HttpServer(const Config& cfg) : config_(cfg) {}

HttpServer::~HttpServer() { stop(); }

// ── start / stop ─────────────────────────────────────────────────────────

bool HttpServer::start() {
    if (running_.load()) return false;

    try {
        faceConfig_ = std::make_shared<face::FaceConfig>(config_.configPath);
        db_ = std::make_unique<face::FaceDatabase>(config_.dbPath);
        workSessionTracker_ = std::make_unique<face::WorkSessionTracker>(*db_);
        face::AttendanceConfig attCfg;
        attendanceManager_ = std::make_unique<face::AttendanceManager>(*db_, *workSessionTracker_, attCfg);
        previewHub_ = std::make_unique<backend::CameraPreviewHub>();
        orchestrator_ = std::make_unique<backend::CameraOrchestrator>(
            faceConfig_, db_.get(), previewHub_.get(), attendanceManager_.get());
        orchestrator_->start();
    } catch (const std::exception& e) {
        std::cerr << "[HttpServer] init failed: " << e.what() << "\n";
        return false;
    }

    running_.store(true);
    serverThread_ = std::make_unique<std::thread>(&HttpServer::runServer, this);
    return true;
}

void HttpServer::stop() {
    if (!running_.load()) return;
    running_.store(false);

    if (svr_) svr_->stop();

    if (serverThread_ && serverThread_->joinable()) {
        serverThread_->join();
    }
    serverThread_.reset();
    svr_ = nullptr;

    if (orchestrator_) {
        orchestrator_->stop();
        orchestrator_.reset();
    }
    previewHub_.reset();
    attendanceManager_.reset();
    workSessionTracker_.reset();
    db_.reset();
    faceConfig_.reset();
}

// ── route registration ───────────────────────────────────────────────────

void HttpServer::setupRoutes(httplib::Server& svr) {

    // ── CORS preflight ──
    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        res.status = 204;
    });

    // ── Health ──
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        json j = {{"status", "ok"}, {"version", SERVER_VERSION}};
        res.set_content(j.dump(), "application/json");
    });

    // ================================================================
    // Persons
    // ================================================================

    svr.Get("/api/persons", [this](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            auto persons = db_->getAllPersons();
            json arr = json::array();
            for (const auto& p : persons) {
                arr.push_back({{"id", p.id}, {"name", p.name}, {"code", p.code},
                               {"picturePath", p.picturePath}, {"createdAt", p.createdAt},
                               {"accessLevel", p.access_level}});
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get(R"(/api/persons/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            face::Person p;
            if (!db_->getPerson(id, p)) {
                res.status = 404;
                res.set_content(R"({"error":"Person not found"})", "application/json");
                return;
            }
            json j = {{"id", p.id}, {"name", p.name}, {"code", p.code},
                      {"picturePath", p.picturePath}, {"createdAt", p.createdAt},
                      {"accessLevel", p.access_level}};
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Post("/api/persons", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const std::string name = body.value("name", "");
            const std::string code = body.value("code", "");
            if (name.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"name is required"})", "application/json");
                return;
            }
            const int64_t id = db_->addPerson(name, code, true);
            res.status = 201;
            json j = {{"id", id}, {"name", name}, {"code", code}};
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Put(R"(/api/persons/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            const std::string name = body.value("name", "");
            const std::string code = body.value("code", "");
            db_->updatePerson(id, name, code);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Delete(R"(/api/persons/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            const bool ok = db_->deletePerson(id);
            if (!ok) {
                res.status = 404;
                res.set_content(R"({"error":"Person not found"})", "application/json");
                return;
            }
            if (orchestrator_) orchestrator_->rebuildIndex();
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Put(R"(/api/persons/(\d+)/access-level)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            const int level = body.value("accessLevel", 2);
            db_->updatePersonAccessLevel(id, level);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get(R"(/api/persons/(\d+)/picture)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            face::Person p;
            if (!db_->getPerson(id, p)) {
                res.status = 404;
                res.set_content(R"({"error":"Person not found"})", "application/json");
                return;
            }
            if (p.picturePath.empty()) {
                res.status = 404;
                res.set_content(R"({"error":"Person has no picture"})", "application/json");
                return;
            }
            const auto path = resolveImagePath(p.picturePath, faceConfig_->Database().cropsDir, config_.configPath);
            if (path.empty()) {
                res.status = 404;
                res.set_content(R"({"error":"Person picture file not found"})", "application/json");
                return;
            }
            serveImageFile(path, res);
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // Change/assign a person's profile photo. Body: {"image": "<base64>", "extension": "jpg"}
    svr.Put(R"(/api/persons/(\d+)/picture)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            face::Person p;
            if (!db_->getPerson(id, p)) {
                res.status = 404;
                res.set_content(R"({"error":"Person not found"})", "application/json");
                return;
            }
            const std::string imageB64 = body.value("image", "");
            if (imageB64.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"image (base64) is required"})", "application/json");
                return;
            }
            std::string extension = body.value("extension", "jpg");
            if (extension != "png") extension = "jpg";

            const std::string bytes = base64Decode(imageB64);
            if (bytes.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid base64 image"})", "application/json");
                return;
            }

            const auto profilesDir = std::filesystem::path(faceConfig_->Database().cropsDir) / "profiles";
            std::error_code ec;
            std::filesystem::create_directories(profilesDir, ec);
            if (ec) {
                res.status = 500;
                res.set_content(R"({"error":"Failed to create profiles directory"})", "application/json");
                return;
            }
            const auto filePath = profilesDir / ("person_" + std::to_string(id) + "." + extension);

            // Remove the old profile photo if it was a custom upload with a different extension
            if (!p.picturePath.empty() && p.picturePath != filePath.string() &&
                p.picturePath.find("/profiles/") != std::string::npos) {
                const auto oldPath = resolveImagePath(p.picturePath, faceConfig_->Database().cropsDir, config_.configPath);
                if (!oldPath.empty()) std::filesystem::remove(oldPath, ec);
            }

            std::ofstream out(filePath, std::ios::binary);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            out.close();
            if (!out.good()) {
                res.status = 500;
                res.set_content(R"({"error":"Failed to save image"})", "application/json");
                return;
            }

            const auto storedPath = std::filesystem::absolute(filePath, ec).string();
            if (!db_->updatePersonPicturePath(id, storedPath)) {
                res.status = 500;
                res.set_content(R"({"error":"Failed to update picture path"})", "application/json");
                return;
            }
            json j = {{"ok", true}, {"picturePath", storedPath}};
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ================================================================
    // Cameras — register specific sub-routes BEFORE general /{uid}
    // ================================================================

    svr.Get("/api/cameras", [this](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            auto cams = db_->getAllCameras();
            json arr = json::array();
            for (const auto& c : cams) {
                arr.push_back({
                    {"id", c.id}, {"uid", c.uid}, {"name", c.name},
                    {"zoneId", c.zoneId},
                    {"type", c.type == face::CameraType::ENTRY ? "ENTRY" :
                             c.type == face::CameraType::EXIT  ? "EXIT" : "BOTH"},
                    {"streamEnabled", c.streamEnabled}, {"aiEnabled", c.aiEnabled},
                    {"connectionType", c.connectionType},
                    {"username", c.username}, {"password", c.password},
                    {"streamUri", c.streamUri}, {"encoding", c.encoding},
                    {"onvifXaddrs", c.onvifXaddrs}, {"transportProtocol", c.transportProtocol}
                });
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Post("/api/cameras", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            face::CameraConfig c;
            c.uid     = body.value("uid", std::string(""));
            c.name    = body.value("name", std::string(""));
            c.zoneId  = body.value("zoneId", 0LL);
            c.streamEnabled = body.value("streamEnabled", true);
            c.aiEnabled = body.value("aiEnabled", true);
            c.connectionType = body.value("connectionType", std::string("RTSP"));
            c.username = body.value("username", std::string(""));
            c.password = body.value("password", std::string(""));
            c.streamUri = body.value("streamUri", std::string(""));
            c.encoding = body.value("encoding", std::string(""));
            c.onvifXaddrs = body.value("onvifXaddrs", std::string(""));
            c.transportProtocol = body.value("transportProtocol", std::string("tcp"));
            const auto type = body.value("type", std::string("BOTH"));
            if      (type == "ENTRY") c.type = face::CameraType::ENTRY;
            else if (type == "EXIT")  c.type = face::CameraType::EXIT;
            else                      c.type = face::CameraType::BOTH;

            const int64_t id = db_->upsertCamera(c);
            res.status = 201;
            json j = {{"id", id}, {"uid", c.uid}};
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Post("/api/cameras/discover", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            bool deepSearch = false;
            if (!req.body.empty()) {
                const auto body = json::parse(req.body);
                deepSearch = body.value("deep", false);
            }
            camera::CameraManager mgr;
            auto devices = mgr.discoverAllCameras(deepSearch);
            json arr = json::array();
            for (const auto& d : devices) {
                json profiles = json::array();
                for (const auto& p : d.mediaProfiles) {
                    profiles.push_back({
                        {"token", p.token}, {"name", p.name},
                        {"width", p.width}, {"height", p.height},
                        {"fps", p.fps}, {"encoding", p.encoding},
                        {"streamUri", p.streamUri}
                    });
                }
                std::string typeStr;
                switch (d.type) {
                    case camera::ConnectionType::LOCAL: typeStr = "LOCAL"; break;
                    case camera::ConnectionType::RTSP:  typeStr = "RTSP";  break;
                    case camera::ConnectionType::ONVIF: typeStr = "ONVIF"; break;
                    default:                            typeStr = "UNKNOWN"; break;
                }
                arr.push_back({
                    {"devicePath", d.devicePath}, {"streamUri", d.streamUri},
                    {"cardName", d.cardName}, {"driver", d.driver},
                    {"type", typeStr}, {"index", d.index},
                    {"manufacturer", d.manufacturer}, {"model", d.model},
                    {"firmwareVersion", d.firmwareVersion},
                    {"serialNumber", d.serialNumber}, {"hardwareId", d.hardwareId},
                    {"onvifDeviceServiceUrl", d.onvifDeviceServiceUrl},
                    {"onvifMediaServiceUrl", d.onvifMediaServiceUrl},
                    {"onvifPort", d.onvifPort}, {"mediaProfiles", profiles},
                    {"requiresAuth", d.requiresAuth},
                    {"username", d.username}, {"password", d.password},
                    {"preferredWidth", d.preferredWidth},
                    {"preferredHeight", d.preferredHeight},
                    {"preferredFps", d.preferredFps}
                });
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Post("/api/cameras/test", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const auto body = json::parse(req.body);
            const std::string uri = body.value("streamUri", "");
            const std::string user = body.value("username", "");
            const std::string pass = body.value("password", "");
            if (uri.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "streamUri required"}}).dump(), "application/json");
                return;
            }
            auto r = camera::CameraDiscovery::testConnection(uri, user, pass);
            json out = {
                {"ok", r.ok},
                {"requiresAuth", r.requiresAuth},
                {"message", r.message},
            };
            res.set_content(out.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ── Specific sub-routes (must be before general /{uid}) ──

    svr.Put(R"(/api/cameras/(.+)/active)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const std::string uid = normalizeCameraUid(req.matches[1]);
            const bool active = body.value("active", false);
            db_->updateCameraStreamEnabled(uid, active);
            if (orchestrator_) orchestrator_->notifyCameraActiveChanged(uid, active);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Put(R"(/api/cameras/(.+)/configure)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const std::string uid = normalizeCameraUid(req.matches[1]);

            auto camConfig = db_->getCameraConfig(uid);
            face::CameraConfig cam;
            if (camConfig.has_value()) {
                cam = *camConfig;
            } else {
                cam.uid = uid;
            }

            bool hasCam = false;
            if (body.contains("name"))               { cam.name = body.value("name", std::string("")); hasCam = true; }
            if (body.contains("zoneId"))              { cam.zoneId = body.value("zoneId", 0LL); hasCam = true; }
            if (body.contains("streamEnabled"))       { cam.streamEnabled = body.value("streamEnabled", true); hasCam = true; }
            if (body.contains("aiEnabled"))           { cam.aiEnabled = body.value("aiEnabled", true); hasCam = true; }
            if (body.contains("connectionType"))      { cam.connectionType = body.value("connectionType", std::string("RTSP")); hasCam = true; }
            if (body.contains("username"))            { cam.username = body.value("username", std::string("")); hasCam = true; }
            if (body.contains("password"))            { cam.password = body.value("password", std::string("")); hasCam = true; }
            if (body.contains("streamUri"))           { cam.streamUri = body.value("streamUri", std::string("")); hasCam = true; }
            if (body.contains("encoding"))            { cam.encoding = body.value("encoding", std::string("")); hasCam = true; }
            if (body.contains("onvifXaddrs"))         { cam.onvifXaddrs = body.value("onvifXaddrs", std::string("")); hasCam = true; }
            if (body.contains("transportProtocol"))   { cam.transportProtocol = body.value("transportProtocol", std::string("tcp")); hasCam = true; }
            if (body.contains("type")) {
                const auto t = body.value("type", std::string("BOTH"));
                if      (t == "ENTRY") cam.type = face::CameraType::ENTRY;
                else if (t == "EXIT")  cam.type = face::CameraType::EXIT;
                else                   cam.type = face::CameraType::BOTH;
                hasCam = true;
            }
            if (hasCam) db_->upsertCamera(cam);

            if (body.contains("modules") && body["modules"].is_object()) {
                for (auto it = body["modules"].begin(); it != body["modules"].end(); ++it) {
                    const std::string moduleName = it.key();
                    bool enabled = false;
                    std::string cfg = "{}";
                    if (it.value().is_object()) {
                        if (it.value().contains("enabled")) enabled = it.value().value("enabled", false);
                        if (it.value().contains("config")) cfg = it.value()["config"].dump();
                    }
                    db_->setCameraModuleConfig(uid, moduleName, enabled, cfg);
                }
            }

            if (orchestrator_) orchestrator_->notifyCameraConfigChanged(uid);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get(R"(/api/cameras/(.+)/module/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const std::string uid = normalizeCameraUid(req.matches[1]);
            const std::string mod = req.matches[2];
            const auto cfg = db_->getCameraModuleConfig(uid, mod);
            if (!cfg.has_value()) {
                res.status = 404;
                res.set_content(R"({"error":"Module config not found"})", "application/json");
                return;
            }
            json payload = {{"enabled", cfg->first}, {"config_json", cfg->second}};
            try { payload["config"] = json::parse(cfg->second); } catch (...) { payload["config"] = cfg->second; }
            res.set_content(payload.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Put(R"(/api/cameras/(.+)/module/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const std::string uid = normalizeCameraUid(req.matches[1]);
            const std::string mod = req.matches[2];
            const bool enabled = body.value("enabled", false);
            std::string cfg = "{}";
            if (body.contains("config")) cfg = body["config"].dump();
            db_->setCameraModuleConfig(uid, mod, enabled, cfg);
            if (orchestrator_) orchestrator_->notifyCameraConfigChanged(uid);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ── General camera routes (AFTER specific sub-routes) ──

    svr.Get(R"(/api/cameras/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const std::string uid = normalizeCameraUid(req.matches[1]);
            auto cam = db_->getCameraConfig(uid);
            if (!cam.has_value()) {
                res.status = 404;
                res.set_content(R"({"error":"Camera not found"})", "application/json");
                return;
            }
            const auto& c = *cam;
            json j = {
                {"id", c.id}, {"uid", c.uid}, {"name", c.name},
                {"zoneId", c.zoneId},
                {"type", c.type == face::CameraType::ENTRY ? "ENTRY" :
                         c.type == face::CameraType::EXIT  ? "EXIT" : "BOTH"},
                {"streamEnabled", c.streamEnabled}, {"aiEnabled", c.aiEnabled},
                {"connectionType", c.connectionType},
                {"username", c.username}, {"password", c.password},
                {"streamUri", c.streamUri}, {"encoding", c.encoding},
                {"onvifXaddrs", c.onvifXaddrs}, {"transportProtocol", c.transportProtocol}
            };
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Delete(R"(/api/cameras/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const std::string uid = normalizeCameraUid(req.matches[1]);
            const bool ok = db_->deleteCamera(uid);
            if (!ok) {
                res.status = 404;
                res.set_content(R"({"error":"Camera not found"})", "application/json");
                return;
            }
            if (orchestrator_) orchestrator_->notifyCameraActiveChanged(uid, false);
            if (previewHub_) previewHub_->removeCamera(uid);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ================================================================
    // Zones
    // ================================================================

    svr.Get("/api/zones", [this](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            auto zones = db_->getAllZones();
            json arr = json::array();
            for (const auto& z : zones) {
                arr.push_back({{"id", z.id}, {"name", z.name}, {"description", z.description},
                               {"requiredAccessLevel", z.requiredAccessLevel}, {"createdAt", z.createdAt}});
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Post("/api/zones", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            face::ZoneInfo z;
            z.name = body.value("name", std::string(""));
            z.description = body.value("description", std::string(""));
            const int64_t id = db_->addZone(z);
            res.status = 201;
            json j = {{"id", id}, {"name", z.name}};
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get(R"(/api/zones/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            auto z = db_->getZone(id);
            if (!z.has_value()) {
                res.status = 404;
                res.set_content(R"({"error":"Zone not found"})", "application/json");
                return;
            }
            json j = {{"id", z->id}, {"name", z->name}, {"description", z->description},
                      {"requiredAccessLevel", z->requiredAccessLevel}, {"createdAt", z->createdAt}};
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Put(R"(/api/zones/(\d+)/access-level)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            const int accessLevel = body.value("requiredAccessLevel", body.value("accessLevel", 2));
            const bool ok = db_->updateZoneRequiredAccessLevel(id, accessLevel);
            if (!ok) {
                res.status = 404;
                res.set_content(R"({"error":"Zone not found"})", "application/json");
                return;
            }
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ================================================================
    // Attendance
    // ================================================================

    svr.Get("/api/attendance/summary", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const std::string personIdParam = req.get_param_value("personId");
            const std::string date = req.get_param_value("date");

            auto summaryToJson = [](const face::DailySummary& s) {
                json j = {
                    {"personId", s.personId}, {"date", s.date},
                    {"arrivalTime", s.arrivalTime}, {"departureTime", s.departureTime},
                    {"workSeconds", s.workSeconds}, {"breakSeconds", s.breakSeconds},
                    {"tardinessSeconds", s.tardinessSeconds}, {"isAbsent", s.isAbsent}
                };
                json intervals = json::array();
                for (const auto& [start, end] : s.breakIntervals) intervals.push_back({start, end});
                j["breakIntervals"] = intervals;
                return j;
            };

            if (personIdParam.empty()) {
                json arr = json::array();
                for (const auto& person : db_->getAllPersons()) {
                    arr.push_back(summaryToJson(db_->getDailySummary(person.id, date)));
                }
                res.set_content(arr.dump(), "application/json");
                return;
            }

            const int64_t personId = std::stoll(personIdParam);
            res.set_content(summaryToJson(db_->getDailySummary(personId, date)).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get("/api/attendance/timeline", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const int64_t personId = std::stoll(req.get_param_value("personId"));
            const std::string date = req.get_param_value("date");
            auto records = db_->getTimeline(personId, date);
            json arr = json::array();
            for (const auto& r : records) {
                arr.push_back({{"id", r.id}, {"sessionId", r.sessionId}, {"personId", r.personId},
                               {"breakStart", r.breakStart}, {"breakEnd", r.breakEnd},
                               {"breakSeconds", r.breakSeconds}});
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get("/api/attendance/tardiness", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const std::string date = req.get_param_value("date");
            auto records = db_->getTardinessList(date);
            json arr = json::array();
            for (const auto& r : records) {
                arr.push_back({{"personId", r.personId}, {"date", r.date},
                               {"expectedArrival", r.expectedArrival}, {"actualArrival", r.actualArrival},
                               {"tardinessSeconds", r.tardinessSeconds}, {"isAbsent", r.isAbsent}});
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get(R"(/api/attendance/current-location/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const int64_t personId = std::stoll(req.matches[1]);
            auto loc = db_->getPersonCurrentLocation(personId);
            if (!loc.has_value()) {
                res.status = 404;
                res.set_content(R"({"error":"Location not found"})", "application/json");
                return;
            }
            json j = {{"personId", loc->personId}, {"zoneId", loc->zoneId},
                      {"cameraId", loc->cameraId}, {"timestamp", loc->timestamp},
                      {"zoneName", loc->zoneName}};
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ================================================================
    // Modules
    // ================================================================

    svr.Get("/api/modules", [this](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            json arr = json::array();
            for (const auto& [name, config] : faceConfig_->Modules()) {
                json defaultConfig = json::object();
                if (!config.rawJson.empty()) {
                    try { defaultConfig = json::parse(config.rawJson); } catch (...) {}
                }
                arr.push_back({{"name", name}, {"enabled", config.enabled},
                               {"mandatory", false}, {"default_config", defaultConfig}});
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ================================================================
    // Settings
    // ================================================================

    svr.Get(R"(/api/settings/([a-zA-Z0-9_-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const std::string key = req.matches[1];
            auto val = db_->getSetting(key);
            if (!val.has_value()) {
                res.status = 404;
                res.set_content(R"({"error":"Setting not found"})", "application/json");
                return;
            }
            json j = {{"key", key}, {"value", *val}};
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Put(R"(/api/settings/([a-zA-Z0-9_-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const std::string key = req.matches[1];
            const std::string value = body.value("value", "");
            const std::string description = body.value("description", "");
            db_->setSetting(key, value, description);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ================================================================
    // Schedules
    // ================================================================

    svr.Put("/api/schedules", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            face::PersonSchedule s;
            s.personId          = body.value("personId", 0LL);
            s.expectedArrival   = body.value("expectedArrival", std::string("08:00"));
            s.expectedDeparture = body.value("expectedDeparture", std::string("17:00"));
            s.graceMinutes      = body.value("graceMinutes", 15);
            s.workDays          = body.value("workDays", std::string("1,2,3,4,5"));
            db_->upsertPersonSchedule(s);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get(R"(/api/schedules/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const int64_t personId = std::stoll(req.matches[1]);
            auto s = db_->getPersonSchedule(personId);
            if (!s.has_value()) {
                res.status = 404;
                res.set_content(R"({"error":"Schedule not found"})", "application/json");
                return;
            }
            json j = {{"personId", s->personId}, {"expectedArrival", s->expectedArrival},
                      {"expectedDeparture", s->expectedDeparture},
                      {"graceMinutes", s->graceMinutes}, {"workDays", s->workDays}};
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ================================================================
    // Preview (JPEG frame polling)
    // ================================================================

    svr.Get(R"(/api/preview/(.+)/jpeg)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        try {
            const std::string uid = normalizeCameraUid(req.matches[1]);
            if (!previewHub_) {
                res.status = 503;
                res.set_content(R"({"error":"Preview hub not available"})", "application/json");
                return;
            }
            const auto frame = previewHub_->getLatestJpeg(uid);
            if (!frame.has_value() || frame->jpegBytes.empty()) {
                res.status = 404;
                res.set_content(R"({"error":"No frame available"})", "application/json");
                return;
            }
            res.set_header("Content-Type", "image/jpeg");
            res.set_header("X-Width", std::to_string(frame->width));
            res.set_header("X-Height", std::to_string(frame->height));
            res.set_header("X-Fps", std::to_string(frame->fps));
            res.set_content(reinterpret_cast<const char*>(frame->jpegBytes.data()),
                            frame->jpegBytes.size(), "image/jpeg");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get(R"(/api/preview/(.+)/subscribe)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        try {
            const std::string uid = normalizeCameraUid(req.matches[1]);
            if (previewHub_) previewHub_->subscribe(uid);
            if (orchestrator_) orchestrator_->notifyPreviewSubscriptionsChanged(uid);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get(R"(/api/preview/(.+)/unsubscribe)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        try {
            const std::string uid = normalizeCameraUid(req.matches[1]);
            if (previewHub_) previewHub_->unsubscribe(uid);
            if (orchestrator_) orchestrator_->notifyPreviewSubscriptionsChanged(uid);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ================================================================
    // Unknown Faces
    // ================================================================

    svr.Get("/api/unknown-faces", [this](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            auto faces = db_->getUnknownFaces();
            json arr = json::array();
            for (const auto& f : faces) {
                json item = {{"id", f.id}, {"trackId", f.trackId},
                             {"cameraUid", f.cameraUid}, {"timestamp", f.timestamp},
                             {"imagePath", f.imagePath}};
                if (!f.embedding.empty()) item["embedding"] = f.embedding;
                arr.push_back(std::move(item));
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Get(R"(/api/unknown-faces/(\d+)/image)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            auto imagePath = db_->getUnknownFaceImagePath(id);
            if (!imagePath.has_value()) {
                res.status = 404;
                res.set_content(R"({"error":"Unknown face image not found"})", "application/json");
                return;
            }

            const auto path = resolveImagePath(*imagePath, faceConfig_->Database().cropsDir, config_.configPath);
            if (path.empty()) {
                res.status = 404;
                res.set_content(R"({"error":"Unknown face image not found"})", "application/json");
                return;
            }
            serveImageFile(path, res);
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Delete(R"(/api/unknown-faces/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            const bool ok = db_->deleteUnknownFace(id);
            if (!ok) {
                res.status = 404;
                res.set_content(R"({"error":"Unknown face not found"})", "application/json");
                return;
            }
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Post(R"(/api/unknown-faces/(\d+)/enroll)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const int64_t id = std::stoll(req.matches[1]);
            const std::string name = body.value("name", "");
            const std::string code = body.value("code", "");
            const int accessLevel = body.value("accessLevel", 2);
            if (name.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"name is required"})", "application/json");
                return;
            }
            const int64_t newId = db_->enrollUnknownAsNewPerson(id, name, code, accessLevel);
            if (newId < 0) {
                res.status = 404;
                res.set_content(R"({"error":"Unknown face not found"})", "application/json");
                return;
            }
            if (orchestrator_) orchestrator_->rebuildIndex();
            res.status = 201;
            json j = {{"personId", newId}};
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    svr.Post(R"(/api/unknown-faces/(\d+)/merge)", [this](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        auto body = parseBody(req, res);
        if (isNull(body)) return;
        std::lock_guard lock(mutex_);
        try {
            const int64_t unknownId = std::stoll(req.matches[1]);
            const int64_t personId = body.value("personId", 0LL);
            const bool ok = db_->mergeUnknownWithPerson(unknownId, personId);
            if (!ok) {
                res.status = 404;
                res.set_content(R"({"error":"Merge failed"})", "application/json");
                return;
            }
            if (orchestrator_) orchestrator_->rebuildIndex();
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // ── 404 fallback ──
    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 404 && res.body.empty()) {
            setCors(res);
            res.set_content(R"({"error":"Not found"})", "application/json");
        }
    });
}

// ── server thread ────────────────────────────────────────────────────────

void HttpServer::runServer() {
    httplib::Server svr;
    svr.new_task_queue = [] {
        return new httplib::ThreadPool(4, 8);
    };
    svr_ = &svr;
    setupRoutes(svr);

    std::cout << "[HttpServer] listening on " << config_.host << ":" << config_.port << "\n";
    svr.listen(config_.host, config_.port);
    svr_ = nullptr;
    std::cout << "[HttpServer] stopped\n";
}

} // namespace http_server
