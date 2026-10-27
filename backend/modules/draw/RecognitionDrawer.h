#ifndef FACE_CPP_RECOGNITIONDRAWER_H
#define FACE_CPP_RECOGNITIONDRAWER_H

#include "IFrameDrawer.h"

namespace face {

class RecognitionDrawer : public IFrameDrawer {
public:
    void draw(cv::Mat& frame, const std::vector<SFace>& detections) const override;
};

}  // namespace face

#endif  // FACE_CPP_RECOGNITIONDRAWER_H
