#include "FaceAuthenticator.h"
#include "Utils.h"
#include "core/StaticModule.h"

#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <type_traits>

namespace {

constexpr const char* kModuleDetection = "detection";
constexpr const char* kModuleTracking = "tracking";
constexpr const char* kModuleRecognition = "recognition";
constexpr const char* kModuleLiveness = "liveness";
constexpr const char* kModuleAttendance = "attendance";

template <typename T>
void applyOptionalNumber(const std::optional<std::string>& value, T& outValue) {
    if (!value.has_value()) {
        return;
    }
    if constexpr (std::is_integral_v<T>) {
        outValue = static_cast<T>(std::stoll(*value));
    } else {
        outValue = static_cast<T>(std::stof(*value));
    }
}

std::string jsonValueToString(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    return value.dump();
}

} // namespace

namespace face {

// اضافه کردن یک ماکرو ساده برای لاگ دیباگ (می‌توانید از logger واقعی استفاده کنید)
#define DEBUG_LOG(msg)                                                                             \
    do {                                                                                           \
        std::cerr << "[DEBUG] " << msg << std::endl;                                               \
    } while (0)
#define DEBUG_LOG_VAL(msg, val)                                                                    \
    do {                                                                                           \
        std::cerr << "[DEBUG] " << msg << ": " << val << std::endl;                                \
    } while (0)

FaceAuthenticator::FaceAuthenticator(const std::shared_ptr<FaceConfig>& config)
    : config_(config), db_(std::make_unique<FaceDatabase>(config_->Database().path)),
      searcher_(std::make_unique<FaceSearcher>(static_cast<int>(DIM))),
      workSessionTracker_(std::make_unique<WorkSessionTracker>(*db_)),
      attendanceMgr_(
          std::make_unique<AttendanceManager>(*db_, *workSessionTracker_, config_->Attendance())),
      motionAnalyzer_(std::make_unique<MotionAnalyzer>(
          MotionConfig{config_->Recognition().motionWindowSize,
                       config_->Recognition().motionThresholdPx, false})),
      locationTracker_(std::make_unique<LocationTracker>(*db_)),
      licenseManager_(config_->LicensePath()), recognitionConfig_(config_->Recognition()),
      attendanceConfig_(config_->Attendance()), threshold_(config_->Recognition().faissThreshold) {
    DEBUG_LOG("FaceAuthenticator constructed");
    reloadIndex();
    reloadRuntimeSettings();
}

std::vector<SFace> FaceAuthenticator::processFrame(const std::string& cameraUid, cv::Mat& frame) {
    DEBUG_LOG_VAL("processFrame called for camera", cameraUid);
    auto& runtime = getOrCreateRuntime(cameraUid);
    auto result = runtime.pipeline->ProcessFrame(frame);
    const auto timestamp = currentUnixTimestamp();
    DEBUG_LOG_VAL("ProcessFrame returned detections count", result.detections.size());

    for (auto& detection : result.detections) {
        if (!detection.face) {
            DEBUG_LOG("Detection missing face object");
            continue;
        }
        if (!detection.mHasEmbedding) {
            DEBUG_LOG("Detection has no embedding");
            continue;
        }
        if (detection.mTrackID.empty()) {
            DEBUG_LOG("Detection has empty track ID");
            continue;
        }
        DEBUG_LOG_VAL("Processing detection with trackId", detection.mTrackID);
        processDetection(detection, buildScopedTrackId(cameraUid, detection.mTrackID), cameraUid,
                         timestamp, frame);
    }

    for (const auto& trackId : result.removedTrackIds) {
        DEBUG_LOG_VAL("Track lost:", trackId);
        onTrackLost(buildScopedTrackId(cameraUid, trackId));
    }

    return result.detections;
}

void FaceAuthenticator::drawFrame(const std::string& cameraUid, cv::Mat& frame,
                                  const std::vector<SFace>& detections) {
    auto& runtime = getOrCreateRuntime(cameraUid);
    bool detectionModuleRunning = isModuleRunning(runtime.registry, kModuleDetection);
    bool recognitionModuleRunning = isModuleRunning(runtime.registry, kModuleRecognition);
    DEBUG_LOG_VAL("drawFrame - detection module running:", detectionModuleRunning);
    DEBUG_LOG_VAL("drawFrame - recognition module running:", recognitionModuleRunning);

    if (detectionModuleRunning) {
        DetectionDrawer{}.draw(frame, detections);
    }
    if (recognitionModuleRunning) {
        RecognitionDrawer{}.draw(frame, detections);
    }
}

void FaceAuthenticator::reloadIndex() {
    std::lock_guard<std::mutex> lock(mutex_);
    DEBUG_LOG("Reloading FAISS index");
    searcher_->rebuildIndex(db_->getAllReferenceEmbeddings());
}

void FaceAuthenticator::reloadRuntimeSettings() {
    std::lock_guard<std::mutex> lock(mutex_);
    DEBUG_LOG("Reloading runtime settings");
    config_->Reload();
    licenseManager_ = backend::LicenseManager(config_->LicensePath());
    recognitionConfig_ = config_->Recognition();
    attendanceConfig_ = config_->Attendance();

    applyOptionalNumber(db_->getSetting("recognition.faiss_threshold"),
                        recognitionConfig_.faissThreshold);
    applyOptionalNumber(db_->getSetting("recognition.motion_window_size"),
                        recognitionConfig_.motionWindowSize);
    applyOptionalNumber(db_->getSetting("recognition.motion_threshold_px"),
                        recognitionConfig_.motionThresholdPx);

    if (const auto value = db_->getSetting("attendance.default_arrival"); value.has_value()) {
        attendanceConfig_.defaultArrival = *value;
    }
    if (const auto value = db_->getSetting("attendance.default_departure"); value.has_value()) {
        attendanceConfig_.defaultDeparture = *value;
    }
    applyOptionalNumber(db_->getSetting("attendance.late_grace_minutes"),
                        attendanceConfig_.lateGraceMinutes);

    threshold_ = recognitionConfig_.faissThreshold;
    DEBUG_LOG_VAL("FAISS threshold set to", threshold_);
    motionAnalyzer_->updateConfig(MotionConfig{recognitionConfig_.motionWindowSize,
                                               recognitionConfig_.motionThresholdPx, false});
    attendanceMgr_->updateDefaults(attendanceConfig_);

    for (auto& [cameraUid, runtime] : runtimes_) {
        rebuildRuntimeModules(cameraUid, runtime);
    }
}

int64_t FaceAuthenticator::enrollPerson(const std::string& name, const std::string& code,
                                        const std::vector<float>& emb) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto personId = db_->addPerson(name, code, true);
    db_->addReferenceEmbedding(personId, emb);
    searcher_->addEmbedding(personId, emb);
    return personId;
}

