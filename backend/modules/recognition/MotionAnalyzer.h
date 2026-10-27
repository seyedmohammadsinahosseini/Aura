#ifndef FACE_CPP_MOTIONANALYZER_H
#define FACE_CPP_MOTIONANALYZER_H

#include "RecognitionTypes.h"

#include <opencv2/opencv.hpp>

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace face {

class MotionAnalyzer {
public:
    explicit MotionAnalyzer(const MotionConfig& config = {});

    void update(const std::string& trackId, const cv::Rect& bbox);
    Direction getDirection(const std::string& trackId) const;
    void removeTrack(const std::string& trackId);
    void updateConfig(const MotionConfig& config);

private:
    MotionConfig config_{};
    mutable std::mutex mutex_{};
    std::unordered_map<std::string, std::deque<cv::Point2f>> history_{};
};

}  // namespace face

#endif  // FACE_CPP_MOTIONANALYZER_H
