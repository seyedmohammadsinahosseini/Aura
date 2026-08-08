//
// Created by hashemi on 12/23/23.
//

#ifndef FACE_CPP_CDETECTION_H
#define FACE_CPP_CDETECTION_H

#include "core/config/Config.h"

#include <opencv2/opencv.hpp>
#include <vector>
#include <optional>

#include "core/SFace.h"
#include <tensorrt_runtime/include/tensorrt_runtime.h>

namespace rt = xbox::base_component::ai::runtime_handler;

namespace face {
    class CDetection {
    private:
        cv::Size mModelShape = cv::Size(640, 640);
        float mDetectScale{};
        float mConfidenceThreshold = 0.4;
        float mIOUThreshold = 0.4;
        int mStride[3] = {8, 16, 32};
        std::mutex mMutex{};
        std::vector<int> mKeepData;
        std::unique_ptr<rt::tensorrt::TensorRTRuntime> mRuntime;

        static cv::Mat Grid(int height, int width);

        static std::vector<cv::Point> RepeatVector(const std::vector<cv::Point> &vec1);

        std::vector<face::SFace>
        Mat2vecConvertor(const cv::Mat &box, const cv::Mat &scores, const cv::Mat &k_point,
                         const std::vector<cv::Point> &keypointVector) const;

        cv::Mat PrepareImage(const cv::Mat& image);

        std::vector<face::SFace> Forward(const cv::Mat& model_input);

    public:
        CDetection(const std::shared_ptr<FaceConfig>& config,
                   std::map<std::string, std::shared_ptr<rt::tensorrt::Engine>>& tensorrtEngines);

        std::vector<SFace> Inference(cv::Mat &frame);

        ~CDetection();

        void ConfidenceSet(float thr);
    };
}
#endif //FACE_CPP_CDETECTION_H