int64_t FaceAuthenticator::addPerson(const std::string& name, const std::string& code,
                                     const cv::Mat& image, std::vector<float>& emb) {
    auto& runtime = getOrCreateRuntime("manual_enrollment");
    cv::Mat frame = image.clone();
    auto result = runtime.pipeline->ProcessFrame(frame);
    if (result.detections.empty() || !result.detections[0].face ||
        !result.detections[0].mHasEmbedding) {
        return -1;
    }

    emb = toVector(result.detections[0].mEmbedding);

    std::lock_guard<std::mutex> lock(mutex_);
    const auto personId = db_->addPerson(name, code, true, image, config_->Database().cropsDir);
    db_->addReferenceEmbedding(personId, emb);
    searcher_->addEmbedding(personId, emb);
    return personId;
}

void FaceAuthenticator::addReferenceEmbedding(int64_t personId, const std::vector<float>& emb) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_->addReferenceEmbedding(personId, emb);
    searcher_->addEmbedding(personId, emb);
}

int64_t FaceAuthenticator::identify(const std::vector<float>& emb, float& confidence) {
    std::lock_guard<std::mutex> lock(mutex_);
    float distance = 1.0F;
    const auto personId = searcher_->search(emb, distance);
    DEBUG_LOG_VAL("identify: personId =", personId);
    DEBUG_LOG_VAL("identify: distance =", distance);
    DEBUG_LOG_VAL("identify: threshold =", threshold_);
    if (personId == -1 || distance >= threshold_) {
        confidence = 0.0F;
        DEBUG_LOG("identify: confidence set to 0 (not recognized)");
        return -1;
    }

    confidence = std::max(0.0F, 1.0F - distance);
    DEBUG_LOG_VAL("identify: confidence =", confidence);
    return personId;
}

