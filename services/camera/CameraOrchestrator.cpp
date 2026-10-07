#include "services/camera/CameraOrchestrator.h"
#include "modules/attendance/AttendanceManager.h"
#include "modules/recognition/Utils.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <ctime>

using json = nlohmann::json;

namespace backend {

CameraOrchestrator::CameraOrchestrator(std::shared_ptr<face::FaceConfig> config,
                                       face::FaceDatabase* db, CameraPreviewHub* previewHub,
                                       face::AttendanceManager* attendanceMgr)
    : config_(std::move(config)), db_(db), previewHub_(previewHub), attendanceMgr_(attendanceMgr) {
    faceSearcher_ = std::make_unique<face::FaceSearcher>();
    if (db_) {
        faceSearcher_->rebuildIndex(db_->getAllReferenceEmbeddings());
    }
}

CameraOrchestrator::~CameraOrchestrator() {
    stop();
}

void CameraOrchestrator::rebuildIndex() {
    if (db_ && faceSearcher_) {
        faceSearcher_->rebuildIndex(db_->getAllReferenceEmbeddings());
    }
    {
        std::lock_guard<std::mutex> lock(workersMutex_);
        for (auto& [uid, worker] : workers_) {
            if (worker && worker->pipeline) {
                worker->pipeline->clearAllRecognitionCache();
            }
        }
    }
}

bool CameraOrchestrator::start() {
    if (!db_)
        return false;
    auto cameras = db_->getAllCameras();
    for (const auto& cam : cameras) {
        if (shouldRunCamera(cam))
            startWorker(cam.uid, cam);
    }
    return true;
}

void CameraOrchestrator::stop() {
    std::lock_guard<std::mutex> lock(workersMutex_);
    for (auto& [uid, worker] : workers_) {
        if (!worker)
            continue;
        worker->running = false;
        if (worker->streamer)
            worker->streamer->stop();
        if (worker->workerThread.joinable())
            worker->workerThread.join();
    }
    workers_.clear();
    cameraManager_.closeAll();
}

void CameraOrchestrator::notifyCameraActiveChanged(const std::string& cameraUid, bool active) {
    if (!db_)
        return;
    if (!active) {
        stopWorker(cameraUid);
        return;
    }

    auto opt = db_->getCameraConfig(cameraUid);
    if (!opt.has_value())
        return;
    if (shouldRunCamera(*opt)) {
        startWorker(cameraUid, *opt);
    }
}

void CameraOrchestrator::notifyPreviewSubscriptionsChanged(const std::string& cameraUid) {
    if (!db_)
        return;

    const auto cameraConfig = db_->getCameraConfig(cameraUid);
    if (!cameraConfig.has_value())
        return;

    if (shouldRunCamera(*cameraConfig)) {
        startWorker(cameraUid, *cameraConfig);
    } else {
        stopWorker(cameraUid);
    }
}

bool CameraOrchestrator::shouldRunCamera(const face::CameraConfig& camConfig) const {
    if (!camConfig.streamEnabled) {
        return false;
    }

    const bool hasPreviewSubscribers =
        previewHub_ != nullptr && previewHub_->hasSubscribers(camConfig.uid);
    return camConfig.aiEnabled || hasPreviewSubscribers;
}

void CameraOrchestrator::startWorker(const std::string& cameraUid,
                                     const face::CameraConfig& camConfig) {
    std::lock_guard<std::mutex> lock(workersMutex_);
    if (workers_.count(cameraUid))
        return;

    camera::DeviceInfo info;
    info.devicePath = camConfig.uid;
    info.streamUri = camConfig.streamUri.empty() ? camConfig.uid : camConfig.streamUri;
    info.cardName = camConfig.name;

    // Auto-detect connection type from uid or streamUri if not explicitly set
    std::string connType = camConfig.connectionType;
    if (connType.empty() || connType == "RTSP") {
        if (camConfig.uid.rfind("/dev/", 0) == 0 || info.streamUri.rfind("/dev/", 0) == 0) {
            connType = "LOCAL";
        }
    }

    if (connType == "LOCAL") {
        info.type = camera::ConnectionType::LOCAL;
        // Extract V4L2 index from device path (e.g. /dev/video1 -> 1)
        const auto& path = info.devicePath;
        if (path.rfind("/dev/video", 0) == 0) {
            try {
                info.index = std::stoi(path.substr(10));
            } catch (...) {
            }
        }
    } else if (connType == "ONVIF") {
        info.type = camera::ConnectionType::ONVIF;
        info.xaddrs = camConfig.onvifXaddrs;
    } else {
        info.type = camera::ConnectionType::RTSP;
    }

    if (!camConfig.encoding.empty()) {
        camera::MediaProfileInfo profile;
        profile.encoding = camConfig.encoding;
        profile.streamUri = info.streamUri;
        profile.width = camConfig.preferredWidth;
        profile.height = camConfig.preferredHeight;
        profile.fps = camConfig.preferredFps;
        info.mediaProfiles.push_back(std::move(profile));
    }

    info.preferredFps = camConfig.preferredFps;
    info.preferredWidth = camConfig.preferredWidth;
    info.preferredHeight = camConfig.preferredHeight;
    info.transportProtocol =
        camConfig.transportProtocol.empty() ? "tcp" : camConfig.transportProtocol;

    auto streamer = cameraManager_.openCamera(info);
    if (!streamer)
        return;

    auto worker = std::make_unique<CameraWorker>();
    worker->streamer = streamer;
    worker->pipeline = std::make_unique<face::FacePipeline>(config_);
    applyPipelineConfig(cameraUid, worker->pipeline.get());
    worker->running = true;
    worker->workerThread =
        std::thread(&CameraOrchestrator::workerLoop, this, cameraUid, worker.get());

    workers_[cameraUid] = std::move(worker);
}

void CameraOrchestrator::applyPipelineConfig(const std::string& cameraUid,
                                             face::FacePipeline* pipeline) {
    if (!pipeline || !db_)
        return;
    try {
        auto maybeDet = db_->getCameraModuleConfig(cameraUid, std::string("detection"));
        if (maybeDet.has_value())
            pipeline->setDetectionEnabled(maybeDet->first);
        auto maybeTrack = db_->getCameraModuleConfig(cameraUid, std::string("tracking"));
        if (maybeTrack.has_value())
            pipeline->setTrackingEnabled(maybeTrack->first);
        auto maybeRec = db_->getCameraModuleConfig(cameraUid, std::string("recognition"));
        if (maybeRec.has_value())
            pipeline->setRecognitionEnabled(maybeRec->first);
        auto maybeLiv = db_->getCameraModuleConfig(cameraUid, std::string("liveness"));
        if (maybeLiv.has_value())
            pipeline->setLivenessEnabled(maybeLiv->first);
        auto maybeQg = db_->getCameraModuleConfig(cameraUid, std::string("quality_gate"));
        if (maybeQg.has_value()) {
            pipeline->setQualityGateEnabled(maybeQg->first);
            try {
                const json qg = json::parse(maybeQg->second);
                if (qg.contains("use_blur_gate"))
                    pipeline->setUseBlurGate(qg["use_blur_gate"].get<bool>());
                if (qg.contains("blur_threshold"))
                    pipeline->setBlurThreshold(qg["blur_threshold"].get<float>());
                if (qg.contains("use_side_profile_gate"))
                    pipeline->setUseSideProfileGate(qg["use_side_profile_gate"].get<bool>());
                if (qg.contains("use_far_distance_gate"))
                    pipeline->setUseFarDistanceGate(qg["use_far_distance_gate"].get<bool>());
                float fh = qg.value("far_height_threshold", 0.12F);
                float fw = qg.value("far_width_threshold", 0.12F);
                pipeline->setFarThresholds(fh, fw);
            } catch (...) {
            }
        }

        // Cache all module config params for real-time use
        std::lock_guard<std::mutex> lock(configMutex_);
        auto& params = cameraModuleConfigs_[cameraUid];
        params.clear();
        for (const auto& moduleName :
             {"detection", "tracking", "recognition", "liveness", "quality_gate", "attendance"}) {
            const auto cfg = db_->getCameraModuleConfig(cameraUid, moduleName);
            if (cfg.has_value()) {
                params[std::string(moduleName) + "_enabled"] = cfg->first ? "1" : "0";
                // Parse config_json for individual parameters
                try {
                    const json j = json::parse(cfg->second);
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        if (it.value().is_string())
                            params[it.key()] = it.value().get<std::string>();
                        else
                            params[it.key()] = it.value().dump();
                    }
                } catch (...) {
                }
            }
        }
    } catch (...) {
    }
}

