//
// Created by hedieh on 12/23/23.
//

#include "modules/detection/CFaceProcess.h"
#include <cmath>

face::CFaceProcess::CFaceProcess(
    const std::shared_ptr<FaceConfig>& config,
    std::map<std::string, std::shared_ptr<rt::tensorrt::Engine>>& tensorrtEngines)
    : mDetector(config, tensorrtEngines), mRecognizer(config, tensorrtEngines) {}

std::vector<face::SFace> face::CFaceProcess::GetBoxes(cv::Mat& frame) {
    return mDetector.Inference(frame);
}

face::FeatureArray face::CFaceProcess::GetEmbeddings(const SFace& detection, cv::Mat& frame) {
    return NormalizeEmbedding(mRecognizer.ExtractEmbeddings(detection, frame));
}

face::FeatureArray face::CFaceProcess::NormalizeEmbedding(const FeatureArray& embedding) {
    double sum = 0.0;
    for (float value : embedding) {
        sum += static_cast<double>(value) * static_cast<double>(value);
    }

    if (sum <= 0.0) {
        return embedding;
    }

    const float inv_norm = 1.0F / static_cast<float>(std::sqrt(sum));
    FeatureArray normalized{};
    for (size_t i = 0; i < embedding.size(); ++i) {
        normalized[i] = embedding[i] * inv_norm;
    }
    return normalized;
}

face::CFaceProcess::~CFaceProcess() {}

void face::CFaceProcess::SetDetectionConfidence(float thr) {
    mDetector.ConfidenceSet(thr);
}