void FaceAuthenticator::processDetection(face::SFace& face, const std::string& trackId,
                                         const std::string& cameraUid, int64_t timestamp,
                                         const cv::Mat& originalFrame) {
    DEBUG_LOG_VAL("processDetection for trackId", trackId);
    if (trackId.empty() || originalFrame.empty()) {
        DEBUG_LOG("trackId empty or originalFrame empty, returning");
        return;
    }

    auto& runtime = getOrCreateRuntime(cameraUid);
    const auto camera = getOrCreateDefaultCamera(cameraUid);
    if (!camera.has_value()) {
        DEBUG_LOG("Camera config not found for UID: " + cameraUid);
        return;
    }
    DEBUG_LOG_VAL("Camera streamEnabled:", camera->streamEnabled);
    DEBUG_LOG_VAL("Camera aiEnabled:", camera->aiEnabled);
    bool recognitionRunning = isModuleRunning(runtime.registry, kModuleRecognition);
    DEBUG_LOG_VAL("Recognition module running:", recognitionRunning);
    if (!camera->streamEnabled || !camera->aiEnabled || !recognitionRunning) {
        DEBUG_LOG(
            "Camera stream/AI disabled or recognition module not running, skipping processing");
        return;
    }

    const auto embedding = toVector(face.mEmbedding);
    const std::string& cropsDir = config_->Database().cropsDir;
    ensureDirectoryExists(cropsDir);

    float confidence = 0.0F;
    const int64_t personId = identify(embedding, confidence);
    DEBUG_LOG_VAL("Identified personId:", personId);

    const std::string cropPath = buildCropPath(cropsDir, timestamp, personId, trackId);
    face.mPersonId = personId;
    face.mRecognitionConfidence = confidence;
    face.mDisplayName = personId == -1 ? "Unknown" : lookupPersonName(*db_, personId);
    DEBUG_LOG_VAL("Display name set to:", face.mDisplayName);

    motionAnalyzer_->update(trackId, face.mBbox);

    // پردازش افراد ناشناس
    if (personId == -1) {
        DEBUG_LOG("Person is unknown, handling unknown logic");
        if (!isModuleRunning(runtime.registry, kModuleAttendance)) {
            DEBUG_LOG("Attendance module not running, unknown person not logged");
            return;
        }

        if (unknown_cropped_track_ids_.find(trackId) == unknown_cropped_track_ids_.end()) {
            DEBUG_LOG("Saving crop for unknown person");
            saveCroppedFace(originalFrame, face.mBbox, cropPath);
            unknown_cropped_track_ids_.insert(trackId);
            db_->addUnknownFace(embedding, trackId, cameraUid, timestamp, cropPath);
            DEBUG_LOG("Stored unknown face embedding in unknown_faces table");
        }

        auto direction = motionAnalyzer_->getDirection(trackId);
        DEBUG_LOG_VAL("Unknown track direction:", (int)direction);
        bool changed = false;
        std::string eventType;
        {
            std::lock_guard<std::mutex> lock(unknownMutex_);
            auto& state = unknownTracks_[trackId];
            if (camera->type == CameraType::ENTRY) {
                if (!state.arrived) {
                    state.arrived = true;
                    state.arrivalTime = timestamp;
                    changed = true;
                    eventType = "arrival";
                    DEBUG_LOG("Unknown arrival (entry camera)");
                }
            } else if (camera->type == CameraType::EXIT) {
                if (state.arrived) {
                    state.arrived = false;
                    changed = true;
                    eventType = "departure";
                    DEBUG_LOG("Unknown departure (exit camera)");
                }
            } else {
                if (direction == Direction::IN && !state.arrived) {
                    state.arrived = true;
                    state.arrivalTime = timestamp;
                    changed = true;
                    eventType = "arrival";
                    DEBUG_LOG("Unknown arrival (direction IN)");
                } else if (direction == Direction::OUT && state.arrived) {
                    state.arrived = false;
                    changed = true;
                    eventType = "departure";
                    DEBUG_LOG("Unknown departure (direction OUT)");
                }
            }
        }
        return;
    }

    // پردازش افراد شناخته شده
    DEBUG_LOG("Person is known, processing attendance");
    if (!isModuleRunning(runtime.registry, kModuleAttendance)) {
        DEBUG_LOG("Attendance module not running, known person not logged");
        return;
    }

    auto currentLocation = locationTracker_->getLocation(personId);
    const bool locationChanged = !currentLocation.has_value() ||
        currentLocation->zoneId != camera->zoneId || currentLocation->cameraId != camera->id;
    if (locationChanged) {
        locationTracker_->updateLocation(personId, camera->zoneId, camera->id, timestamp);
        DEBUG_LOG("Location updated for person");
    }

    const auto direction = motionAnalyzer_->getDirection(trackId);
    DEBUG_LOG_VAL("Direction for known person:", (int)direction);
    bool changed = false;
    std::string eventType;

    switch (camera->type) {
        case CameraType::ENTRY:
            changed = attendanceMgr_->markArrival(trackId, personId, timestamp);
            eventType = "arrival";
            DEBUG_LOG("Entry camera: markArrival result = " << changed);
            break;
        case CameraType::EXIT:
            changed = attendanceMgr_->markDeparture(trackId, personId, timestamp);
            eventType = "departure";
            DEBUG_LOG("Exit camera: markDeparture result = " << changed);
            break;
        case CameraType::BOTH:
        default:
            if (direction == Direction::IN) {
                changed = attendanceMgr_->markArrival(trackId, personId, timestamp);
                eventType = "arrival";
                DEBUG_LOG("Both camera, direction IN: markArrival = " << changed);
            } else if (direction == Direction::OUT) {
                changed = attendanceMgr_->markDeparture(trackId, personId, timestamp);
                eventType = "departure";
                DEBUG_LOG("Both camera, direction OUT: markDeparture = " << changed);
            } else {
                const auto status = attendanceMgr_->getCurrentStatus(trackId);
                DEBUG_LOG_VAL("Both camera, undetermined direction, current status:", status);
                if (status == "IN") {
                    changed = attendanceMgr_->markDeparture(trackId, personId, timestamp);
                    eventType = "departure";
                } else {
                    changed = attendanceMgr_->markArrival(trackId, personId, timestamp);
                    eventType = "arrival";
                }
                DEBUG_LOG("Fallback: changed = " << changed);
            }
            break;
    }

    if (changed) {
        DEBUG_LOG("Attendance changed, adding log and saving crop");
        std::lock_guard<std::mutex> lock(mutex_);
        db_->addAttendanceLog(personId, trackId, eventType, timestamp);
        saveCroppedFace(originalFrame, face.mBbox, cropPath);
    } else {
        DEBUG_LOG("No change in attendance state, skipping log and crop");
    }
}