void CameraOrchestrator::notifyCameraConfigChanged(const std::string& cameraUid) {
    if (!db_)
        return;

    const auto cameraConfig = db_->getCameraConfig(cameraUid);
    if (!cameraConfig.has_value())
        return;

    if (!shouldRunCamera(*cameraConfig)) {
        stopWorker(cameraUid);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(workersMutex_);
        auto it = workers_.find(cameraUid);
        if (it == workers_.end()) {
            // Camera exists in DB and is enabled, but no worker is running yet.
            // Start it so initial add/configure works without requiring a manual toggle.
            // Lock is released before startWorker to avoid recursive mutex usage.
        } else {
            auto* worker = it->second.get();
            if (!worker || !worker->pipeline)
                return;
            applyPipelineConfig(cameraUid, worker->pipeline.get());
            return;
        }
    }

    startWorker(cameraUid, *cameraConfig);
}

void CameraOrchestrator::stopWorker(const std::string& cameraUid) {
    std::unique_ptr<CameraWorker> w;
    {
        std::lock_guard<std::mutex> lock(workersMutex_);
        auto it = workers_.find(cameraUid);
        if (it != workers_.end()) {
            w = std::move(it->second);
            workers_.erase(it);
        }
    }
    if (w) {
        w->running = false;
        if (w->streamer)
            w->streamer->stop();
        if (w->workerThread.joinable())
            w->workerThread.join();
    }

    if (previewHub_) {
        previewHub_->clearCamera(cameraUid);
    }
    cameraManager_.closeCamera(cameraUid);
}

