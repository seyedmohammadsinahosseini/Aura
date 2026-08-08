#include "FacePipeline.h"

#include <iostream>

namespace face {

FacePipeline::FacePipeline(const std::shared_ptr<FaceConfig>& config)
    : config_(config),
      face_process_(config_, engines_),
      head_detector_(config_, engines_),
      tracker_(*config_),
    motion_detector_(*config_),
    motion_short_circuit_enabled_(false) {
    far_analyzer_.mFarHeight = config_->FarHeightThreshold();
    far_analyzer_.mFarWidth = config_->FarWidthThreshold();
    head_face_iou_threshold_ = config_->HeadFaceIouThreshold();
    embedding_queue_max_size_ = config_->EmbeddingQueueMaxSize();
    quality_gate_enabled_ = config_->GetQualityGateEnabled();
    use_blur_gate_ = config_->GetUseBlurGate();
    blur_threshold_ = config_->GetBlurThreshold();
    use_side_profile_gate_ = config_->GetUseSideProfileGate();
    use_far_distance_gate_ = config_->GetUseFarDistanceGate();
    embedding_thread_ = std::thread(&FacePipeline::EmbeddingWorker, this);
}

FacePipeline::~FacePipeline() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_embedding_thread_ = true;
    }
    queue_cv_.notify_all();
    if (embedding_thread_.joinable()) {
        embedding_thread_.join();
    }
}

FacePipeline::ProcessFrameResult FacePipeline::ProcessFrame(cv::Mat& frame) {
    if (frame.empty()) {
        std::cout << "ProcessFrame called with empty frame" << std::endl;
        return {};
    }

    if (!detection_enabled_.load()) {
        return {};
    }

        if (motion_short_circuit_enabled_) {
            if (!motion_detector_.PrepareMotionResult(frame)) {
                ProcessFrameResult empty;
                empty.detections = {};
                empty.removedTrackIds = {};
                return empty;
            }
        }

    auto detections = face_process_.GetBoxes(frame);
    auto headDetections = DetectHeads(frame);

    for (const auto& head : headDetections) {
        cv::Rect headBox(static_cast<int>(head.x1),
                         static_cast<int>(head.y1),
                         static_cast<int>(head.width),
                         static_cast<int>(head.height));
        bool overlap = false;
        for (const auto& faceDet : detections) {
            if (IoU(faceDet.mBbox, headBox) > head_face_iou_threshold_) {
                overlap = true;
                break;
            }
        }
        if (!overlap) {
            SFace headOnly;
            headOnly.face = false;
            headOnly.mBbox = headBox;
            headOnly.mConfidence = head.confidence;
            detections.push_back(std::move(headOnly));
        }
    }

    std::vector<std::shared_ptr<byte_track::STrack>> removedTargets;
    if (tracking_enabled_.load()) {
        removedTargets = tracker_.Track(detections);
    }

    const std::pair<int, int> frameInfo{frame.rows, frame.cols};
    for (auto& detection : detections) {
        if (tracking_enabled_.load() && !detection.mTrackID.empty()) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            active_track_ids_.insert(detection.mTrackID);
        } else if (!tracking_enabled_.load()) {
            detection.mTrackID.clear();
        }

        if (detection.face && detection.mKeypoint.size() >= 5) {
            detection.mSideProfileView = profile_analyzer_.CheckSideProfile(detection.mKeypoint);
        }
        detection.mFarDistance = far_analyzer_.CheckSize(frameInfo, detection.mBbox);

        if (!detection.face) {
            continue;
        }

        bool alreadyRecognized = false;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            const auto it = embedding_cache_.find(detection.mTrackID);
            if (it != embedding_cache_.end()) {
                detection.mEmbedding = it->second;
                detection.mHasEmbedding = true;
            }
            alreadyRecognized =
                embedding_extracted_track_ids_.contains(detection.mTrackID) && embedding_extracted_track_ids_[detection.mTrackID];
        }

        detection.mBlurScore = -1.0F;
        detection.mBlurry = false;
        if (quality_gate_enabled_ && use_blur_gate_) {
            detection.mBlurScore = ComputeBlurScore(frame, detection.mBbox);
            detection.mBlurry = (detection.mBlurScore < blur_threshold_);
        }

        bool passesGate = true;
        if (quality_gate_enabled_) {
            if (use_side_profile_gate_ && detection.mSideProfileView) passesGate = false;
            if (use_far_distance_gate_ && detection.mFarDistance)     passesGate = false;
            if (use_blur_gate_ && detection.mBlurry)                  passesGate = false;
        }

        if (recognition_enabled_.load() && !alreadyRecognized && passesGate) {
            EnqueueEmbeddingTask(detection, frame);
        }
    }

    ProcessFrameResult result;
    result.detections = std::move(detections);
    for (const auto& target : removedTargets) {
        const auto trackId = target->getTrackId();
        result.removedTrackIds.push_back(trackId);
        CleanupTrack(trackId);
    }
    return result;
}

std::vector<SBox> FacePipeline::DetectHeads(const cv::Mat& frame) {
    const std::any output = head_detector_.Inference(frame);
    try {
        return std::any_cast<std::vector<SBox>>(output);
    } catch (const std::bad_any_cast&) {
        std::cout << "Head detection returned invalid output type" << std::endl;
        return {};
    }
}