void FaceAuthenticator::onTrackLost(const std::string& trackId) {
    DEBUG_LOG_VAL("onTrackLost called for", trackId);
    if (trackId.empty()) {
        return;
    }

    const auto cameraUid = extractCameraUid(trackId);
    auto& runtime = getOrCreateRuntime(cameraUid);

    if (isModuleRunning(runtime.registry, kModuleAttendance)) {
        const auto state = attendanceMgr_->getTrackState(trackId);
        if (state.has_value() && state->personId != -1) {
            const auto timestamp = currentUnixTimestamp();
            DEBUG_LOG("Track lost, marking departure for person " << state->personId);
            if (attendanceMgr_->markDeparture(trackId, state->personId, timestamp)) {
                std::lock_guard<std::mutex> lock(mutex_);
                db_->addAttendanceLog(state->personId, trackId, "departure", timestamp);
            }
            locationTracker_->clearLocation(state->personId);
        } else {
            DEBUG_LOG("Track lost but no valid person state");
        }
    }

    motionAnalyzer_->removeTrack(trackId);
    attendanceMgr_->removeTrack(trackId);
}

void FaceAuthenticator::ResetUnknownCropCache() {
    unknown_cropped_track_ids_.clear();
}

int64_t FaceAuthenticator::upsertCamera(const CameraConfig& camera) {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_->upsertCamera(camera);
}