void CameraOrchestrator::workerLoop(const std::string& cameraUid, CameraWorker* worker) {
    if (!worker)
        return;

    auto lastFpsTime = std::chrono::steady_clock::now();
    uint64_t lastFpsSeq = 0;
    uint64_t lastProcessedSeq = 0;
    float currentFps = 0.0f;

    while (worker->running) {
        cv::Mat frame;
        uint64_t curSeq = 0;

        if (!worker->streamer->getLatestFrame(frame, curSeq) || frame.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (curSeq == lastProcessedSeq) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        lastProcessedSeq = curSeq;

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<float>(now - lastFpsTime).count();

        if (elapsed >= 1.0f) {
            currentFps = static_cast<float>(curSeq - lastFpsSeq) / elapsed;
            lastFpsSeq = curSeq;
            lastFpsTime = now;
        }

        processFrame(cameraUid, std::move(frame), worker->pipeline.get(), worker->motionAnalyzer,
                     currentFps);
    }
}

void CameraOrchestrator::processFrame(const std::string& cameraUid, cv::Mat frame,
                                      face::FacePipeline* pipeline,
                                      face::MotionAnalyzer& motionAnalyzer, float fps) {
    if (!db_)
        return;
    try {
        const auto cameraConfig = db_->getCameraConfig(cameraUid);
        if (!cameraConfig.has_value() || !cameraConfig->streamEnabled) {
            return;
        }

        const bool previewRequested =
            previewHub_ != nullptr && previewHub_->hasSubscribers(cameraUid);
        const bool runAi = cameraConfig->aiEnabled && pipeline != nullptr;

        std::vector<face::SFace> detections;
        if (runAi) {
            auto res = pipeline->ProcessFrame(frame);
            detections = std::move(res.detections);
            matchDetections(cameraUid, detections, motionAnalyzer, cameraConfig->type);

            for (const auto& tid : res.removedTrackIds) {
                motionAnalyzer.removeTrack(tid);
            }

            persistDetections(cameraUid, detections, frame);
        }

        if (previewRequested && previewHub_ != nullptr) {
            cv::Mat previewFrame = frame.clone();
            if (runAi) {
                drawProcessedFrame(cameraUid, previewFrame, detections);
            }
            previewHub_->publishFrame(cameraUid, previewFrame, fps);
        }
    } catch (const std::exception& e) {
        // log but don't throw
    }
}

void CameraOrchestrator::matchDetections(const std::string& cameraUid,
                                         std::vector<face::SFace>& detections,
                                         face::MotionAnalyzer& motionAnalyzer,
                                         face::CameraType camType) {
    if (!db_ || !faceSearcher_ || faceSearcher_->size() == 0)
        return;

    float distanceThreshold = config_->Recognition().faissThreshold;
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        const auto camIt = cameraModuleConfigs_.find(cameraUid);
        if (camIt != cameraModuleConfigs_.end()) {
            const auto& params = camIt->second;
            const auto thIt = params.find("faiss_threshold");
            if (thIt != params.end()) {
                try {
                    distanceThreshold = std::stof(thIt->second);
                } catch (...) {
                }
            }
        }
    }

    const auto ts = static_cast<int64_t>(std::time(nullptr));

    for (auto& detection : detections) {
        if (!detection.face || !detection.mHasEmbedding)
            continue;

        motionAnalyzer.update(detection.mTrackID, detection.mBbox);

        const auto embedding =
            std::vector<float>(detection.mEmbedding.begin(), detection.mEmbedding.end());

        float distance = 1.0f;
        const int64_t personId = faceSearcher_->search(embedding, distance);

        if (personId >= 0 && distance <= distanceThreshold) {
            detection.mPersonId = personId;
            detection.mRecognitionConfidence = 1.0f - distance;
            detection.mDisplayName = lookupPersonName(personId);

            if (attendanceMgr_) {
                const auto direction = motionAnalyzer.getDirection(detection.mTrackID);
                switch (camType) {
                    case face::CameraType::ENTRY:
                        attendanceMgr_->markArrival(detection.mTrackID, personId, ts);
                        break;
                    case face::CameraType::EXIT:
                        attendanceMgr_->markDeparture(detection.mTrackID, personId, ts);
                        break;
                    case face::CameraType::BOTH:
                    default:
                        if (direction == face::Direction::IN) {
                            attendanceMgr_->markArrival(detection.mTrackID, personId, ts);
                        } else if (direction == face::Direction::OUT) {
                            attendanceMgr_->markDeparture(detection.mTrackID, personId, ts);
                        } else {
                            const auto status =
                                attendanceMgr_->getCurrentStatus(detection.mTrackID);
                            if (status == "IN") {
                                attendanceMgr_->markDeparture(detection.mTrackID, personId, ts);
                            } else {
                                attendanceMgr_->markArrival(detection.mTrackID, personId, ts);
                            }
                        }
                        break;
                }
            }
        } else {
            detection.mPersonId = -1;
            detection.mRecognitionConfidence = 0.0f;
            detection.mDisplayName = "Unknown";
        }
    }
}

