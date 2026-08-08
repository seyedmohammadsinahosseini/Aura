#include "face_ffi.h"
#include "modules/pipeline/FacePipeline.h"
#include "services/storage/FaceDatabase.h"
#include "modules/attendance/AttendanceManager.h"
#include "modules/attendance/WorkSessionTracker.h"
#include "core/config/Config.h"
#include "modules/camera/CameraManager.h"    // NEW
#include "modules/camera/CameraStreamer.h"   // NEW
#include "services/camera/CameraOrchestrator.h"
#include "services/stream/CameraPreviewHub.h"
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <vector>
#include <memory>
#include <sstream>
#include <mutex>
#include <cstring>
#include <cstdlib>

using json = nlohmann::json;

// ========== Global singleton state ==========
static std::shared_ptr<face::FaceConfig>        g_config;
static std::unique_ptr<face::FaceDatabase>      g_db;
static std::unique_ptr<face::WorkSessionTracker> g_workSessionTracker;
static std::unique_ptr<face::AttendanceManager> g_attendanceManager;
static std::unique_ptr<backend::CameraPreviewHub> g_previewHub;
static std::unique_ptr<backend::CameraOrchestrator> g_orchestrator;
static std::mutex                               g_globalMutex;
static std::string                              g_lastError;

static void setErrorUnlocked(const std::string& msg) { g_lastError = msg; }
static void setError(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_globalMutex);
    setErrorUnlocked(msg);
}