std::optional<CameraConfig> FaceAuthenticator::getCameraConfig(const std::string& cameraUid) {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_->getCameraConfig(cameraUid);
}

std::vector<CameraConfig> FaceAuthenticator::getAllCameras() {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_->getAllCameras();
}

std::optional<std::pair<bool, std::string>>
FaceAuthenticator::getCameraModuleConfig(const std::string& cameraUid,
                                         const std::string& moduleName) {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_->getCameraModuleConfig(cameraUid, moduleName);
}

void FaceAuthenticator::setCameraModuleConfig(const std::string& cameraUid,
                                              const std::string& moduleName, bool enabled,
                                              const std::string& configJson) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        db_->setCameraModuleConfig(cameraUid, moduleName, enabled, configJson);
    }
    auto& runtime = getOrCreateRuntime(cameraUid);
    rebuildRuntimeModules(cameraUid, runtime);
}

void FaceAuthenticator::upsertSchedule(const PersonSchedule& schedule) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_->upsertPersonSchedule(schedule);
}

std::optional<PersonSchedule> FaceAuthenticator::getPersonSchedule(int64_t personId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_->getPersonSchedule(personId);
}

std::optional<LocationRecord> FaceAuthenticator::getPersonLocation(int64_t personId) {
    return locationTracker_->getLocation(personId);
}

std::map<int64_t, LocationRecord> FaceAuthenticator::getAllCurrentLocations() {
    return locationTracker_->getAllLocations();
}

DailySummary FaceAuthenticator::getDailySummary(int64_t personId, const std::string& date) {
    return workSessionTracker_->getDailySummary(personId, date);
}

std::vector<BreakRecord> FaceAuthenticator::getTimeline(int64_t personId, const std::string& date) {
    return workSessionTracker_->getTimeline(personId, date);
}

std::vector<TardinessRecord> FaceAuthenticator::getTardinessList(const std::string& date) {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_->getTardinessList(date);
}

FaceAuthenticator::CameraRuntime&
FaceAuthenticator::getOrCreateRuntime(const std::string& cameraUid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = runtimes_.find(cameraUid);
    if (it == runtimes_.end()) {
        CameraRuntime runtime;
        runtime.pipeline = std::make_unique<FacePipeline>(config_);
        registerBuiltInModules(runtime.registry);
        auto inserted = runtimes_.emplace(cameraUid, std::move(runtime));
        it = inserted.first;
        rebuildRuntimeModules(cameraUid, it->second);
    } else {
        applyRuntimeModules(cameraUid, it->second);
    }
    return it->second;
}

