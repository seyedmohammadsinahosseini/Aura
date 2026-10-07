//
// Created by hashemi on 12/23/23.
//

#ifndef FACE_CPP_CFACEPROCESS_H
#define FACE_CPP_CFACEPROCESS_H

#include "modules/detection/CDetection.h"
#include "modules/recognition/CRecognition.h"
#include "core/config/Config.h"

#include <opencv2/opencv.hpp>

namespace face {
class CFaceProcess {
private:
    CRecognition mRecognizer;

    CDetection mDetector;

public:
    CFaceProcess(const std::shared_ptr<FaceConfig>& config,
                 std::map<std::string, std::shared_ptr<rt::tensorrt::Engine>>& tensorrtEngines);

    std::vector<SFace> GetBoxes(cv::Mat& frame);
    void SetDetectionConfidence(float thr);
    face::FeatureArray GetEmbeddings(const SFace& detection, cv::Mat& frame);
    static FeatureArray NormalizeEmbedding(const FeatureArray& embedding);

    ~CFaceProcess();
};
} // namespace face

#endif //FACE_CPP_CFACEPROCESS_H
