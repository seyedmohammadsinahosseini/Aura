#include "DetectionDrawer.h"

namespace face {

void DetectionDrawer::draw(cv::Mat& frame, const std::vector<SFace>& detections) const {
    for (const auto& detection : detections) {
        const cv::Scalar color(0, 255, 0);
        cv::rectangle(frame, detection.mBbox, color, 3);
    }
}

} // namespace face
