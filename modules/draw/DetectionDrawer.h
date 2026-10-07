#ifndef FACE_CPP_DETECTIONDRAWER_H
#define FACE_CPP_DETECTIONDRAWER_H

#include "IFrameDrawer.h"

namespace face {

class DetectionDrawer : public IFrameDrawer {
public:
    void draw(cv::Mat& frame, const std::vector<SFace>& detections) const override;
};

} // namespace face

#endif // FACE_CPP_DETECTIONDRAWER_H
