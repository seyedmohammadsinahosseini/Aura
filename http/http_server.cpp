#include "http_server.h"
#include "httplib.h"
#include "ffi/face_ffi.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <cstring>
#include <sstream>
#include <filesystem>
#include <system_error>

// Helper: try DB with and without leading / (httplib normalises // away)
static std::string resolve_camera_uid(FaceDatabaseHandle db, std::string uid) {
    auto* d = face_db_get_camera(db, uid.c_str());
    if (d) {
        face_free_string(d);
        return uid;
    }
    if (!uid.empty() && uid[0] != '/') {
        std::string alt = "/" + uid;
        d = face_db_get_camera(db, alt.c_str());
        if (d) {
            face_free_string(d);
            return alt;
        }
    }
    return uid;
}

static std::string url_decode(const std::string& src) {
    std::string out;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()) {
            int hi = src[i + 1];
            int lo = src[i + 2];
            auto hex = [](int c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return 0;
            };
            out += static_cast<char>((hex(hi) << 4) | hex(lo));
            i += 2;
        } else if (src[i] == '+') {
            out += ' ';
        } else {
            out += src[i];
        }
    }
    return out;
}

using json = nlohmann::json;

namespace face {

class HttpServer::Impl {
public:
    httplib::Server server;
    std::thread worker;
    HttpServerConfig config;

    void setupRoutes() {
        // CORS headers on all responses
        server.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            return httplib::Server::HandlerResponse::Unhandled;
        });

