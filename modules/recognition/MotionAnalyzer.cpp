#include "MotionAnalyzer.h"

#include <cmath>

namespace face {

MotionAnalyzer::MotionAnalyzer(const MotionConfig& config) : config_(config) {}

void MotionAnalyzer::update(const std::string& trackId, const cv::Rect& bbox) {
    if (trackId.empty()) {
        return;
    }

    const cv::Point2f center(static_cast<float>(bbox.x) + static_cast<float>(bbox.width) * 0.5F,
                             static_cast<float>(bbox.y) + static_cast<float>(bbox.height) * 0.5F);

    std::lock_guard<std::mutex> lock(mutex_);
    auto& window = history_[trackId];
    window.push_back(center);
    while (static_cast<int>(window.size()) > config_.windowSize) {
        window.pop_front();
    }
}

Direction MotionAnalyzer::getDirection(const std::string& trackId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = history_.find(trackId);
    if (it == history_.end() || it->second.size() < 2) {
        return Direction::UNKNOWN;
    }

    float sumDelta = 0.0F;
    for (size_t i = 1; i < it->second.size(); ++i) {
        if (config_.useVertical) {
            sumDelta += it->second[i].y - it->second[i - 1].y;
        } else {
            sumDelta += it->second[i].x - it->second[i - 1].x;
        }
    }

    const float averageDelta = sumDelta / static_cast<float>(it->second.size() - 1);
    if (averageDelta < -config_.threshold) {
        return Direction::IN;
    }
    if (averageDelta > config_.threshold) {
        return Direction::OUT;
    }
    return Direction::UNKNOWN;
}

void MotionAnalyzer::removeTrack(const std::string& trackId) {
    std::lock_guard<std::mutex> lock(mutex_);
    history_.erase(trackId);
}

void MotionAnalyzer::updateConfig(const MotionConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    for (auto& [_, window] : history_) {
        while (static_cast<int>(window.size()) > config_.windowSize) {
            window.pop_front();
        }
    }
}

} // namespace face