static char* copyString(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (out) std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

// ========== toJson helpers ==========

static const char* cameraTypeToString(const face::CameraType type) {
    switch (type) {
        case face::CameraType::ENTRY: return "ENTRY";
        case face::CameraType::EXIT:  return "EXIT";
        default:                      return "BOTH";
    }
}

static json toJson(const face::CameraConfig& c) {
    return {{"id",c.id},{"uid",c.uid},{"name",c.name},
            {"zoneId",c.zoneId},{"type",cameraTypeToString(c.type)},
            {"streamEnabled", c.streamEnabled},{"aiEnabled",c.aiEnabled},
            {"connectionType", c.connectionType},
            {"username", c.username},
            {"password", c.password},
            {"streamUri", c.streamUri},
            {"encoding", c.encoding},
            {"onvifXaddrs", c.onvifXaddrs},
            {"transportProtocol", c.transportProtocol}};
}
static json toJson(const face::ZoneInfo& z) {
    return {{"id",z.id},{"name",z.name},{"description",z.description},{"createdAt",z.createdAt},{"requiredAccessLevel",z.requiredAccessLevel}};
}
static json toJson(const face::PersonSchedule& s) {
    return {{"personId",s.personId},{"expectedArrival",s.expectedArrival},
            {"expectedDeparture",s.expectedDeparture},{"graceMinutes",s.graceMinutes},{"workDays",s.workDays}};
}
static json toJson(const face::BreakRecord& r) {
    return {{"id",r.id},{"sessionId",r.sessionId},{"personId",r.personId},
            {"breakStart",r.breakStart},{"breakEnd",r.breakEnd},{"breakSeconds",r.breakSeconds}};
}
static json toJson(const face::TardinessRecord& r) {
    return {{"personId",r.personId},{"date",r.date},{"expectedArrival",r.expectedArrival},
            {"actualArrival",r.actualArrival},{"tardinessSeconds",r.tardinessSeconds},{"isAbsent",r.isAbsent}};
}
static json toJson(const face::LocationRecord& r) {
    return {{"personId",r.personId},{"zoneId",r.zoneId},{"cameraId",r.cameraId},
            {"timestamp",r.timestamp},{"zoneName",r.zoneName}};
}
static json toJson(const face::DailySummary& s) {
    json bi = json::array();
    for (const auto& [start,end] : s.breakIntervals) bi.push_back({start,end});
    return {{"personId",s.personId},{"date",s.date},{"arrivalTime",s.arrivalTime},
            {"departureTime",s.departureTime},{"workSeconds",s.workSeconds},
            {"breakSeconds",s.breakSeconds},{"tardinessSeconds",s.tardinessSeconds},
            {"isAbsent",s.isAbsent},{"breakIntervals",bi}};
}

static json toJson(const face::UnknownFace& f) {
    return {{"id", f.id}, {"embedding", f.embedding}, {"trackId", f.trackId},
            {"cameraUid", f.cameraUid}, {"timestamp", f.timestamp}, {"imagePath", f.imagePath}};
}

// Convert a camera::DeviceInfo into JSON for the Flutter layer
static json deviceInfoToJson(const camera::DeviceInfo& d) {
    std::string typeStr;
    switch (d.type) {
        case camera::ConnectionType::LOCAL: typeStr = "LOCAL"; break;
        case camera::ConnectionType::RTSP:  typeStr = "RTSP";  break;
        case camera::ConnectionType::ONVIF: typeStr = "ONVIF"; break;
        default:                            typeStr = "UNKNOWN"; break;
    }

    json profiles = json::array();
    for (const auto& p : d.mediaProfiles) {
        profiles.push_back({
            {"token", p.token},
            {"name", p.name},
            {"width", p.width},
            {"height", p.height},
            {"fps", p.fps},
            {"encoding", p.encoding},
            {"streamUri", p.streamUri},
        });
    }

    return {
        {"devicePath", d.devicePath},
        {"streamUri",  d.streamUri},
        {"cardName",   d.cardName},
        {"driver",     d.driver},
        {"type",       typeStr},
        {"index",      d.index},
        {"xaddrs",     d.xaddrs},
        {"manufacturer", d.manufacturer},
        {"model",      d.model},
        {"firmwareVersion", d.firmwareVersion},
        {"serialNumber", d.serialNumber},
        {"hardwareId", d.hardwareId},
        {"onvifDeviceServiceUrl", d.onvifDeviceServiceUrl},
        {"onvifMediaServiceUrl", d.onvifMediaServiceUrl},
        {"onvifPort",  d.onvifPort},
        {"mediaProfiles", profiles},
        {"preferredWidth", d.preferredWidth},
        {"preferredHeight", d.preferredHeight},
        {"preferredFps", d.preferredFps},
    };
}

// ========== face_last_error ==========
const char* face_last_error() {
    std::lock_guard<std::mutex> lock(g_globalMutex);
    return g_lastError.c_str();
}

// ========== Init / Cleanup ==========
int face_init(const char* path_config, const char* path_db) {
    if (!path_config || !path_db) { setError("Invalid arguments to face_init"); return -1; }
    try {
        std::lock_guard<std::mutex> lock(g_globalMutex);
        g_config = std::make_shared<face::FaceConfig>(path_config);
        g_db     = std::make_unique<face::FaceDatabase>(path_db);
        g_workSessionTracker = std::make_unique<face::WorkSessionTracker>(*g_db);
        face::AttendanceConfig attConfig;
        g_attendanceManager  = std::make_unique<face::AttendanceManager>(*g_db, *g_workSessionTracker, attConfig);
        g_previewHub = std::make_unique<backend::CameraPreviewHub>();
        // Start camera orchestrator to auto-manage active cameras
        g_orchestrator = std::make_unique<backend::CameraOrchestrator>(g_config, g_db.get(), g_previewHub.get(), g_attendanceManager.get());
        if (g_orchestrator) g_orchestrator->start();
        return 0;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

void face_cleanup() {
    std::lock_guard<std::mutex> lock(g_globalMutex);
    g_attendanceManager.reset();
    g_workSessionTracker.reset();
    if (g_orchestrator) {
        g_orchestrator->stop();
        g_orchestrator.reset();
    }
    g_previewHub.reset();
    g_db.reset();
    g_config.reset();
}

// ========== FacePipeline ==========
class PipelineHolder {
public:
    std::unique_ptr<face::FacePipeline> pipeline;
    explicit PipelineHolder(std::shared_ptr<face::FaceConfig> cfg)
        : pipeline(std::make_unique<face::FacePipeline>(cfg)) {}
};

FacePipelineHandle face_pipeline_create() {
    try {
        std::lock_guard<std::mutex> lock(g_globalMutex);
        if (!g_config) { setErrorUnlocked("face_init not called"); return nullptr; }
        return static_cast<FacePipelineHandle>(new PipelineHolder(g_config));
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

void face_pipeline_destroy(FacePipelineHandle pipeline) {
    delete static_cast<PipelineHolder*>(pipeline);
}

static json processCvMat(face::FacePipeline* pipeline, cv::Mat& frame) {
    json result;
    try {
        auto res = pipeline->ProcessFrame(frame);
        json detections = json::array();
        for (const auto& det : res.detections) {
            json obj;
            obj["track_id"]       = det.mTrackID;
            obj["confidence"]     = det.mConfidence;
            obj["is_face"]        = det.face;
            obj["is_side_profile"]= det.mSideProfileView;
            obj["is_far"]         = det.mFarDistance;
            obj["is_blurry"]      = det.mBlurry;
            obj["blur_score"]     = det.mBlurScore;
            obj["bbox"]           = json::array({det.mBbox.x,det.mBbox.y,det.mBbox.width,det.mBbox.height});
            if (det.mHasEmbedding)
                obj["embedding"] = std::vector<float>(det.mEmbedding.begin(), det.mEmbedding.end());
            detections.push_back(obj);
        }

        // For face detections still lacking embeddings (single-shot upload),
        // extract them synchronously from the original frame
        for (auto& det : res.detections) {
            if (!det.mHasEmbedding && det.face && det.mKeypoint.size() >= 5) {
                try {
                    const auto emb = pipeline->extractEmbeddingSync(det, frame);
                    if (!emb.empty()) {
                        det.mEmbedding = emb;
                        det.mHasEmbedding = true;
                        // Update the JSON entry that was already serialized
                        for (auto& jdet : detections) {
                            if (jdet["track_id"] == det.mTrackID) {
                                jdet["embedding"] = std::vector<float>(emb.begin(), emb.end());
                                break;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    // Log but don't fail the whole response
                }
            }
        }

        result["detections"]        = detections;
        result["removed_track_ids"] = res.removedTrackIds;
        result["success"]           = true;
    } catch (...) {
        result["success"] = false;
        result["error"]   = "ProcessFrame exception";
    }
    return result;
}

char* face_process_frame_raw(FacePipelineHandle pipeline, const uint8_t* bgr_data, int rows, int cols, int step) {
    if (!pipeline || !bgr_data || rows <= 0 || cols <= 0) {
        setError("Invalid arguments to face_process_frame_raw"); return nullptr;
    }
    try {
        cv::Mat frame(rows, cols, CV_8UC3, const_cast<uint8_t*>(bgr_data), step);
        json j = processCvMat(static_cast<PipelineHolder*>(pipeline)->pipeline.get(), frame);
        return copyString(j.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

char* face_process_frame(FacePipelineHandle pipeline, const uint8_t* image_data, size_t image_len) {
    if (!pipeline || !image_data || image_len == 0) {
        setError("Invalid arguments to face_process_frame"); return nullptr;
    }
    try {
        std::vector<uchar> buf(image_data, image_data + image_len);
        cv::Mat img = cv::imdecode(buf, cv::IMREAD_COLOR);
        if (img.empty()) { setError("Failed to decode image"); return nullptr; }
        return face_process_frame_raw(pipeline, img.data, img.rows, img.cols, img.step);
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

// ========== FaceDatabase ==========
FaceDatabaseHandle face_db_open(const char* path_db) {
    try { return static_cast<FaceDatabaseHandle>(new face::FaceDatabase(path_db)); }
    catch (const std::exception& e) { setError(e.what()); return nullptr; }
}
void face_db_close(FaceDatabaseHandle db) { delete static_cast<face::FaceDatabase*>(db); }

int64_t face_db_add_person(FaceDatabaseHandle db, const char* name, const char* code) {
    if (!db) return -1;
    try { return static_cast<face::FaceDatabase*>(db)->addPerson(name?name:"", code?code:"", true); }
    catch (const std::exception& e) { setError(e.what()); return -1; }
}

char* face_db_get_person(FaceDatabaseHandle db, int64_t person_id) {
    if (!db) return nullptr;
    try {
        face::Person p;
        if (!static_cast<face::FaceDatabase*>(db)->getPerson(person_id, p)) {
            setError("Person not found"); return nullptr;
        }
        json j = {{"id",p.id},{"name",p.name},{"code",p.code},
                  {"picturePath",p.picturePath},{"createdAt",p.createdAt},
                  {"accessLevel",p.access_level}};
        return copyString(j.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

int face_db_add_reference_embedding(FaceDatabaseHandle db, int64_t person_id, const float* emb, size_t emb_size) {
    if (!db || !emb || emb_size != 512) { setError("Invalid embedding size (must be 512)"); return -1; }
    try {
        static_cast<face::FaceDatabase*>(db)->addReferenceEmbedding(person_id, std::vector<float>(emb, emb+emb_size));
        if (g_orchestrator) g_orchestrator->rebuildIndex();
        return 0;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_db_add_unknown_face(FaceDatabaseHandle db, const float* emb, size_t emb_size, const char* track_id, const char* camera_uid, int64_t timestamp, const char* image_path) {
    if (!db || !emb || emb_size != 512 || !track_id || !camera_uid) { setError("Invalid arguments to face_db_add_unknown_face"); return -1; }
    try {
        static_cast<face::FaceDatabase*>(db)->addUnknownFace(
            std::vector<float>(emb, emb + emb_size),
            track_id,
            camera_uid,
            timestamp,
            image_path ? image_path : "");
        return 0;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

char* face_db_get_unknown_faces(FaceDatabaseHandle db) {
    if (!db) { setError("Invalid db handle"); return nullptr; }
    try {
        json arr = json::array();
        for (const auto& f : static_cast<face::FaceDatabase*>(db)->getUnknownFaces())
            arr.push_back(toJson(f));
        return copyString(arr.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

int face_db_delete_unknown_face(FaceDatabaseHandle db, int64_t unknown_face_id) {
    if (!db) { setError("Invalid db handle"); return -1; }
    try {
        return static_cast<face::FaceDatabase*>(db)->deleteUnknownFace(unknown_face_id) ? 0 : -1;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int64_t face_db_enroll_unknown_as_new(FaceDatabaseHandle db, int64_t unknown_face_id, const char* name, const char* code, int access_level) {
    if (!db || !name) { setError("Invalid arguments to face_db_enroll_unknown_as_new"); return -1; }
    try {
        const auto id = static_cast<face::FaceDatabase*>(db)->enrollUnknownAsNewPerson(unknown_face_id, name, code ? code : "", access_level);
        if (g_orchestrator) g_orchestrator->rebuildIndex();
        return id;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_db_merge_unknown_with_person(FaceDatabaseHandle db, int64_t unknown_face_id, int64_t person_id) {
    if (!db) { setError("Invalid db handle"); return -1; }
    try {
        const auto result = static_cast<face::FaceDatabase*>(db)->mergeUnknownWithPerson(unknown_face_id, person_id);
        if (g_orchestrator) g_orchestrator->rebuildIndex();
        return result ? 0 : -1;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

void face_db_rebuild_index(void) {
    std::lock_guard<std::mutex> lock(g_globalMutex);
    if (g_orchestrator) {
        g_orchestrator->rebuildIndex();
    }
}

char* face_db_get_all_references(FaceDatabaseHandle db) {
    if (!db) return nullptr;
    try {
        json arr = json::array();
        for (const auto& [pid,emb] : static_cast<face::FaceDatabase*>(db)->getAllReferenceEmbeddings())
            arr.push_back({{"person_id",pid},{"embedding",emb}});
        return copyString(arr.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

int64_t face_db_add_raw_detection(FaceDatabaseHandle db, const char* track_id, const float* emb, size_t emb_size, const char* image_path, int64_t timestamp, const char* status, float confidence) {
    if (!db || !track_id || !emb || emb_size != 512) return -1;
    try {
        face::RawDetection det;
        det.trackId    = track_id;
        det.embedding.assign(emb, emb+emb_size);
        det.imagePath  = image_path ? image_path : "";
        det.timestamp  = timestamp;
        det.status     = status ? status : "unknown";
        det.confidence = confidence;
        return static_cast<face::FaceDatabase*>(db)->addRawDetection(det);
    } catch (...) { return -1; }
}

void face_db_update_detection_person(FaceDatabaseHandle db, int64_t detection_id, int64_t person_id) {
    if (db) static_cast<face::FaceDatabase*>(db)->updateRawDetectionPersonId(detection_id, person_id);
}

// Camera config
int64_t face_db_upsert_camera(FaceDatabaseHandle db, const char* camera_json) {
    if (!db || !camera_json) { setError("Invalid arguments to face_db_upsert_camera"); return -1; }
    try {
        const json j = json::parse(camera_json);
        face::CameraConfig c;
        c.id      = j.value("id",      0LL);
        c.uid     = j.value("uid",     std::string(""));
        c.name    = j.value("name",    std::string(""));
        c.zoneId  = j.value("zoneId",  0LL);
        c.streamEnabled = j.value("streamEnabled", true);
        c.aiEnabled = j.value("aiEnabled", true);
        c.connectionType = j.value("connectionType", std::string("RTSP"));
        c.username = j.value("username", std::string(""));
        c.password = j.value("password", std::string(""));
        c.streamUri = j.value("streamUri", std::string(""));
        c.encoding = j.value("encoding", std::string(""));
        c.onvifXaddrs = j.value("onvifXaddrs", std::string(""));
        c.transportProtocol = j.value("transportProtocol", std::string("tcp"));
        const auto type = j.value("type", std::string("BOTH"));
        if      (type == "ENTRY") c.type = face::CameraType::ENTRY;
        else if (type == "EXIT")  c.type = face::CameraType::EXIT;
        else                      c.type = face::CameraType::BOTH;
        return static_cast<face::FaceDatabase*>(db)->upsertCamera(c);
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

char* face_db_get_camera(FaceDatabaseHandle db, const char* camera_uid) {
    if (!db || !camera_uid) { setError("Invalid arguments to face_db_get_camera"); return nullptr; }
    try {
        auto cam = static_cast<face::FaceDatabase*>(db)->getCameraConfig(camera_uid);
        if (!cam.has_value()) { setError("Camera not found"); return nullptr; }
        return copyString(toJson(*cam).dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

char* face_db_get_camera_by_id(FaceDatabaseHandle db, int64_t camera_id) {
    if (!db) { setError("Invalid arguments to face_db_get_camera_by_id"); return nullptr; }
    try {
        for (const auto& c : static_cast<face::FaceDatabase*>(db)->getAllCameras()) {
            if (c.id == camera_id) return copyString(toJson(c).dump());
        }
        setError("Camera not found");
        return nullptr;
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

char* face_db_get_all_cameras(FaceDatabaseHandle db) {
    if (!db) { setError("Invalid arguments to face_db_get_all_cameras"); return nullptr; }
    try {
        json items = json::array();
        for (const auto& c : static_cast<face::FaceDatabase*>(db)->getAllCameras())
            items.push_back(toJson(c));
        return copyString(items.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

// NEW: Delete a camera from the database by UID
int face_db_delete_camera(FaceDatabaseHandle db, const char* camera_uid) {
    if (!db || !camera_uid) { setError("Invalid arguments to face_db_delete_camera"); return -1; }
    try {
        const bool ok = static_cast<face::FaceDatabase*>(db)->deleteCamera(camera_uid);
        if (ok && g_orchestrator) {
            try { g_orchestrator->notifyCameraActiveChanged(camera_uid, false); } catch (...) {}
        }
        if (ok && g_previewHub) {
            g_previewHub->removeCamera(camera_uid);
        }
        return ok ? 0 : -1;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_db_set_camera_active(FaceDatabaseHandle db, const char* camera_uid, int active) {
    if (!db || !camera_uid) { setError("Invalid arguments to face_db_set_camera_active"); return -1; }
    try {
        const bool ok = static_cast<face::FaceDatabase*>(db)->updateCameraStreamEnabled(camera_uid, active != 0);
        if (ok && g_orchestrator) {
            try { g_orchestrator->notifyCameraActiveChanged(camera_uid, active != 0); } catch (...) {}
        }
        return ok ? 0 : -1;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_db_set_camera_ai_enabled(FaceDatabaseHandle db, const char* camera_uid, int enabled) {
    if (!db || !camera_uid) { setError("Invalid arguments to face_db_set_camera_ai_enabled"); return -1; }
    try {
        const bool ok = static_cast<face::FaceDatabase*>(db)->updateCameraAiEnabled(camera_uid, enabled != 0);
        if (ok && g_orchestrator) {
            try { g_orchestrator->notifyCameraConfigChanged(camera_uid); } catch (...) {}
        }
        return ok ? 0 : -1;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

char* face_db_get_camera_module_config(FaceDatabaseHandle db, const char* camera_uid, const char* module_name) {
    if (!db || !camera_uid || !module_name) { setError("Invalid arguments to face_db_get_camera_module_config"); return nullptr; }
    try {
        const auto config = static_cast<face::FaceDatabase*>(db)->getCameraModuleConfig(camera_uid, module_name);
        if (!config.has_value()) {
            setError("Camera module config not found");
            return nullptr;
        }
        json payload = {{"enabled", config->first}, {"config_json", config->second}};
        try {
            payload["config"] = json::parse(config->second);
        } catch (...) {
            payload["config"] = config->second;
        }
        return copyString(payload.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

int face_db_set_camera_module_config(FaceDatabaseHandle db, const char* camera_uid, const char* module_name, int enabled, const char* config_json) {
    if (!db || !camera_uid || !module_name) { setError("Invalid arguments to face_db_set_camera_module_config"); return -1; }
    try {
        static_cast<face::FaceDatabase*>(db)->setCameraModuleConfig(camera_uid, module_name, enabled != 0, config_json ? config_json : "{}");
        if (g_orchestrator) {
            try { g_orchestrator->notifyCameraConfigChanged(camera_uid); } catch (...) {}
        }
        return 0;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

char* face_db_get_available_modules(FaceDatabaseHandle db) {
    if (!db || !g_config) { setError("Invalid arguments to face_db_get_available_modules"); return nullptr; }
    try {
        json items = json::array();
        for (const auto& [name, config] : g_config->Modules()) {
            json defaultConfig = json::object();
            if (!config.rawJson.empty()) {
                try {
                    defaultConfig = json::parse(config.rawJson);
                } catch (...) {
                    defaultConfig = json::object();
                }
            }
            items.push_back({
                {"name", name},
                {"enabled", config.enabled},
                {"mandatory", false},
                {"default_config", defaultConfig}
            });
        }
        return copyString(items.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

char* face_db_get_module_config(FaceDatabaseHandle db, const char* camera_uid, const char* module_name) {
    if (!db || !camera_uid || !module_name) { setError("Invalid arguments to face_db_get_module_config"); return nullptr; }
    try {
        const auto config = static_cast<face::FaceDatabase*>(db)->getCameraModuleConfig(camera_uid, module_name);
        if (!config.has_value()) {
            setError("Camera module config not found");
            return nullptr;
        }
        json payload = {{"enabled", config->first}, {"config_json", config->second}};
        try {
            payload["config"] = json::parse(config->second);
        } catch (...) {
            payload["config"] = config->second;
        }
        return copyString(payload.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

int face_db_set_module_config(FaceDatabaseHandle db, const char* camera_uid, const char* module_name, const char* config_json, int enabled) {
    if (!db || !camera_uid || !module_name) { setError("Invalid arguments to face_db_set_module_config"); return -1; }
    try {
        static_cast<face::FaceDatabase*>(db)->setCameraModuleConfig(camera_uid, module_name, enabled != 0, config_json ? config_json : "{}");
        if (g_orchestrator) {
            try { g_orchestrator->notifyCameraConfigChanged(camera_uid); } catch (...) {}
        }
        return 0;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_db_set_person_access_level(FaceDatabaseHandle db, int64_t person_id, int access_level) {
    if (!db) { setError("Invalid arguments to face_db_set_person_access_level"); return -1; }
    try {
        const bool ok = static_cast<face::FaceDatabase*>(db)->updatePersonAccessLevel(person_id, access_level);
        return ok ? 0 : -1;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_db_set_zone_required_access_level(FaceDatabaseHandle db, int64_t zone_id, int required_access_level) {
    if (!db) { setError("Invalid arguments to face_db_set_zone_required_access_level"); return -1; }
    try {
        const bool ok = static_cast<face::FaceDatabase*>(db)->updateZoneRequiredAccessLevel(zone_id, required_access_level);
        return ok ? 0 : -1;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_db_configure_camera(FaceDatabaseHandle db, const char* camera_uid, const char* config_json) {
    if (!db || !camera_uid || !config_json) { setError("Invalid arguments to face_db_configure_camera"); return -1; }
    try {
        const auto j = json::parse(config_json);
        // If camera-level fields provided, upsert camera
        auto* dbHandle = static_cast<face::FaceDatabase*>(db);
        face::CameraConfig cam;
        if (const auto existing = dbHandle->getCameraConfig(camera_uid); existing.has_value()) {
            cam = *existing;
        } else {
            cam.uid = camera_uid;
        }
        bool hasCam = false;
        if (j.contains("name")) { cam.name = j.value("name", std::string("")); hasCam = true; }
        if (j.contains("zoneId")) { cam.zoneId = j.value("zoneId", 0LL); hasCam = true; }
        if (j.contains("streamEnabled")) { cam.streamEnabled = j.value("streamEnabled", true); hasCam = true; }
        if (j.contains("aiEnabled")) { cam.aiEnabled = j.value("aiEnabled", true); hasCam = true; }
        if (j.contains("connectionType")) { cam.connectionType = j.value("connectionType", std::string("RTSP")); hasCam = true; }
        if (j.contains("username")) { cam.username = j.value("username", std::string("")); hasCam = true; }
        if (j.contains("password")) { cam.password = j.value("password", std::string("")); hasCam = true; }
        if (j.contains("streamUri")) {
            cam.streamUri = j.value("streamUri", std::string(""));
            // Auto-detect local device from path
            if (cam.streamUri.rfind("/dev/", 0) == 0) {
                cam.connectionType = "LOCAL";
            }
            hasCam = true;
        }
        if (j.contains("encoding")) { cam.encoding = j.value("encoding", std::string("")); hasCam = true; }
        if (j.contains("onvifXaddrs")) { cam.onvifXaddrs = j.value("onvifXaddrs", std::string("")); hasCam = true; }
        if (j.contains("transportProtocol")) { cam.transportProtocol = j.value("transportProtocol", std::string("tcp")); hasCam = true; }
        if (j.contains("type")) {
            const auto t = j.value("type", std::string("BOTH"));
            if (t == "ENTRY") cam.type = face::CameraType::ENTRY;
            else if (t == "EXIT") cam.type = face::CameraType::EXIT;
            else cam.type = face::CameraType::BOTH;
            hasCam = true;
        }
        if (hasCam) {
            dbHandle->upsertCamera(cam);
        }

        if (j.contains("modules") && j["modules"].is_object()) {
            for (auto it = j["modules"].begin(); it != j["modules"].end(); ++it) {
                const std::string moduleName = it.key();
                bool enabled = false;
                std::string cfg = "{}";
                if (it.value().is_object()) {
                    if (it.value().contains("enabled")) enabled = it.value().value("enabled", false);
                    if (it.value().contains("config")) cfg = it.value()["config"].dump();
                }
                dbHandle->setCameraModuleConfig(camera_uid, moduleName, enabled, cfg);
            }
        }

        if (g_orchestrator) {
            try { g_orchestrator->notifyCameraConfigChanged(camera_uid); } catch (...) {}
        }
        return 0;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_preview_subscribe(const char* camera_uid) {
    if (!camera_uid || !g_previewHub) { setError("Invalid arguments to face_preview_subscribe"); return -1; }
    try {
        g_previewHub->subscribe(camera_uid);
        if (g_orchestrator) {
            try { g_orchestrator->notifyPreviewSubscriptionsChanged(camera_uid); } catch (...) {}
        }
        return 0;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_preview_unsubscribe(const char* camera_uid) {
    if (!camera_uid || !g_previewHub) { setError("Invalid arguments to face_preview_unsubscribe"); return -1; }
    try {
        g_previewHub->unsubscribe(camera_uid);
        if (g_orchestrator) {
            try { g_orchestrator->notifyPreviewSubscriptionsChanged(camera_uid); } catch (...) {}
        }
        return 0;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

uint8_t* face_preview_get_latest_frame_jpeg(const char* camera_uid,
                                            size_t* out_len,
                                            int* out_width,
                                            int* out_height,
                                            float* out_fps) {
    if (!camera_uid || !out_len || !out_width || !out_height || !out_fps || !g_previewHub) {
        setError("Invalid arguments to face_preview_get_latest_frame_jpeg");
        return nullptr;
    }
    try {
        const auto encoded = g_previewHub->getLatestJpeg(camera_uid);
        if (!encoded.has_value() || encoded->jpegBytes.empty()) {
            return nullptr;
        }

        auto* out = static_cast<uint8_t*>(std::malloc(encoded->jpegBytes.size()));
        if (!out) {
            setError("Failed to allocate preview frame buffer");
            return nullptr;
        }

        std::memcpy(out, encoded->jpegBytes.data(), encoded->jpegBytes.size());
        *out_len = encoded->jpegBytes.size();
        *out_width = encoded->width;
        *out_height = encoded->height;
        *out_fps = encoded->fps;
        return out;
    } catch (const std::exception& e) {
        setError(e.what());
        return nullptr;
    }
}

uint8_t* face_preview_get_latest_frame_bgr(const char* camera_uid,
                                           size_t* out_len,
                                           int* out_width,
                                           int* out_height,
                                           int* out_stride,
                                           float* out_fps) {
    if (!camera_uid || !out_len || !out_width || !out_height || !out_stride || !out_fps || !g_previewHub) {
        setError("Invalid arguments to face_preview_get_latest_frame_bgr");
        return nullptr;
    }
    try {
        const auto raw = g_previewHub->getLatestBgr(camera_uid);
        if (!raw.has_value() || raw->bgrBytes.empty()) {
            return nullptr;
        }

        auto* out = static_cast<uint8_t*>(std::malloc(raw->bgrBytes.size()));
        if (!out) {
            setError("Failed to allocate BGR frame buffer");
            return nullptr;
        }

        std::memcpy(out, raw->bgrBytes.data(), raw->bgrBytes.size());
        *out_len = raw->bgrBytes.size();
        *out_width = raw->width;
        *out_height = raw->height;
        *out_stride = raw->stride;
        *out_fps = raw->fps;
        return out;
    } catch (const std::exception& e) {
        setError(e.what());
        return nullptr;
    }
}

// Zones
int64_t face_db_add_zone(FaceDatabaseHandle db, const char* zone_json) {
    if (!db || !zone_json) { setError("Invalid arguments to face_db_add_zone"); return -1; }
    try {
        const json j = json::parse(zone_json);
        face::ZoneInfo z;
        z.id          = j.value("id",          0LL);
        z.name        = j.value("name",        std::string(""));
        z.description = j.value("description", std::string(""));
        return static_cast<face::FaceDatabase*>(db)->addZone(z);
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

char* face_db_get_zone(FaceDatabaseHandle db, int64_t zone_id) {
    if (!db) { setError("Invalid arguments to face_db_get_zone"); return nullptr; }
    try {
        auto z = static_cast<face::FaceDatabase*>(db)->getZone(zone_id);
        if (!z.has_value()) { setError("Zone not found"); return nullptr; }
        return copyString(toJson(*z).dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

char* face_db_get_all_zones(FaceDatabaseHandle db) {
    if (!db) { setError("Invalid arguments to face_db_get_all_zones"); return nullptr; }
    try {
        json items = json::array();
        for (const auto& z : static_cast<face::FaceDatabase*>(db)->getAllZones())
            items.push_back(toJson(z));
        return copyString(items.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

int face_db_update_zone(FaceDatabaseHandle db, int64_t zone_id, const char* name, const char* description) {
    if (!db || !name || !description) { setError("Invalid arguments to face_db_update_zone"); return -1; }
    try {
        return static_cast<face::FaceDatabase*>(db)->updateZone(zone_id, name, description) ? 0 : -1;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_db_delete_zone(FaceDatabaseHandle db, int64_t zone_id) {
    if (!db) { setError("Invalid arguments to face_db_delete_zone"); return -1; }
    try {
        return static_cast<face::FaceDatabase*>(db)->deleteZone(zone_id) ? 0 : -1;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

// Schedule / settings
int face_db_upsert_person_schedule(FaceDatabaseHandle db, const char* schedule_json) {
    if (!db || !schedule_json) { setError("Invalid arguments to face_db_upsert_person_schedule"); return -1; }
    try {
        const json j = json::parse(schedule_json);
        face::PersonSchedule s;
        s.personId          = j.value("personId",          0LL);
        s.expectedArrival   = j.value("expectedArrival",   std::string("08:00"));
        s.expectedDeparture = j.value("expectedDeparture", std::string("17:00"));
        s.graceMinutes      = j.value("graceMinutes",      15);
        s.workDays          = j.value("workDays",          std::string("1,2,3,4,5"));
        static_cast<face::FaceDatabase*>(db)->upsertPersonSchedule(s);
        return 0;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

char* face_db_get_person_schedule(FaceDatabaseHandle db, int64_t person_id) {
    if (!db) { setError("Invalid arguments to face_db_get_person_schedule"); return nullptr; }
    try {
        auto s = static_cast<face::FaceDatabase*>(db)->getPersonSchedule(person_id);
        if (!s.has_value()) { setError("Person schedule not found"); return nullptr; }
        return copyString(toJson(*s).dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

void face_db_set_setting(FaceDatabaseHandle db, const char* key, const char* value, const char* description) {
    if (!db || !key || !value) { setError("Invalid arguments to face_db_set_setting"); return; }
    try { static_cast<face::FaceDatabase*>(db)->setSetting(key, value, description ? description : ""); }
    catch (const std::exception& e) { setError(e.what()); }
}

char* face_db_get_setting(FaceDatabaseHandle db, const char* key) {
    if (!db || !key) { setError("Invalid arguments to face_db_get_setting"); return nullptr; }
    try {
        auto v = static_cast<face::FaceDatabase*>(db)->getSetting(key);
        if (!v.has_value()) { setError("Setting not found"); return nullptr; }
        return copyString(json({{"key",key},{"value",*v}}).dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

// Reports
char* face_db_get_daily_summary(FaceDatabaseHandle db, int64_t person_id, const char* date) {
    if (!db || !date) { setError("Invalid arguments to face_db_get_daily_summary"); return nullptr; }
    try { return copyString(toJson(static_cast<face::FaceDatabase*>(db)->getDailySummary(person_id, date)).dump()); }
    catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

char* face_db_get_timeline(FaceDatabaseHandle db, int64_t person_id, const char* date) {
    if (!db || !date) { setError("Invalid arguments to face_db_get_timeline"); return nullptr; }
    try {
        json items = json::array();
        for (const auto& r : static_cast<face::FaceDatabase*>(db)->getTimeline(person_id, date))
            items.push_back(toJson(r));
        return copyString(items.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

char* face_db_get_tardiness_list(FaceDatabaseHandle db, const char* date) {
    if (!db || !date) { setError("Invalid arguments to face_db_get_tardiness_list"); return nullptr; }
    try {
        json items = json::array();
        for (const auto& r : static_cast<face::FaceDatabase*>(db)->getTardinessList(date))
            items.push_back(toJson(r));
        return copyString(items.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

char* face_db_get_current_location(FaceDatabaseHandle db, int64_t person_id) {
    if (!db) { setError("Invalid arguments to face_db_get_current_location"); return nullptr; }
    try {
        auto loc = static_cast<face::FaceDatabase*>(db)->getPersonCurrentLocation(person_id);
        if (!loc.has_value()) { setError("Current location not found"); return nullptr; }
        return copyString(toJson(*loc).dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

// ========== AttendanceManager ==========
class AttendanceHolder {
public:
    std::unique_ptr<face::WorkSessionTracker> tracker;
    std::unique_ptr<face::AttendanceManager>  manager;
    AttendanceHolder(face::FaceDatabase& db, const face::AttendanceConfig& cfg)
        : tracker(std::make_unique<face::WorkSessionTracker>(db))
        , manager(std::make_unique<face::AttendanceManager>(db, *tracker, cfg)) {}
};

AttendanceManagerHandle face_attendance_create(FaceDatabaseHandle db, const char* defaults_json) {
    if (!db) return nullptr;
    try {
        face::AttendanceConfig cfg;
        if (defaults_json) {
            json j = json::parse(defaults_json);
            cfg.defaultArrival   = j.value("defaultArrival",   cfg.defaultArrival);
            cfg.defaultDeparture = j.value("defaultDeparture", cfg.defaultDeparture);
            cfg.lateGraceMinutes = j.value("lateGraceMinutes", cfg.lateGraceMinutes);
        }
        return static_cast<AttendanceManagerHandle>(new AttendanceHolder(*static_cast<face::FaceDatabase*>(db), cfg));
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

void face_attendance_destroy(AttendanceManagerHandle att) { delete static_cast<AttendanceHolder*>(att); }

char* face_attendance_mark_arrival(AttendanceManagerHandle att, const char* track_id, int64_t person_id, int64_t timestamp) {
    if (!att || !track_id) return nullptr;
    try {
        bool ok = static_cast<AttendanceHolder*>(att)->manager->markArrival(track_id, person_id, timestamp);
        json res = {{"success",ok}};
        if (!ok) res["error"] = "markArrival failed";
        return copyString(res.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

char* face_attendance_mark_departure(AttendanceManagerHandle att, const char* track_id, int64_t person_id, int64_t timestamp) {
    if (!att || !track_id) return nullptr;
    try {
        bool ok = static_cast<AttendanceHolder*>(att)->manager->markDeparture(track_id, person_id, timestamp);
        return copyString(json({{"success",ok}}).dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

void face_attendance_remove_track(AttendanceManagerHandle att, const char* track_id) {
    if (att && track_id) static_cast<AttendanceHolder*>(att)->manager->removeTrack(track_id);
}

char* face_attendance_get_status(AttendanceManagerHandle att, const char* track_id) {
    if (!att || !track_id) return nullptr;
    try {
        auto state = static_cast<AttendanceHolder*>(att)->manager->getTrackState(track_id);
        if (!state.has_value()) return nullptr;
        return copyString(json({{"person_id",state->personId},{"arrived",state->arrived},{"arrival_time",state->arrivalTime}}).dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

// ========== CameraManager ==========

CameraManagerHandle face_camera_manager_create() {
    try {
        return static_cast<CameraManagerHandle>(new camera::CameraManager());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

void face_camera_manager_destroy(CameraManagerHandle mgr) {
    if (!mgr) return;
    auto* m = static_cast<camera::CameraManager*>(mgr);
    m->closeAll();
    delete m;
}

char* face_camera_discover(CameraManagerHandle mgr, int deep_search) {
    if (!mgr) { setError("Invalid CameraManagerHandle"); return nullptr; }
    try {
        auto devices = static_cast<camera::CameraManager*>(mgr)->discoverAllCameras(deep_search != 0);
        json arr = json::array();
        for (const auto& d : devices)
            arr.push_back(deviceInfoToJson(d));
        return copyString(arr.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

// ========== CameraStreamer ==========

// Internal holder so we can hold the shared_ptr alive and access the streamer.
struct StreamerHolder {
    std::shared_ptr<camera::CameraStreamer> streamer;
};

CameraStreamerHandle face_camera_open(CameraManagerHandle mgr, const char* device_path_or_uri) {
    if (!mgr || !device_path_or_uri) { setError("Invalid arguments to face_camera_open"); return nullptr; }
    try {
        auto* m = static_cast<camera::CameraManager*>(mgr);

        // Build a minimal DeviceInfo from the path/URI string so CameraManager can open it.
        camera::DeviceInfo info;
        std::string uri(device_path_or_uri);

        if (uri.rfind("/dev/", 0) == 0) {
            // Local V4L2 device
            info.type       = camera::ConnectionType::LOCAL;
            info.devicePath = uri;
            info.streamUri  = uri;
            try { info.index = std::stoi(uri.substr(10)); } catch (...) { info.index = 0; }
            info.cardName   = "Local Camera";
        } else {
            // Network (RTSP / ONVIF)
            info.type       = camera::ConnectionType::RTSP;
            info.devicePath = uri;   // used as map key inside CameraManager
            info.streamUri  = uri;
            info.index      = -1;
            info.cardName   = "Network Camera";
        }

        auto shared = m->openCamera(info);
        if (!shared) { setError("Failed to open camera: " + uri); return nullptr; }

        auto* holder = new StreamerHolder{shared};
        return static_cast<CameraStreamerHandle>(holder);
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

void face_camera_close(CameraStreamerHandle streamer) {
    if (!streamer) return;
    auto* h = static_cast<StreamerHolder*>(streamer);
    h->streamer->stop();
    delete h;
}

// Returns raw BGR data (caller must free with face_free_bytes)
uint8_t* face_camera_get_frame_bgr(CameraStreamerHandle streamer, 
                                   int* out_rows, int* out_cols, size_t* out_step) {
    auto* h = static_cast<StreamerHolder*>(streamer);
    cv::Mat frame;
    uint64_t dummy;
    if (!h->streamer->getLatestFrame(frame, dummy) || frame.empty()) return nullptr;
    
    // Allocate memory for BGR data (3 channels)
    size_t total_bytes = frame.total() * frame.elemSize();
    uint8_t* out = static_cast<uint8_t*>(std::malloc(total_bytes));
    std::memcpy(out, frame.data, total_bytes);
    *out_rows = frame.rows;
    *out_cols = frame.cols;
    *out_step = frame.step;
    return out;
}

char* face_db_get_all_persons(FaceDatabaseHandle db) {
    if (!db) { setError("Invalid db handle"); return nullptr; }
    try {
        auto* dbs = static_cast<face::FaceDatabase*>(db);
        auto persons = dbs->getAllPersons();
        json arr = json::array();
        for (const auto& p : persons) {
            json obj;
            obj["id"] = p.id;
            obj["name"] = p.name;
            obj["code"] = p.code;
            obj["picturePath"] = p.picturePath;
            obj["createdAt"] = p.createdAt;
            obj["accessLevel"] = p.access_level;
            arr.push_back(std::move(obj));
        }
        return copyString(arr.dump());
    } catch (const std::exception& e) {
        setError(e.what());
        return nullptr;
    }
}

int face_db_update_person(FaceDatabaseHandle db, int64_t person_id, const char* name, const char* code) {
    if (!db) return -1;
    try {
        static_cast<face::FaceDatabase*>(db)->updatePerson(person_id, name?name:"", code?code:"");
        return 0;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

int face_db_delete_person(FaceDatabaseHandle db, int64_t person_id) {
    if (!db) { setError("Invalid db handle"); return -1; }
    try {
        const bool ok = static_cast<face::FaceDatabase*>(db)->deletePerson(person_id);
        if (ok && g_orchestrator) {
            g_orchestrator->rebuildIndex();
        }
        return ok ? 0 : -1;
    } catch (const std::exception& e) { setError(e.what()); return -1; }
}

char* face_db_get_unknown_detections(FaceDatabaseHandle db) {
    if (!db) { setError("Invalid db handle"); return nullptr; }
    try {
        auto* dbs = static_cast<face::FaceDatabase*>(db);
        auto unknown = dbs->getUnknownDetectionsNotProcessed();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& det : unknown) {
            nlohmann::json item;
            item["id"] = det.id;
            item["personId"] = det.personId;
            item["trackId"] = det.trackId;
            item["imagePath"] = det.imagePath;
            item["timestamp"] = det.timestamp;
            item["status"] = det.status;
            arr.push_back(item);
        }
        return copyString(arr.dump());
    } catch (const std::exception& e) {
        setError(e.what());
        return nullptr;
    }
}

char* face_db_get_all_detections(FaceDatabaseHandle db) {
    if (!db) { setError("Invalid db handle"); return nullptr; }
    try {
        auto* dbs = static_cast<face::FaceDatabase*>(db);
        auto all = dbs->getAllDetections();
        json arr = json::array();
        for (const auto& det : all) {
            arr.push_back({
                {"id", det.id},
                {"personId", det.personId},
                {"trackId", det.trackId},
                {"imagePath", det.imagePath},
                {"timestamp", det.timestamp},
                {"status", det.status},
                {"confidence", det.confidence}
            });
        }
        return copyString(arr.dump());
    } catch (const std::exception& e) { setError(e.what()); return nullptr; }
}

// ========== Helpers ==========
void face_free_string(char* str) { free(str); }
void face_free_bytes(uint8_t* ptr) { free(ptr); }
