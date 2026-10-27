#pragma once

#include "modules/camera/CameraManager.h"
#include "modules/draw/DetectionDrawer.h"
#include "modules/draw/RecognitionDrawer.h"
#include "modules/pipeline/FacePipeline.h"
#include "modules/recognition/FaceSearcher.h"
#include "modules/recognition/MotionAnalyzer.h"
#include "services/storage/FaceDatabase.h"
#include "services/stream/CameraPreviewHub.h"
#include <chrono>
#include <memory>
#include <thread>
#include <unordered_map>
#include <atomic>
#include <mutex>

namespace face { class AttendanceManager; }

namespace backend {

class CameraOrchestrator {
public:
    explicit CameraOrchestrator(std::shared_ptr<face::FaceConfig> config,
                                face::FaceDatabase* db,
                                CameraPreviewHub* previewHub = nullptr,
                                face::AttendanceManager* attendanceMgr = nullptr);
    ~CameraOrchestrator();

    bool start();
    void stop();
    void rebuildIndex();

    void notifyCameraActiveChanged(const std::string& cameraUid, bool active);
    void notifyCameraConfigChanged(const std::string& cameraUid);
    void notifyPreviewSubscriptionsChanged(const std::string& cameraUid);

private:
    struct CameraWorker {
        std::shared_ptr<camera::CameraStreamer> streamer;
        std::unique_ptr<face::FacePipeline> pipeline;
        face::MotionAnalyzer motionAnalyzer;
        std::thread workerThread;
        std::atomic<bool> running{false};
    };

    std::shared_ptr<face::FaceConfig> config_;
    face::FaceDatabase* db_ = nullptr;
    CameraPreviewHub* previewHub_ = nullptr;
    face::AttendanceManager* attendanceMgr_ = nullptr;
    camera::CameraManager cameraManager_;
    std::unordered_map<std::string, std::unique_ptr<CameraWorker>> workers_;
    std::mutex workersMutex_;

    void matchDetections(const std::string& cameraUid, std::vector<face::SFace>& detections,
                         face::MotionAnalyzer& motionAnalyzer, face::CameraType camType);
    std::string lookupPersonName(int64_t personId) const;

    bool shouldRunCamera(const face::CameraConfig& camConfig) const;
    void startWorker(const std::string& cameraUid, const face::CameraConfig& camConfig);
    void applyPipelineConfig(const std::string& cameraUid, face::FacePipeline* pipeline);
    void drawProcessedFrame(const std::string& cameraUid, cv::Mat& frame, const std::vector<face::SFace>& detections);
    void persistDetections(const std::string& cameraUid, const std::vector<face::SFace>& detections, const cv::Mat& frame);
    void stopWorker(const std::string& cameraUid);
    void workerLoop(const std::string& cameraUid, CameraWorker* worker);
    void processFrame(const std::string& cameraUid, cv::Mat frame,
                      face::FacePipeline* pipeline, face::MotionAnalyzer& motionAnalyzer,
                      float fps = 0.0f);

    std::unique_ptr<face::FaceSearcher> faceSearcher_;

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> cameraModuleConfigs_;
    std::mutex configMutex_;
    std::unordered_map<std::string, float> best_blur_score_per_track_;
};

} // namespace backend