std::optional<CameraConfig>
FaceAuthenticator::getOrCreateDefaultCamera(const std::string& cameraUid) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto existing = db_->getCameraConfig(cameraUid); existing.has_value()) {
            return existing;
        }
    }

    CameraConfig camera;
    camera.uid = cameraUid;
    camera.name = cameraUid;
    camera.zoneId = 0;
    camera.type = CameraType::BOTH;
    camera.streamEnabled = true;
    camera.aiEnabled = true;
    upsertCamera(camera);
    return getCameraConfig(cameraUid);
}

void FaceAuthenticator::rebuildRuntimeModules(const std::string& cameraUid,
                                              CameraRuntime& runtime) {
    runtime.registry = backend::PluginRegistry{};
    registerBuiltInModules(runtime.registry);

    std::unordered_map<std::string, backend::ModuleConfig> configs;
    for (const auto& [name, config] : config_->Modules()) {
        configs[name] = toBackendConfig(config);
    }

    if (licenseManager_.isValid()) {
        runtime.registry.loadAll(licenseManager_.entitlements(), configs);
    }

    applyRuntimeModules(cameraUid, runtime);
}

void FaceAuthenticator::applyRuntimeModules(const std::string& cameraUid, CameraRuntime& runtime) {
    for (const auto& [name, baseConfig] : config_->Modules()) {
        const auto merged = mergeModuleConfig(toBackendConfig(baseConfig),
                                              db_->getCameraModuleConfig(cameraUid, name));
        runtime.registry.updateModuleConfig(name, merged);
    }

    runtime.pipeline->setDetectionEnabled(isModuleRunning(runtime.registry, kModuleDetection));
    runtime.pipeline->setTrackingEnabled(isModuleRunning(runtime.registry, kModuleTracking));
    runtime.pipeline->setRecognitionEnabled(isModuleRunning(runtime.registry, kModuleRecognition));
    runtime.pipeline->setLivenessEnabled(isModuleRunning(runtime.registry, kModuleLiveness));

    backend::ModuleConfig trackingConfig{};
    if (config_->Modules().contains(kModuleTracking)) {
        trackingConfig = mergeModuleConfig(toBackendConfig(config_->Modules().at(kModuleTracking)),
                                           db_->getCameraModuleConfig(cameraUid, kModuleTracking));
    }
    runtime.pipeline->setMotionShortCircuitEnabled(
        boolValue(trackingConfig.parameters, "motion_short_circuit", false));
    runtime.pipeline->updateMotionParameters(
        intValue(trackingConfig.parameters, "motion_threshold", config_->MotionAreaThreshold()),
        intValue(trackingConfig.parameters, "motion_buffer", config_->MotionBufferFrames()),
        intValue(trackingConfig.parameters, "motion_diff_threshold",
                 config_->MotionDiffThreshold()),
        intValue(trackingConfig.parameters, "blur_kernel_size", config_->MotionBlurKernelSize()),
        intValue(trackingConfig.parameters, "dilate_kernel_size",
                 config_->MotionDilateKernelSize()));

    backend::ModuleConfig recognitionModule{};
    if (config_->Modules().contains(kModuleRecognition)) {
        recognitionModule =
            mergeModuleConfig(toBackendConfig(config_->Modules().at(kModuleRecognition)),
                              db_->getCameraModuleConfig(cameraUid, kModuleRecognition));
    }
    threshold_ = floatValue(recognitionModule.parameters, "faiss_threshold",
                            recognitionConfig_.faissThreshold);
    motionAnalyzer_->updateConfig(
        MotionConfig{intValue(recognitionModule.parameters, "motion_window_size",
                              recognitionConfig_.motionWindowSize),
                     floatValue(recognitionModule.parameters, "motion_threshold_px",
                                recognitionConfig_.motionThresholdPx),
                     false});
}

