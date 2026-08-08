#ifndef FACE_CPP_MODULEHUDDRAWER_H
#define FACE_CPP_MODULEHUDDRAWER_H

#include <opencv2/opencv.hpp>
#include <string>
#include <unordered_map>

namespace face {

struct ModuleHudConfig {
    bool detectionEnabled = true;
    bool trackingEnabled = true;
    bool recognitionEnabled = true;
    bool livenessEnabled = true;
    bool qualityGateEnabled = true;
};

class ModuleHudDrawer {
public:
    explicit ModuleHudDrawer(const ModuleHudConfig& config);
    void draw(cv::Mat& frame) const;

private:
    ModuleHudConfig config_;
};

} // namespace face

#endif // FACE_CPP_MODULEHUDDRAWER_H
