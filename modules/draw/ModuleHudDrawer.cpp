#include "ModuleHudDrawer.h"

namespace face {

static constexpr int kHudMargin = 12;
static constexpr int kHudLineHeight = 22;
static constexpr int kHudPadding = 10;
static constexpr double kHudFontScale = 0.5;
static constexpr int kHudThickness = 2;

static const cv::Scalar kGreen(0, 200, 0);
static const cv::Scalar kRed(0, 0, 200);
static const cv::Scalar kWhite(255, 255, 255);
static const cv::Scalar kBg(0, 0, 0, 180);

ModuleHudDrawer::ModuleHudDrawer(const ModuleHudConfig& config)
    : config_(config) {}

void ModuleHudDrawer::draw(cv::Mat& frame) const {
    struct HudEntry {
        std::string label;
        bool enabled;
    };

    const HudEntry entries[] = {
        {"DETECTION", config_.detectionEnabled},
        {"TRACKING",  config_.trackingEnabled},
        {"RECOGNITION", config_.recognitionEnabled},
        {"LIVENESS",  config_.livenessEnabled},
        {"QUALITY",   config_.qualityGateEnabled},
    };

    const int numEntries = sizeof(entries) / sizeof(entries[0]);
    const int boxHeight = numEntries * kHudLineHeight + kHudPadding * 2;
    const int boxWidth = 160;

    const int x0 = frame.cols - boxWidth - kHudMargin;
    const int y0 = kHudMargin;

    // Semi-transparent background
    cv::Rect bgRect(x0, y0, boxWidth, boxHeight);
    cv::Mat overlay = frame.clone();
    cv::rectangle(overlay, bgRect, cv::Scalar(0, 0, 0), cv::FILLED);
    cv::addWeighted(overlay, 0.5, frame, 0.5, 0, frame);
    cv::rectangle(frame, bgRect, cv::Scalar(60, 60, 60), 1);

    // Title
    cv::putText(frame, "MODULES",
                cv::Point(x0 + kHudPadding, y0 + kHudPadding + 14),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, kWhite, 1);

    // Module entries
    for (int i = 0; i < numEntries; ++i) {
        const int y = y0 + kHudPadding + 24 + i * kHudLineHeight;
        const auto& entry = entries[i];

        // Status indicator circle
        cv::circle(frame,
                   cv::Point(x0 + kHudPadding + 6, y - 4),
                   4, entry.enabled ? kGreen : kRed, cv::FILLED);

        // Module name
        cv::putText(frame, entry.label,
                    cv::Point(x0 + kHudPadding + 18, y),
                    cv::FONT_HERSHEY_SIMPLEX, kHudFontScale, kWhite, 1);

        // ON/OFF text
        cv::putText(frame, entry.enabled ? "ON" : "OFF",
                    cv::Point(x0 + boxWidth - kHudPadding - 30, y),
                    cv::FONT_HERSHEY_SIMPLEX, kHudFontScale,
                    entry.enabled ? kGreen : kRed, kHudThickness);
    }
}

} // namespace face