        // ========== Health ==========
        server.Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"status":"ok"})", "application/json");
        });

        // ========== Init / Cleanup ==========
        server.Post("/api/init", [this](const httplib::Request& req, httplib::Response& res) {
            auto j = json::parse(req.body.empty() ? "{}" : req.body);
            auto cfg = j.value("config", config.configPath);
            auto dbPath = j.value("db", config.dbPath);
            int result = face_init(cfg.c_str(), dbPath.c_str());
            json r = {{"success", result == 0}};
            if (result != 0)
                r["error"] = face_last_error();
            res.set_content(r.dump(), "application/json");
        });

        server.Post("/api/cleanup", [](const httplib::Request&, httplib::Response& res) {
            face_cleanup();
            res.set_content(R"({"success":true})", "application/json");
        });

        // ========== Persons ==========
        server.Get("/api/persons", [this](const httplib::Request&, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                res.set_content(json({{"error", face_last_error()}}).dump(), "application/json");
                return;
            }
            auto* data = face_db_get_all_persons(db);
            std::string body = data ? data : "[]";
            face_free_string(data);
            face_db_close(db);
            res.set_content(body, "application/json");
        });

        server.Get("/api/persons/(\\d+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* data = face_db_get_person(db, id);
                       if (!data) {
                           res.status = 404;
                           face_db_close(db);
                           return;
                       }
                       std::string body = data;
                       face_free_string(data);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        server.Post("/api/persons", [this](const httplib::Request& req, httplib::Response& res) {
            auto j = json::parse(req.body);
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            auto id =
                face_db_add_person(db, j.value("name", "").c_str(), j.value("code", "").c_str());
            face_db_close(db);
            if (id < 0) {
                res.status = 400;
                res.set_content(json({{"error", "Failed to add person"}}).dump(),
                                "application/json");
                return;
            }
            res.status = 201;
            res.set_content(json({{"id", id}}).dump(), "application/json");
        });

        server.Put("/api/persons/(\\d+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       auto j = json::parse(req.body);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       int r = face_db_update_person(db, id, j.value("name", "").c_str(),
                                                     j.value("code", "").c_str());
                       face_db_close(db);
                       res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                   });

        server.Put("/api/persons/(\\d+)/access-level",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       auto j = json::parse(req.body.empty() ? "{}" : req.body);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       int r = face_db_set_person_access_level(db, id, j.value("accessLevel", 2));
                       face_db_close(db);
                       res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                   });

        server.Delete("/api/persons/(\\d+)",
                      [this](const httplib::Request& req, httplib::Response& res) {
                          int64_t id = std::stoll(req.matches[1]);
                          auto db = face_db_open(config.dbPath.c_str());
                          if (!db) {
                              res.status = 500;
                              return;
                          }
                          int r = face_db_delete_person(db, id);
                          face_db_close(db);
                          res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                      });

        // ========== Embeddings ==========
        // ========== Person schedule ==========
        server.Get("/api/persons/(\\d+)/schedule",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* data = face_db_get_person_schedule(db, id);
                       if (!data) {
                           res.status = 404;
                           face_db_close(db);
                           return;
                       }
                       std::string body = data;
                       face_free_string(data);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        server.Put("/api/persons/(\\d+)/schedule",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       int r = face_db_upsert_person_schedule(db, req.body.c_str());
                       face_db_close(db);
                       res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                   });

        // ========== Embeddings ==========
        server.Get("/api/embeddings/references",
                   [this](const httplib::Request&, httplib::Response& res) {
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* data = face_db_get_all_references(db);
                       std::string body = data ? data : "[]";
                       face_free_string(data);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        // ========== Person face image upload ==========
        server.Post("/api/persons/(\\d+)/face",
                    [this](const httplib::Request& req, httplib::Response& res) {
                        int64_t personId = std::stoll(req.matches[1]);
                        if (req.body.empty()) {
                            res.status = 400;
                            return;
                        }
                        auto pipeline = face_pipeline_create();
                        if (!pipeline) {
                            res.status = 500;
                            return;
                        }
                        auto* detectionsStr = face_process_frame(
                            pipeline, reinterpret_cast<const uint8_t*>(req.body.data()),
                            req.body.size());
                        if (!detectionsStr) {
                            face_pipeline_destroy(pipeline);
                            res.status = 400;
                            return;
                        }
                        auto db = face_db_open(config.dbPath.c_str());
                        if (!db) {
                            face_free_string(detectionsStr);
                            face_pipeline_destroy(pipeline);
                            res.status = 500;
                            return;
                        }
                        int result = -1;
                        try {
                            auto detections = json::parse(detectionsStr);
                            if (!detections.empty() && detections[0].contains("embedding")) {
                                auto emb = detections[0]["embedding"];
                                std::vector<float> embedding;
                                for (const auto& v : emb)
                                    embedding.push_back(v);
                                result = face_db_add_reference_embedding(
                                    db, personId, embedding.data(), embedding.size());
                            }
                        } catch (...) {
                        }
                        face_free_string(detectionsStr);
                        face_db_close(db);
                        face_pipeline_destroy(pipeline);
                        if (result != 0) {
                            res.status = 400;
                            return;
                        }
                        res.set_content(json({{"success", true}}).dump(), "application/json");
                    });

        // ========== File serving (face images) ==========
        // Serves face crop images used by the dashboard. Access is restricted to
        // the configured crops directory: requests are resolved against that
        // directory and rejected if they would escape it, preventing arbitrary
        // file reads and path-traversal attacks.
        server.Get("/api/files/(.+)", [this](const httplib::Request& req, httplib::Response& res) {
            std::string rawPath = req.matches[1];
            std::error_code ec;

            std::filesystem::path base = std::filesystem::weakly_canonical(config.cropsDir, ec);
            if (ec)
                base = std::filesystem::absolute(config.cropsDir).lexically_normal();

            std::filesystem::path candidate = (base / rawPath).lexically_normal();
            std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, ec);
            if (ec)
                resolved = candidate;

            const std::string baseStr = base.string();
            const std::string candStr = resolved.string();
            if (candStr != baseStr && candStr.compare(0, baseStr.size() + 1, baseStr + "/") != 0) {
                res.status = 403;
                return;
            }

            std::ifstream f(resolved, std::ios::binary | std::ios::ate);
            if (!f) {
                res.status = 404;
                return;
            }
            auto size = f.tellg();
            f.seekg(0);
            std::string body(size, '\0');
            f.read(body.data(), size);
            auto ext = resolved.extension().string();
            if (ext == ".jpg" || ext == ".jpeg")
                res.set_content(body, "image/jpeg");
            else if (ext == ".png")
                res.set_content(body, "image/png");
            else
                res.set_content(body, "application/octet-stream");
        });

        // ========== Dashboard activity ==========
        server.Get(
            "/api/dashboard/activity", [this](const httplib::Request&, httplib::Response& res) {
                auto db = face_db_open(config.dbPath.c_str());
                if (!db) {
                    res.status = 500;
                    return;
                }
                auto* personsData = face_db_get_all_persons(db);
                auto* unknownData = face_db_get_unknown_faces(db);
                auto* detectionsData = face_db_get_all_detections(db);
                std::string today = []() {
                    auto t = std::time(nullptr);
                    std::tm tm;
                    localtime_r(&t, &tm);
                    char buf[11];
                    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
                    return std::string(buf);
                }();

                json activities = json::array();

                // Add person detections as activity entries
                if (detectionsData) {
                    try {
                        auto arr = json::parse(detectionsData);
                        for (const auto& d : arr) {
                            json entry;
                            entry["type"] = d.contains("personId") && d["personId"] > 0
                                ? "detection"
                                : "unknown";
                            entry["personId"] = d.value("personId", 0LL);
                            entry["timestamp"] = d.value("timestamp", 0LL);
                            entry["cameraUid"] = d.value("cameraUid", "");
                            entry["trackId"] = d.value("trackId", "");
                            entry["id"] = d.value("id", 0LL);
                            entry["confidence"] = d.value("confidence", 0.0);
                            if (entry["personId"] > 0 && personsData) {
                                try {
                                    auto persons = json::parse(personsData);
                                    for (const auto& p : persons) {
                                        if (p.value("id", 0LL) == entry["personId"]) {
                                            entry["personName"] = p.value("name", "");
                                            break;
                                        }
                                    }
                                } catch (...) {
                                }
                            }
                            activities.push_back(entry);
                        }
                    } catch (...) {
                    }
                    face_free_string(detectionsData);
                }

                // Add unknown faces as alert entries
                if (unknownData) {
                    try {
                        auto arr = json::parse(unknownData);
                        for (const auto& u : arr) {
                            json entry;
                            entry["type"] = "alert";
                            entry["id"] = u.value("id", 0LL);
                            entry["timestamp"] = u.value("timestamp", 0LL);
                            entry["cameraUid"] = u.value("cameraUid", "");
                            entry["trackId"] = u.value("trackId", "");
                            entry["imagePath"] = u.value("imagePath", "");
                            activities.push_back(entry);
                        }
                    } catch (...) {
                    }
                    face_free_string(unknownData);
                }

                if (personsData)
                    face_free_string(personsData);

                // Sort by timestamp descending, limit to 50
                std::sort(activities.begin(), activities.end(), [](const json& a, const json& b) {
                    return a.value("timestamp", 0LL) > b.value("timestamp", 0LL);
                });
                if (activities.size() > 50)
                    activities.erase(activities.begin() + 50, activities.end());

                face_db_close(db);
                res.set_content(activities.dump(), "application/json");
            });

        // ========== Unknown Faces ==========
        server.Get("/api/unknown-faces", [this](const httplib::Request&, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            auto* data = face_db_get_unknown_faces(db);
            std::string body = data ? data : "[]";
            face_free_string(data);
            face_db_close(db);
            res.set_content(body, "application/json");
        });

        server.Delete("/api/unknown-faces/(\\d+)",
                      [this](const httplib::Request& req, httplib::Response& res) {
                          int64_t id = std::stoll(req.matches[1]);
                          auto db = face_db_open(config.dbPath.c_str());
                          if (!db) {
                              res.status = 500;
                              return;
                          }
                          int r = face_db_delete_unknown_face(db, id);
                          face_db_close(db);
                          res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                      });

        server.Post("/api/unknown-faces/(\\d+)/enroll",
                    [this](const httplib::Request& req, httplib::Response& res) {
                        int64_t unknownId = std::stoll(req.matches[1]);
                        auto j = json::parse(req.body);
                        auto db = face_db_open(config.dbPath.c_str());
                        if (!db) {
                            res.status = 500;
                            return;
                        }
                        auto newId = face_db_enroll_unknown_as_new(
                            db, unknownId, j.value("name", "").c_str(), j.value("code", "").c_str(),
                            j.value("accessLevel", 2));
                        face_db_close(db);
                        if (newId < 0) {
                            res.status = 400;
                            return;
                        }
                        res.status = 201;
                        res.set_content(json({{"id", newId}}).dump(), "application/json");
                    });

        server.Post("/api/unknown-faces/(\\d+)/merge",
                    [this](const httplib::Request& req, httplib::Response& res) {
                        int64_t unknownId = std::stoll(req.matches[1]);
                        auto j = json::parse(req.body);
                        auto db = face_db_open(config.dbPath.c_str());
                        if (!db) {
                            res.status = 500;
                            return;
                        }
                        int r = face_db_merge_unknown_with_person(db, unknownId,
                                                                  j.value("personId", 0LL));
                        face_db_close(db);
                        res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                    });

        // ========== Cameras ==========
        server.Get("/api/cameras", [this](const httplib::Request&, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            auto* data = face_db_get_all_cameras(db);
            std::string body = data ? data : "[]";
            face_free_string(data);
            face_db_close(db);
            res.set_content(body, "application/json");
        });

        server.Get("/api/cameras/(\\d+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* data = face_db_get_camera_by_id(db, id);
                       if (!data) {
                           res.status = 404;
                           face_db_close(db);
                           return;
                       }
                       std::string body = data;
                       face_free_string(data);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        server.Post("/api/cameras", [this](const httplib::Request& req, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            auto j = json::parse(req.body.empty() ? "{}" : req.body);
            auto id = face_db_upsert_camera(db, req.body.c_str());
            if (id < 0) {
                face_db_close(db);
                res.status = 400;
                return;
            }
            // Auto-subscribe if streaming is enabled so the orchestrator starts a worker
            if (j.value("streamEnabled", false) || j.value("aiEnabled", false)) {
                std::string uid = j.value("uid", "");
                if (!uid.empty())
                    face_preview_subscribe(uid.c_str());
            }
            face_db_close(db);
            res.status = 201;
            res.set_content(json({{"id", id}}).dump(), "application/json");
        });

        server.Put("/api/cameras/(\\d+)/active",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       auto j = json::parse(req.body);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* camData = face_db_get_camera_by_id(db, id);
                       if (!camData) {
                           face_db_close(db);
                           res.status = 404;
                           return;
                       }
                       std::string uid = json::parse(camData).value("uid", "");
                       face_free_string(camData);
                       int active = j.value("active", 0);
                       int r = face_db_set_camera_active(db, uid.c_str(), active);
                       face_db_close(db);
                       if (r != 0) {
                           res.status = 400;
                           return;
                       }
                       if (active)
                           face_preview_subscribe(uid.c_str());
                       else
                           face_preview_unsubscribe(uid.c_str());
                       res.set_content(json({{"success", true}}).dump(), "application/json");
                   });

        server.Delete("/api/cameras/(\\d+)",
                      [this](const httplib::Request& req, httplib::Response& res) {
                          int64_t id = std::stoll(req.matches[1]);
                          auto db = face_db_open(config.dbPath.c_str());
                          if (!db) {
                              res.status = 500;
                              return;
                          }
                          auto* camData = face_db_get_camera_by_id(db, id);
                          if (!camData) {
                              face_db_close(db);
                              res.status = 404;
                              return;
                          }
                          std::string uid = json::parse(camData).value("uid", "");
                          face_free_string(camData);
                          int r = face_db_delete_camera(db, uid.c_str());
                          face_db_close(db);
                          face_preview_unsubscribe(uid.c_str());
                          res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                      });

        server.Put(
            "/api/cameras/(\\d+)/ai", [this](const httplib::Request& req, httplib::Response& res) {
                int64_t id = std::stoll(req.matches[1]);
                auto j = json::parse(req.body);
                auto db = face_db_open(config.dbPath.c_str());
                if (!db) {
                    res.status = 500;
                    return;
                }
                auto* camData = face_db_get_camera_by_id(db, id);
                if (!camData) {
                    face_db_close(db);
                    res.status = 404;
                    return;
                }
                std::string uid = json::parse(camData).value("uid", "");
                face_free_string(camData);
                int r = face_db_set_camera_ai_enabled(db, uid.c_str(), j.value("enabled", 0));
                face_db_close(db);
                res.set_content(json({{"success", r == 0}}).dump(), "application/json");
            });

        server.Post("/api/cameras/discover",
                    [](const httplib::Request& req, httplib::Response& res) {
                        auto j = json::parse(req.body.empty() ? "{}" : req.body);
                        int deep = j.value("deep", 0);
                        auto mgr = face_camera_manager_create();
                        if (!mgr) {
                            res.status = 500;
                            return;
                        }
                        auto* data = face_camera_discover(mgr, deep);
                        face_camera_manager_destroy(mgr);
                        std::string body = data ? data : "[]";
                        face_free_string(data);
                        res.set_content(body, "application/json");
                    });

        // ========== Camera UID-based routes ==========
        // IMPORTANT: more specific routes (with sub-paths like /stream) must be
        // registered BEFORE the generic by-uid/(.+) catch-all.
        server.Get("/api/cameras/by-uid/(.+)/stream",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       std::string uid = url_decode(req.matches[1]);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (db) {
                           uid = resolve_camera_uid(db, uid);
                           face_db_close(db);
                       }
                       face_preview_subscribe(uid.c_str());
                       std::cout << "[MJPEG] Starting stream for by-uid " << uid << std::endl;
                       res.set_content_provider(
                           "multipart/x-mixed-replace; boundary=frame",
                           [uid](size_t, httplib::DataSink& sink) {
                               size_t jpegLen = 0;
                               int w = 0, h = 0;
                               float fps = 0;
                               auto* jpeg = face_preview_get_latest_frame_jpeg(
                                   uid.c_str(), &jpegLen, &w, &h, &fps);
                               if (jpeg && jpegLen > 0) {
                                   std::string hdr = "--frame\r\nContent-Type: image/jpeg\r\n\r\n";
                                   sink.write(hdr.data(), hdr.size());
                                   sink.write(reinterpret_cast<char*>(jpeg), jpegLen);
                                   std::string ftr = "\r\n";
                                   sink.write(ftr.data(), ftr.size());
                                   face_free_bytes(jpeg);
                               }
                               std::this_thread::sleep_for(std::chrono::milliseconds(33));
                               return true;
                           },
                           [uid](bool) {
                               face_preview_unsubscribe(uid.c_str());
                               std::cout << "[MJPEG] Stream ended for by-uid " << uid << std::endl;
                           });
                   });

        server.Get("/api/cameras/by-uid/(.+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       std::string uid = url_decode(req.matches[1]);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       uid = resolve_camera_uid(db, uid);
                       auto* data = face_db_get_camera(db, uid.c_str());
                       if (!data) {
                           face_db_close(db);
                           res.status = 404;
                           return;
                       }
                       std::string body = data;
                       face_free_string(data);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        server.Delete("/api/cameras/by-uid/(.+)",
                      [this](const httplib::Request& req, httplib::Response& res) {
                          std::string uid = url_decode(req.matches[1]);
                          auto db = face_db_open(config.dbPath.c_str());
                          if (!db) {
                              res.status = 500;
                              return;
                          }
                          uid = resolve_camera_uid(db, uid);
                          int r = face_db_delete_camera(db, uid.c_str());
                          face_db_close(db);
                          if (r != 0) {
                              res.status = 400;
                              return;
                          }
                          face_preview_unsubscribe(uid.c_str());
                          res.set_content(json({{"success", true}}).dump(), "application/json");
                      });

        server.Put("/api/cameras/by-uid/(.+)/active",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       std::string uid = url_decode(req.matches[1]);
                       auto j = json::parse(req.body);
                       int active = j.value("active", 0);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       uid = resolve_camera_uid(db, uid);
                       int r = face_db_set_camera_active(db, uid.c_str(), active);
                       face_db_close(db);
                       if (r != 0) {
                           res.status = 400;
                           return;
                       }
                       if (active)
                           face_preview_subscribe(uid.c_str());
                       else
                           face_preview_unsubscribe(uid.c_str());
                       res.set_content(json({{"success", true}}).dump(), "application/json");
                   });

        server.Put("/api/cameras/by-uid/(.+)/ai",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       std::string uid = url_decode(req.matches[1]);
                       auto j = json::parse(req.body);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       uid = resolve_camera_uid(db, uid);
                       int r =
                           face_db_set_camera_ai_enabled(db, uid.c_str(), j.value("enabled", 0));
                       face_db_close(db);
                       res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                   });

        server.Get("/api/cameras/(\\d+)/stream",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* camData = face_db_get_camera_by_id(db, id);
                       if (!camData) {
                           face_db_close(db);
                           res.status = 404;
                           return;
                       }
                       std::string uid = json::parse(camData).value("uid", "");
                       face_free_string(camData);
                       face_db_close(db);
                       face_preview_subscribe(uid.c_str());
                       std::cout << "[MJPEG] Starting stream for " << uid << std::endl;
                       res.set_content_provider(
                           "multipart/x-mixed-replace; boundary=frame",
                           [uid](size_t, httplib::DataSink& sink) {
                               size_t jpegLen = 0;
                               int w = 0, h = 0;
                               float fps = 0;
                               auto* jpeg = face_preview_get_latest_frame_jpeg(
                                   uid.c_str(), &jpegLen, &w, &h, &fps);
                               if (jpeg && jpegLen > 0) {
                                   std::string hdr = "--frame\r\nContent-Type: image/jpeg\r\n\r\n";
                                   sink.write(hdr.data(), hdr.size());
                                   sink.write(reinterpret_cast<char*>(jpeg), jpegLen);
                                   std::string ftr = "\r\n";
                                   sink.write(ftr.data(), ftr.size());
                                   face_free_bytes(jpeg);
                               }
                               std::this_thread::sleep_for(std::chrono::milliseconds(33));
                               return true;
                           },
                           [uid](bool) {
                               face_preview_unsubscribe(uid.c_str());
                               std::cout << "[MJPEG] Stream ended for " << uid << std::endl;
                           });
                   });

        // ========== Camera module config ==========
        server.Get(
            "/api/cameras/(\\d+)/modules",
            [this](const httplib::Request& req, httplib::Response& res) {
                int64_t id = std::stoll(req.matches[1]);
                auto db = face_db_open(config.dbPath.c_str());
                if (!db) {
                    res.status = 500;
                    return;
                }
                auto* camData = face_db_get_camera_by_id(db, id);
                if (!camData) {
                    face_db_close(db);
                    res.status = 404;
                    return;
                }
                std::string uid = json::parse(camData).value("uid", "");
                face_free_string(camData);
                auto* modulesData = face_db_get_available_modules(db);
                if (!modulesData) {
                    face_db_close(db);
                    res.status = 500;
                    return;
                }
                try {
                    auto modules = json::parse(modulesData);
                    for (auto& m : modules) {
                        std::string name = m["name"];
                        auto* cfg = face_db_get_camera_module_config(db, uid.c_str(), name.c_str());
                        if (cfg) {
                            try {
                                auto cfgJson = json::parse(cfg);
                                m["enabled"] = cfgJson.value("enabled", m.value("enabled", false));
                                if (cfgJson.contains("config") && cfgJson["config"].is_object()) {
                                    // Merge per-camera config on top of default_config
                                    json merged = m.value("default_config", json::object());
                                    for (auto& [k, v] : cfgJson["config"].items())
                                        merged[k] = v;
                                    m["config"] = merged;
                                }
                            } catch (...) {
                            }
                            face_free_string(cfg);
                        } else {
                            m["config"] = m.value("default_config", json::object());
                        }
                    }
                    res.set_content(modules.dump(), "application/json");
                } catch (...) {
                    res.status = 500;
                }
                face_free_string(modulesData);
                face_db_close(db);
            });

        server.Get("/api/cameras/(\\d+)/modules/(.+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       std::string moduleName = req.matches[2];
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* camData = face_db_get_camera_by_id(db, id);
                       if (!camData) {
                           face_db_close(db);
                           res.status = 404;
                           return;
                       }
                       std::string uid = json::parse(camData).value("uid", "");
                       face_free_string(camData);
                       auto* cfg =
                           face_db_get_camera_module_config(db, uid.c_str(), moduleName.c_str());
                       if (!cfg) {
                           face_db_close(db);
                           res.status = 404;
                           return;
                       }
                       std::string body = cfg;
                       face_free_string(cfg);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        server.Put("/api/cameras/(\\d+)/modules/(.+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       std::string moduleName = req.matches[2];
                       auto j = json::parse(req.body.empty() ? "{}" : req.body);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* camData = face_db_get_camera_by_id(db, id);
                       if (!camData) {
                           face_db_close(db);
                           res.status = 404;
                           return;
                       }
                       std::string uid = json::parse(camData).value("uid", "");
                       face_free_string(camData);
                       std::string configJson = j.contains("config") ? j["config"].dump() : "{}";
                       int r = face_db_set_camera_module_config(db, uid.c_str(), moduleName.c_str(),
                                                                j.value("enabled", 1),
                                                                configJson.c_str());
                       face_db_close(db);
                       res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                   });

        // ========== Zones ==========
        server.Get("/api/zones", [this](const httplib::Request&, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            auto* data = face_db_get_all_zones(db);
            std::string body = data ? data : "[]";
            face_free_string(data);
            face_db_close(db);
            res.set_content(body, "application/json");
        });

        server.Get("/api/zones/(\\d+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* data = face_db_get_zone(db, id);
                       if (!data) {
                           res.status = 404;
                           face_db_close(db);
                           return;
                       }
                       std::string body = data;
                       face_free_string(data);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        server.Post("/api/zones", [this](const httplib::Request& req, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            auto id = face_db_add_zone(db, req.body.c_str());
            face_db_close(db);
            if (id < 0) {
                res.status = 400;
                return;
            }
            res.status = 201;
            res.set_content(json({{"id", id}}).dump(), "application/json");
        });

        server.Delete("/api/zones/(\\d+)",
                      [this](const httplib::Request& req, httplib::Response& res) {
                          int64_t id = std::stoll(req.matches[1]);
                          auto db = face_db_open(config.dbPath.c_str());
                          if (!db) {
                              res.status = 500;
                              return;
                          }
                          int r = face_db_delete_zone(db, id);
                          face_db_close(db);
                          res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                      });

        server.Put("/api/zones/(\\d+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       auto j = json::parse(req.body.empty() ? "{}" : req.body);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       int r = face_db_update_zone(db, id, j.value("name", "").c_str(),
                                                   j.value("description", "").c_str());
                       face_db_close(db);
                       res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                   });

        server.Put("/api/zones/(\\d+)/access-level",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t id = std::stoll(req.matches[1]);
                       auto j = json::parse(req.body.empty() ? "{}" : req.body);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       int r = face_db_set_zone_required_access_level(db, id,
                                                                      j.value("accessLevel", 2));
                       face_db_close(db);
                       res.set_content(json({{"success", r == 0}}).dump(), "application/json");
                   });

        // ========== Attendance ==========
        server.Post(
            "/api/attendance/arrival", [this](const httplib::Request& req, httplib::Response& res) {
                auto j = json::parse(req.body);
                auto db = face_db_open(config.dbPath.c_str());
                if (!db) {
                    res.status = 500;
                    return;
                }
                auto att = face_attendance_create(db, nullptr);
                std::string trackId = j.value("trackId", "");
                auto* result = face_attendance_mark_arrival(
                    att, trackId.c_str(), j.value("personId", 0LL), j.value("timestamp", 0LL));
                std::string body = result ? result : R"({"success":false})";
                face_free_string(result);
                face_attendance_destroy(att);
                face_db_close(db);
                res.set_content(body, "application/json");
            });

        server.Post("/api/attendance/departure",
                    [this](const httplib::Request& req, httplib::Response& res) {
                        auto j = json::parse(req.body);
                        auto db = face_db_open(config.dbPath.c_str());
                        if (!db) {
                            res.status = 500;
                            return;
                        }
                        auto att = face_attendance_create(db, nullptr);
                        std::string trackId = j.value("trackId", "");
                        auto* result = face_attendance_mark_departure(att, trackId.c_str(),
                                                                      j.value("personId", 0LL),
                                                                      j.value("timestamp", 0LL));
                        std::string body = result ? result : R"({"success":false})";
                        face_free_string(result);
                        face_attendance_destroy(att);
                        face_db_close(db);
                        res.set_content(body, "application/json");
                    });

        server.Get(
            "/api/attendance/summary", [this](const httplib::Request& req, httplib::Response& res) {
                auto db = face_db_open(config.dbPath.c_str());
                if (!db) {
                    res.status = 500;
                    return;
                }
                int64_t personId =
                    req.has_param("personId") ? std::stoll(req.get_param_value("personId")) : 0;
                std::string date = req.get_param_value("date");
                auto* data = face_db_get_daily_summary(db, personId, date.c_str());
                std::string body = data ? data : "{}";
                face_free_string(data);
                face_db_close(db);
                res.set_content(body, "application/json");
            });

        server.Get("/api/attendance/tardiness",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       std::string date = req.get_param_value("date");
                       auto* data = face_db_get_tardiness_list(db, date.c_str());
                       std::string body = data ? data : "[]";
                       face_free_string(data);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        server.Get("/api/attendance/timeline",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       int64_t personId = req.has_param("personId")
                           ? std::stoll(req.get_param_value("personId"))
                           : 0;
                       std::string date = req.get_param_value("date");
                       auto* data = face_db_get_timeline(db, personId, date.c_str());
                       std::string body = data ? data : "[]";
                       face_free_string(data);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        server.Get("/api/attendance/current-location/(\\d+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       int64_t personId = std::stoll(req.matches[1]);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* data = face_db_get_current_location(db, personId);
                       if (!data) {
                           res.status = 404;
                           face_db_close(db);
                           return;
                       }
                       std::string body = data;
                       face_free_string(data);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        // ========== Dashboard / Stats ==========
        server.Get("/api/dashboard/stats", [this](const httplib::Request&, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            auto* personsData = face_db_get_all_persons(db);
            auto* camerasData = face_db_get_all_cameras(db);
            auto* unknownFacesData = face_db_get_unknown_faces(db);
            auto* detectionsData = face_db_get_all_detections(db);
            std::string today = []() {
                auto t = std::time(nullptr);
                std::tm tm;
                localtime_r(&t, &tm);
                char buf[11];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
                return std::string(buf);
            }();

            int totalPersons = 0, activeDevices = 0, offlineDevices = 0;
            int presentToday = 0, absentToday = 0;
            int unknownDetections = 0, unknownFaces = 0;

            if (personsData) {
                try {
                    auto arr = json::parse(personsData);
                    totalPersons = arr.size();
                    for (const auto& p : arr) {
                        int64_t pid = p.value("id", 0LL);
                        if (pid > 0) {
                            auto* summary = face_db_get_daily_summary(db, pid, today.c_str());
                            if (summary) {
                                try {
                                    auto s = json::parse(summary);
                                    if (s.value("arrivalTime", 0LL) > 0 &&
                                        s.value("departureTime", 0LL) == 0)
                                        presentToday++;
                                } catch (...) {
                                }
                                face_free_string(summary);
                            }
                        }
                    }
                } catch (...) {
                }
                face_free_string(personsData);
            }

            absentToday = totalPersons - presentToday;
            if (absentToday < 0)
                absentToday = 0;

            if (camerasData) {
                try {
                    auto arr = json::parse(camerasData);
                    for (const auto& c : arr) {
                        if (c.value("streamEnabled", false))
                            activeDevices++;
                        else
                            offlineDevices++;
                    }
                } catch (...) {
                }
                face_free_string(camerasData);
            }

            if (unknownFacesData) {
                try {
                    auto arr = json::parse(unknownFacesData);
                    unknownFaces = arr.size();
                } catch (...) {
                }
                face_free_string(unknownFacesData);
            }

            if (detectionsData) {
                try {
                    auto arr = json::parse(detectionsData);
                    unknownDetections = arr.size();
                } catch (...) {
                }
                face_free_string(detectionsData);
            }

            face_db_close(db);
            json stats = {
                {"totalPersons", totalPersons},     {"presentToday", presentToday},
                {"absentToday", absentToday},       {"unknownDetections", unknownDetections},
                {"unknownFaces", unknownFaces},     {"activeDevices", activeDevices},
                {"offlineDevices", offlineDevices}, {"alertsToday", 0}};
            res.set_content(stats.dump(), "application/json");
        });

        // ========== Events (raw detections) ==========
        server.Get("/api/events", [this](const httplib::Request& req, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            int64_t limit = req.has_param("limit") ? std::stoll(req.get_param_value("limit")) : 100;
            (void)limit;
            auto* data = face_db_get_all_detections(db);
            face_db_close(db);
            if (!data) {
                res.set_content("[]", "application/json");
                return;
            }
            try {
                auto j = json::parse(data);
                if (j.is_array()) {
                    for (auto& item : j) {
                        if (item.contains("timestamp")) {
                            item["timestamp"] = item["timestamp"].get<int64_t>() * 1000;
                        }
                    }
                }
                res.set_content(j.dump(), "application/json");
            } catch (...) {
                res.set_content(data, "application/json");
            }
            face_free_string(data);
        });

        // ========== Settings ==========
        server.Get("/api/settings", [this](const httplib::Request&, httplib::Response& res) {
            // Return commonly-used settings as a flat key-value map.
            // The backend has no "list all keys" API, so we query known keys.
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            const char* knownKeys[] = {
                "groups",    "work_start_time", "work_end_time", "grace_minutes",
                "work_days", "language",        "theme",         nullptr};
            json result;
            for (int i = 0; knownKeys[i]; ++i) {
                auto* data = face_db_get_setting(db, knownKeys[i]);
                if (data) {
                    try {
                        auto j = json::parse(data);
                        result[knownKeys[i]] = j.value("value", "");
                    } catch (...) {
                        result[knownKeys[i]] = data;
                    }
                    face_free_string(data);
                }
            }
            face_db_close(db);
            res.set_content(result.dump(), "application/json");
        });

        server.Get("/api/settings-all", [this](const httplib::Request&, httplib::Response& res) {
            // Return all settings as an array of {key, value, description}
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            const char* knownKeys[] = {
                "groups",    "work_start_time", "work_end_time", "grace_minutes",
                "work_days", "language",        "theme",         nullptr};
            json arr = json::array();
            for (int i = 0; knownKeys[i]; ++i) {
                auto* data = face_db_get_setting(db, knownKeys[i]);
                std::string val = "";
                std::string desc = "";
                if (data) {
                    try {
                        auto j = json::parse(data);
                        val = j.value("value", "");
                        desc = j.value("description", "");
                    } catch (...) {
                        val = data;
                    }
                    face_free_string(data);
                }
                arr.push_back({{"key", knownKeys[i]}, {"value", val}, {"description", desc}});
            }
            face_db_close(db);
            res.set_content(arr.dump(), "application/json");
        });

        server.Get("/api/settings/(.+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       auto key = req.matches[1].str();
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       auto* data = face_db_get_setting(db, key.c_str());
                       std::string body = data ? data : "{}";
                       face_free_string(data);
                       face_db_close(db);
                       res.set_content(body, "application/json");
                   });

        server.Put("/api/settings/(.+)",
                   [this](const httplib::Request& req, httplib::Response& res) {
                       auto key = req.matches[1].str();
                       auto j = json::parse(req.body.empty() ? "{}" : req.body);
                       auto db = face_db_open(config.dbPath.c_str());
                       if (!db) {
                           res.status = 500;
                           return;
                       }
                       face_db_set_setting(db, key.c_str(), j.value("value", "").c_str(),
                                           j.value("description", "").c_str());
                       face_db_close(db);
                       res.set_content(json({{"success", true}}).dump(), "application/json");
                   });

        // ========== Available Modules ==========
        server.Get("/api/modules", [this](const httplib::Request&, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            auto* data = face_db_get_available_modules(db);
            std::string body = data ? data : "[]";
            face_free_string(data);
            face_db_close(db);
            res.set_content(body, "application/json");
        });

        // ========== CORS preflight ==========
        server.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            res.status = 204;
        });

        // ========== Groups (simplified — stored in settings) ==========
        server.Get("/api/groups", [this](const httplib::Request&, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            auto* data = face_db_get_setting(db, "groups");
            std::string body = data ? data : R"({"value":"[]"})";
            // Parse and extract just the JSON array
            try {
                auto j = json::parse(body);
                body = j.value("value", "[]");
            } catch (...) {
                body = "[]";
            }
            face_free_string(data);
            face_db_close(db);
            res.set_content(body, "application/json");
        });

        server.Post("/api/groups", [this](const httplib::Request& req, httplib::Response& res) {
            auto db = face_db_open(config.dbPath.c_str());
            if (!db) {
                res.status = 500;
                return;
            }
            // Get existing groups, append new one, save back
            auto* data = face_db_get_setting(db, "groups");
            json groups = json::array();
            if (data) {
                try {
                    auto j = json::parse(data);
                    groups = json::parse(j.value("value", "[]"));
                } catch (...) {
                }
                face_free_string(data);
            }
            auto j = json::parse(req.body);
            json newGroup = {{"id", std::to_string(groups.size() + 1)},
                             {"name", j.value("name", "")}};
            groups.push_back(newGroup);
            face_db_set_setting(db, "groups", groups.dump().c_str(), "Groups list");
            face_db_close(db);
            res.status = 201;
            res.set_content(newGroup.dump(), "application/json");
        });
    }
};

// ========== Public API ==========

HttpServer::HttpServer() : impl_(std::make_unique<Impl>()) {}
HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start(const HttpServerConfig& cfg) {
    impl_->config = cfg;
    impl_->setupRoutes();

    running_ = true;
    impl_->worker = std::thread([this]() {
        auto& srv = impl_->server;
        auto port = impl_->config.port;
        srv.set_read_timeout(300, 0);
        srv.set_write_timeout(300, 0);
        std::cout << "[HTTP Server] Starting on 0.0.0.0:" << port << std::endl;
        if (!srv.listen("0.0.0.0", port)) {
            std::cerr << "[HTTP Server] Failed to start on port " << port << std::endl;
        }
        running_ = false;
    });
    std::cout << "[HTTP Server] Listening on 0.0.0.0:" << impl_->config.port << std::endl;
    return true;
}

void HttpServer::stop() {
    if (running_) {
        impl_->server.stop();
        if (impl_->worker.joinable()) {
            impl_->worker.join();
        }
        running_ = false;
        std::cout << "[HTTP Server] Stopped." << std::endl;
    }
}

bool HttpServer::isRunning() const {
    return running_;
}

} // namespace face