std::string CameraOrchestrator::lookupPersonName(int64_t personId) const {
    if (personId < 0 || !db_)
        return "Unknown";
    face::Person person;
    if (!db_->getPerson(personId, person))
        return "Person#" + std::to_string(personId);
    if (!person.name.empty())
        return person.name;
    if (!person.code.empty())
        return person.code;
    return "Person#" + std::to_string(personId);
}

void CameraOrchestrator::drawProcessedFrame(const std::string& cameraUid, cv::Mat& frame,
                                            const std::vector<face::SFace>& detections) {
    face::ModuleHudConfig hudCfg;
    try {
        auto loadMod = [&](const std::string& name, bool defaultVal) -> bool {
            const auto cfg = db_->getCameraModuleConfig(cameraUid, name);
            return cfg.has_value() ? cfg->first : defaultVal;
        };
        hudCfg.detectionEnabled = loadMod("detection", true);
        hudCfg.trackingEnabled = loadMod("tracking", true);
        hudCfg.recognitionEnabled = loadMod("recognition", true);
        hudCfg.livenessEnabled = loadMod("liveness", true);
        hudCfg.qualityGateEnabled = loadMod("quality_gate", true);

        if (hudCfg.detectionEnabled) {
            face::DetectionDrawer{}.draw(frame, detections);
        }
        if (hudCfg.recognitionEnabled) {
            face::RecognitionDrawer{}.draw(frame, detections);
        }
    } catch (...) {
    }

    face::ModuleHudDrawer(hudCfg).draw(frame);
}

