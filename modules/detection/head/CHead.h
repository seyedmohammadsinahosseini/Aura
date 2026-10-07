#ifndef COUNTING_CPP_CHEAD_H
#define COUNTING_CPP_CHEAD_H

#include <fstream>
#include <string>
#include <random>
#include <optional>

#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <any>
#include "core/config/Config.h"
#include <tensorrt_runtime/include/tensorrt_runtime.h>

namespace rt = xbox::base_component::ai::runtime_handler;

namespace face {
struct SBox {
    float x1, y1, x2, y2;
    float width, height;
    int classId;
    float confidence;
};

class CHead {
private:
    //        std::shared_ptr<IDeepLearning> mNet;
    cv::Size2i mModelShape = cv::Size(640, 640);
    std::vector<std::string> mClasses{"head"};
    float mModelConfidenceThreshold{0.3};
    float mModelNMSThreshold{0.50};
    bool mLetterBoxForSquare = true;
    std::unique_ptr<rt::tensorrt::TensorRTRuntime> mRuntime;
    static cv::Mat FormatToSquare(const cv::Mat& source);

public:
    CHead(const std::shared_ptr<face::FaceConfig>& config,
          std::map<std::string, std::shared_ptr<rt::tensorrt::Engine>>& tensorrtEngines);

    std::any Inference(const cv::Mat& input);

    ~CHead();

    void setConfidence(const float& confidence);
};

} // namespace face
#endif //COUNTING_CPP_CHEAD_H
