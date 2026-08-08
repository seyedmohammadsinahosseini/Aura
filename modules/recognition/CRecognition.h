//
// Created by hedieh on 12/23/23.
//

#ifndef FACE_CPP_CRECOGNITION_H
#define FACE_CPP_CRECOGNITION_H

#include "core/config/Config.h"
#include "core/SFace.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <optional>
#include <tensorrt_runtime/include/tensorrt_runtime.h>

namespace rt = xbox::base_component::ai::runtime_handler;

namespace face {
class CRecognition {
 private:
//        std::shared_ptr<IDeepLearning> mNet;
    std::unique_ptr<rt::tensorrt::TensorRTRuntime> mRuntime;

    cv::Size mModelShape = cv::Size(112, 112);
    cv::Mat mArcFaceSrc;

    static cv::Mat Vector2matConvertor(std::vector<cv::Point> data);

    static std::array<float, DIM> EmbedExtractor(cv::Mat embed);

 public:
    explicit CRecognition(const std::shared_ptr<FaceConfig>& config,
                          std::map<std::string, std::shared_ptr<rt::tensorrt::Engine>> &tensorrtEngines);

    FeatureArray ExtractEmbeddings(const SFace &face, cv::Mat &image);

    ~CRecognition();

};
}
#endif //FACE_CPP_CRECOGNITION_H
