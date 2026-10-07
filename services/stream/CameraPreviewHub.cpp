#include "services/stream/CameraPreviewHub.h"

#include <ctime>

namespace backend {

void CameraPreviewHub::subscribe(const std::string& cameraUid) {
    if (cameraUid.empty())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    ++subscriberCounts_[cameraUid];
}

void CameraPreviewHub::unsubscribe(const std::string& cameraUid) {
    if (cameraUid.empty())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = subscriberCounts_.find(cameraUid);
    if (it == subscriberCounts_.end()) {
        return;
    }

    if (it->second <= 1) {
        subscriberCounts_.erase(it);
        latestFrames_.erase(cameraUid);
        return;
    }

    --it->second;
}

bool CameraPreviewHub::hasSubscribers(const std::string& cameraUid) const {
    return subscriberCount(cameraUid) > 0;
}

int CameraPreviewHub::subscriberCount(const std::string& cameraUid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = subscriberCounts_.find(cameraUid);
    if (it == subscriberCounts_.end()) {
        return 0;
    }
    return it->second;
}

void CameraPreviewHub::publishFrame(const std::string& cameraUid, const cv::Mat& frame, float fps) {
    if (cameraUid.empty() || frame.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!subscriberCounts_.contains(cameraUid)) {
        return;
    }

    auto& entry = latestFrames_[cameraUid];
    cv::imencode(".jpg", frame, entry.jpegBytes);
    const size_t bgrSize = frame.total() * frame.elemSize();
    entry.bgrBytes.resize(bgrSize);
    std::memcpy(entry.bgrBytes.data(), frame.data, bgrSize);
    entry.width = frame.cols;
    entry.height = frame.rows;
    entry.stride = static_cast<int>(frame.step);
    entry.timestamp = static_cast<int64_t>(std::time(nullptr));
    entry.fps = fps;
}

void CameraPreviewHub::clearCamera(const std::string& cameraUid) {
    std::lock_guard<std::mutex> lock(mutex_);
    latestFrames_.erase(cameraUid);
}

void CameraPreviewHub::removeCamera(const std::string& cameraUid) {
    std::lock_guard<std::mutex> lock(mutex_);
    latestFrames_.erase(cameraUid);
    subscriberCounts_.erase(cameraUid);
}

std::optional<CameraPreviewHub::EncodedJpegFrame>
CameraPreviewHub::getLatestJpeg(const std::string& cameraUid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = latestFrames_.find(cameraUid);
    if (it == latestFrames_.end() || it->second.jpegBytes.empty()) {
        return std::nullopt;
    }

    EncodedJpegFrame encoded;
    encoded.jpegBytes = it->second.jpegBytes;
    encoded.width = it->second.width;
    encoded.height = it->second.height;
    encoded.timestamp = it->second.timestamp;
    encoded.fps = it->second.fps;
    return encoded;
}

std::optional<CameraPreviewHub::RawBgrFrame>
CameraPreviewHub::getLatestBgr(const std::string& cameraUid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = latestFrames_.find(cameraUid);
    if (it == latestFrames_.end() || it->second.bgrBytes.empty()) {
        return std::nullopt;
    }

    RawBgrFrame raw;
    raw.bgrBytes = it->second.bgrBytes;
    raw.width = it->second.width;
    raw.height = it->second.height;
    raw.stride = it->second.stride;
    raw.timestamp = it->second.timestamp;
    raw.fps = it->second.fps;
    return raw;
}

} // namespace backend