FeatureArray FacePipeline::extractEmbeddingSync(const SFace& detection, cv::Mat& frame) {
    if (!detection.face || detection.mKeypoint.size() < 5) {
        return {};
    }
    return face_process_.GetEmbeddings(detection, frame);
}

void FacePipeline::EmbeddingWorker() {
    while (true) {
        SEmbeddingTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return stop_embedding_thread_ || !embedding_queue_.empty();
            });
            if (stop_embedding_thread_ && embedding_queue_.empty()) {
                return;
            }
            task = std::move(embedding_queue_.front());
            embedding_queue_.pop_front();
        }

        try {
            if (!recognition_enabled_.load()) {
                continue;
            }
            const auto embedding = face_process_.GetEmbeddings(task.detection, task.frame);
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (!active_track_ids_.contains(task.track_id)) {
                continue;
            }
            embedding_cache_[task.track_id] = embedding;
            embedding_extracted_track_ids_[task.track_id] = true;
        } catch (const std::exception& exception) {
            std::cout << "Embedding worker error: " << exception.what() << std::endl;
        }
    }
}

void FacePipeline::setDetectionEnabled(bool enabled) {
    detection_enabled_.store(enabled);
}

void FacePipeline::setTrackingEnabled(bool enabled) {
    tracking_enabled_.store(enabled);
}

void FacePipeline::setRecognitionEnabled(bool enabled) {
    recognition_enabled_.store(enabled);
}

void FacePipeline::setLivenessEnabled(bool enabled) {
    liveness_enabled_.store(enabled);
}

void FacePipeline::setMotionShortCircuitEnabled(bool enabled) {
    motion_short_circuit_enabled_ = enabled;
}

void FacePipeline::updateMotionParameters(int motionThreshold, int motionBuffer, int motionDiffThreshold, int blurKernel, int dilateKernel) {
    motion_detector_.updateConfig(motionThreshold, motionBuffer, motionDiffThreshold, blurKernel, dilateKernel);
}

void FacePipeline::setQualityGateEnabled(bool enabled) { quality_gate_enabled_ = enabled; }
void FacePipeline::setUseBlurGate(bool enabled) { use_blur_gate_ = enabled; }
void FacePipeline::setBlurThreshold(float threshold) { blur_threshold_ = threshold; }
void FacePipeline::setUseSideProfileGate(bool enabled) { use_side_profile_gate_ = enabled; }
void FacePipeline::setUseFarDistanceGate(bool enabled) { use_far_distance_gate_ = enabled; }
void FacePipeline::setFarThresholds(float heightThreshold, float widthThreshold) {
    far_analyzer_.mFarHeight = heightThreshold;
    far_analyzer_.mFarWidth = widthThreshold;
}

void FacePipeline::EnqueueEmbeddingTask(const SFace& detection, const cv::Mat& frame) {
    if (detection.mTrackID.empty() || !recognition_enabled_.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (embedding_extracted_track_ids_.count(detection.mTrackID) &&
        embedding_extracted_track_ids_[detection.mTrackID]) {
        return;
    }

    for (const auto& task : embedding_queue_) {
        if (task.track_id == detection.mTrackID) {
            return;
        }
    }

    if (static_cast<int>(embedding_queue_.size()) >= embedding_queue_max_size_) {
        std::cerr << "[EmbeddingQueue] FULL! dropping track=" << detection.mTrackID << std::endl;
        return;
    }

    SEmbeddingTask task;
    task.track_id = detection.mTrackID;
    task.detection = detection;
    task.frame = frame.clone();
    embedding_queue_.push_back(std::move(task));
    queue_cv_.notify_one();
}

void FacePipeline::CleanupTrack(const std::string& trackId) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    active_track_ids_.erase(trackId);
    embedding_extracted_track_ids_.erase(trackId);
    embedding_cache_.erase(trackId);
}

void FacePipeline::clearAllRecognitionCache() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    embedding_extracted_track_ids_.clear();
    embedding_cache_.clear();
}

float FacePipeline::ComputeBlurScore(const cv::Mat& frame, const cv::Rect& bbox) {
    cv::Rect safe = bbox & cv::Rect(0, 0, frame.cols, frame.rows);
    if (safe.area() <= 0) return 0.0F;
    cv::Mat roi = frame(safe);
    cv::Mat gray, laplacian;
    if (roi.channels() == 3)
        cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
    else
        gray = roi;
    cv::Laplacian(gray, laplacian, CV_64F);
    cv::Scalar mean, stddev;
    cv::meanStdDev(laplacian, mean, stddev);
    return static_cast<float>(stddev[0] * stddev[0]);
}

float FacePipeline::IoU(const cv::Rect& a, const cv::Rect& b) {
    const cv::Rect inter = a & b;
    if (inter.area() <= 0) {
        return 0.0F;
    }
    const float unionArea = static_cast<float>(a.area() + b.area() - inter.area());
    if (unionArea <= 0.0F) {
        return 0.0F;
    }
    return static_cast<float>(inter.area()) / unionArea;
}

}  // namespace face
