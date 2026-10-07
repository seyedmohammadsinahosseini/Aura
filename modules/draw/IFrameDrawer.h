#ifndef FACE_CPP_IFRAMEDRAWER_H
#define FACE_CPP_IFRAMEDRAWER_H

#include "core/SFace.h"

#include <opencv2/opencv.hpp>
#include <vector>

namespace face {

class IFrameDrawer {
public:
    virtual ~IFrameDrawer() = default;
    virtual void draw(cv::Mat& frame, const std::vector<SFace>& detections) const = 0;
};

} // namespace face

#endif // FACE_CPP_IFRAMEDRAWER_H