void CameraOrchestrator::persistDetections(const std::string& cameraUid,
                                           const std::vector<face::SFace>& detections,
                                           const cv::Mat& frame) {
    if (!db_)
        return;
    const std::string cropsDir = config_->Database().cropsDir;

    for (const auto& det : detections) {
        if (!det.mHasEmbedding || !det.face)
            continue;

        // Always log raw detection
        face::RawDetection rd;
        rd.personId = det.mPersonId;
        rd.trackId = det.mTrackID;
        rd.embedding.assign(det.mEmbedding.begin(), det.mEmbedding.end());
        rd.timestamp = static_cast<int64_t>(std::time(nullptr));
        rd.imagePath = "";
        rd.status = (det.mPersonId >= 0) ? "matched" : "new";
        rd.confidence = det.mRecognitionConfidence;
        try {
            db_->addRawDetection(rd);
        } catch (...) {
        }

        if (det.mPersonId >= 0) {
            // Successfully identified — remove from unknown_faces if present
            try {
                db_->deleteUnknownFaceByTrackId(det.mTrackID, cameraUid);
            } catch (...) {
            }
            best_blur_score_per_track_.erase(det.mTrackID);
        } else {
            // Unknown person
            const bool alreadyExists = db_->unknownFaceExists(det.mTrackID, cameraUid);

            if (!alreadyExists) {
                // First time seeing this trackId — save crop and add to unknown_faces
                face::ensureDirectoryExists(cropsDir);
                const auto ts = static_cast<int64_t>(std::time(nullptr));
                const std::string cropPath = face::buildCropPath(cropsDir, ts, -1, det.mTrackID);
                face::saveCroppedFace(frame, det.mBbox, cropPath);
                try {
                    db_->addUnknownFace(
                        std::vector<float>(det.mEmbedding.begin(), det.mEmbedding.end()),
                        det.mTrackID, cameraUid, ts, cropPath);
                } catch (...) {
                }
                best_blur_score_per_track_[det.mTrackID] = det.mBlurScore;
            } else {
                // Already exists — update embedding only if new frame is sharper
                float& best = best_blur_score_per_track_[det.mTrackID];
                if (det.mBlurScore > best) {
                    best = det.mBlurScore;
                    try {
                        db_->updateUnknownFaceEmbedding(
                            det.mTrackID, cameraUid,
                            std::vector<float>(det.mEmbedding.begin(), det.mEmbedding.end()));
                    } catch (...) {
                    }
                }
            }
        }
    }
}

} // namespace backend