void FaceAuthenticator::registerBuiltInModules(backend::PluginRegistry& registry) {
    registry.registerFactory(kModuleDetection, []() {
        return std::make_shared<backend::StaticModule>(kModuleDetection);
    });
    registry.registerFactory(
        kModuleTracking, []() { return std::make_shared<backend::StaticModule>(kModuleTracking); });
    registry.registerFactory(kModuleRecognition, []() {
        return std::make_shared<backend::StaticModule>(kModuleRecognition);
    });
    registry.registerFactory(
        kModuleLiveness, []() { return std::make_shared<backend::StaticModule>(kModuleLiveness); });
    registry.registerFactory(kModuleAttendance, []() {
        return std::make_shared<backend::StaticModule>(kModuleAttendance);
    });
}

backend::ModuleConfig FaceAuthenticator::toBackendConfig(const face::ModuleConfig& config) {
    backend::ModuleConfig out;
    out.enabled = config.enabled;
    out.parameters = config.parameters;
    out.rawJson = config.rawJson;
    return out;
}

backend::ModuleConfig FaceAuthenticator::mergeModuleConfig(
    const backend::ModuleConfig& baseConfig,
    const std::optional<std::pair<bool, std::string>>& overrideConfig) {
    backend::ModuleConfig merged = baseConfig;
    if (!overrideConfig.has_value()) {
        return merged;
    }

    merged.enabled = overrideConfig->first;
    merged.rawJson = overrideConfig->second;

    try {
        const auto configJson = nlohmann::json::parse(overrideConfig->second);
        if (configJson.is_object()) {
            for (auto it = configJson.begin(); it != configJson.end(); ++it) {
                if (it.key() == "enabled") {
                    continue;
                }
                merged.parameters[it.key()] = jsonValueToString(it.value());
            }
        }
    } catch (...) {
    }

    return merged;
}

bool FaceAuthenticator::isModuleRunning(const backend::PluginRegistry& registry,
                                        const std::string& moduleName) {
    const auto module = registry.getModule(moduleName);
    return module && module->state() == backend::ModuleState::Running;
}

bool FaceAuthenticator::boolValue(const std::unordered_map<std::string, std::string>& params,
                                  const std::string& key, bool fallback) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return fallback;
    }
    return it->second == "true" || it->second == "1";
}

int FaceAuthenticator::intValue(const std::unordered_map<std::string, std::string>& params,
                                const std::string& key, int fallback) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return fallback;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

float FaceAuthenticator::floatValue(const std::unordered_map<std::string, std::string>& params,
                                    const std::string& key, float fallback) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return fallback;
    }
    try {
        return std::stof(it->second);
    } catch (...) {
        return fallback;
    }
}

std::string FaceAuthenticator::lookupPersonName(FaceDatabase& db, int64_t personId) {
    DEBUG_LOG_VAL("lookupPersonName called with personId", personId);
    if (personId < 0)
        return "Unknown";
    Person person;
    if (!db.getPerson(personId, person)) {
        DEBUG_LOG("Person not found in DB");
        return "Person#" + std::to_string(personId);
    }
    DEBUG_LOG_VAL("Person name:", person.name);
    DEBUG_LOG_VAL("Person code:", person.code);
    if (!person.name.empty())
        return person.name;
    if (!person.code.empty())
        return person.code;
    return "Person#" + std::to_string(personId);
}

std::vector<float> FaceAuthenticator::toVector(const FeatureArray& embedding) {
    return std::vector<float>(embedding.begin(), embedding.end());
}

int64_t FaceAuthenticator::currentUnixTimestamp() {
    return static_cast<int64_t>(
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::string FaceAuthenticator::buildScopedTrackId(const std::string& cameraUid,
                                                  const std::string& trackId) {
    return cameraUid + "::" + trackId;
}

std::string FaceAuthenticator::extractCameraUid(const std::string& scopedTrackId) {
    const auto pos = scopedTrackId.find("::");
    if (pos == std::string::npos) {
        return scopedTrackId;
    }
    return scopedTrackId.substr(0, pos);
}

} // namespace face
