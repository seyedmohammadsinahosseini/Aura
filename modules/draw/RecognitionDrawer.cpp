#include "RecognitionDrawer.h"

#include <algorithm>

namespace face {

static constexpr double kFontScale = 0.9;
static constexpr int kThickness = 3;
static constexpr int kLineGap = 28;

static const cv::Scalar kGreen(0, 255, 0);
static const cv::Scalar kRed(0, 0, 255);

static cv::Point DrawText(cv::Mat& frame, cv::Point pos, const std::string& text,
                          const cv::Scalar& color) {
    cv::putText(frame, text, pos, cv::FONT_HERSHEY_SIMPLEX, kFontScale, color, kThickness);
    pos.x += cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, kFontScale, kThickness, nullptr).width;
    return pos;
}

void RecognitionDrawer::draw(cv::Mat& frame, const std::vector<SFace>& detections) const {
    for (const auto& detection : detections) {
        if (!detection.face)
            continue;

        const bool known = detection.mPersonId >= 0;
        const bool allOk =
            !detection.mBlurry && !detection.mSideProfileView && !detection.mFarDistance;

        const std::string name = detection.mDisplayName.empty()
            ? (known ? "Person#" + std::to_string(detection.mPersonId) : "Unknown")
            : detection.mDisplayName;

        int baseLine = 0;
        cv::Size nameSize =
            cv::getTextSize(name, cv::FONT_HERSHEY_SIMPLEX, kFontScale, kThickness, &baseLine);
        cv::Point pos(detection.mBbox.x, std::max(nameSize.height, detection.mBbox.y - 4));

        // Line 1: name + overall status
        pos = DrawText(frame, pos, name, kGreen);
        pos = DrawText(frame, pos, " [", allOk ? kGreen : kRed);
        pos = DrawText(frame, pos, allOk ? "PASS" : "FAIL", allOk ? kGreen : kRed);
        pos = DrawText(frame, pos, "]", allOk ? kGreen : kRed);

        // Line 2: per-parameter breakdown
        pos.x = detection.mBbox.x;
        pos.y += kLineGap;

        char buf[48];
        if (detection.mBlurScore < 0.0F)
            snprintf(buf, sizeof(buf), "BLUR:OFF");
        else
            snprintf(buf, sizeof(buf), "BLUR:%.0f", detection.mBlurScore);
        pos = DrawText(frame, pos, buf, detection.mBlurry ? kRed : kGreen);

        pos = DrawText(frame, pos, "  SIDE", detection.mSideProfileView ? kRed : kGreen);
        pos = DrawText(frame, pos, "  FAR", detection.mFarDistance ? kRed : kGreen);
    }
}

} // namespace face
