#ifndef FACE_CPP_PIPELINE_FACEPIPELINE_H
#define FACE_CPP_PIPELINE_FACEPIPELINE_H

#include "modules/detection/CFaceProcess.h"
#include "modules/tracking/CFar.h"
#include "modules/tracking/CMotion.h"
#include "modules/tracking/CProfile.h"
#include "core/config/Config.h"
#include "modules/detection/head/CHead.h"
#include "modules/tracking/CTrack.h"
#include <atomic>

#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace face {

class FacePipeline {
public:
    struct ProcessFrameResult {
        std::vector<SFace> detections{};
        std::vector<std::string> removedTrackIds{};
    };

    explicit FacePipeline(const std::shared_ptr<FaceConfig>& config);
    ~FacePipeline();

    void setDetectionEnabled(bool enabled);
    void setTrackingEnabled(bool enabled);
    void setRecognitionEnabled(bool enabled);
    void setLivenessEnabled(bool enabled);
    void setMotionShortCircuitEnabled(bool enabled);
    void updateMotionParameters(int motionThreshold, int motionBuffer, int motionDiffThreshold,
                                int blurKernel, int dilateKernel);
    void setQualityGateEnabled(bool enabled);
    void setUseBlurGate(bool enabled);
    void setBlurThreshold(float threshold);
    void setUseSideProfileGate(bool enabled);
    void setUseFarDistanceGate(bool enabled);
    void setFarThresholds(float heightThreshold, float widthThreshold);

    ProcessFrameResult ProcessFrame(cv::Mat& frame);
    FeatureArray extractEmbeddingSync(const SFace& detection, cv::Mat& frame);
    std::vector<SBox> DetectHeads(const cv::Mat& frame);
    void clearAllRecognitionCache();

private:
    struct SEmbeddingTask {
        std::string track_id;
        SFace detection;
        cv::Mat frame;
    };

    std::shared_ptr<FaceConfig> config_;
    std::map<std::string, std::shared_ptr<rt::tensorrt::Engine>> engines_{};

    CFaceProcess face_process_;
    CHead head_detector_;
    CTracker tracker_;
    CFar far_analyzer_;
    CMotion motion_detector_;
    bool motion_short_circuit_enabled_;
    CProfile profile_analyzer_;

    std::unordered_map<std::string, bool> embedding_extracted_track_ids_{};
    std::unordered_map<std::string, FeatureArray> embedding_cache_{};
    std::unordered_set<std::string> active_track_ids_{};

    std::mutex queue_mutex_{};
    std::condition_variable queue_cv_{};
    std::deque<SEmbeddingTask> embedding_queue_{};
    std::thread embedding_thread_{};
    bool stop_embedding_thread_{false};

    float head_face_iou_threshold_{0.2F};
    int embedding_queue_max_size_{128};
    bool quality_gate_enabled_{false};
    bool use_blur_gate_{true};
    float blur_threshold_{80.0F};
    bool use_side_profile_gate_{true};
    bool use_far_distance_gate_{true};
    std::atomic<bool> detection_enabled_{true};
    std::atomic<bool> tracking_enabled_{true};
    std::atomic<bool> recognition_enabled_{true};
    std::atomic<bool> liveness_enabled_{true};

    void EmbeddingWorker();
    void EnqueueEmbeddingTask(const SFace& detection, const cv::Mat& frame);
    void CleanupTrack(const std::string& trackId);
    static float ComputeBlurScore(const cv::Mat& frame, const cv::Rect& bbox);
    static float IoU(const cv::Rect& a, const cv::Rect& b);
};

} // namespace face

#endif // FACE_CPP_PIPELINE_FACEPIPELINE_H
