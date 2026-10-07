//
// Created by hedieh on 12/23/23.
//

#include "CRecognition.h"
#include "core/utils.h"
#include <float_image_tensor/include/float_image_tensor.h>

face::CRecognition::CRecognition(
    const std::shared_ptr<FaceConfig>& config,
    std::map<std::string, std::shared_ptr<rt::tensorrt::Engine>>& tensorrtEngines) {
    using namespace rt;
    tensorrt::Logger logger(config->LogProvider(), config->LogCallback());
    const std::string& enginePath = config->RecognitionModelEngine();
    const std::string& onnxPath = config->RecognitionModelOnnx();

    if (enginePath.empty())
        throw std::runtime_error("Engine path cannot be empty");

    if (!tensorrtEngines.contains(enginePath)) {
        if (std::filesystem::exists(enginePath))
            tensorrtEngines[enginePath] =
                std::make_shared<tensorrt::Engine>(logger, enginePath.data());
        else {
            auto filename = WriteOnnxFile(onnxPath);
            nvinfer1::Dims4 minShape{1, 3, mModelShape.height, mModelShape.width};
            nvinfer1::Dims4 optShape{1, 3, mModelShape.height, mModelShape.width};
            nvinfer1::Dims4 maxShape{1, 3, mModelShape.height, mModelShape.width};

            std::size_t poolSize = 1ULL << 30; // 1GB GPU Memory
            std::string modelInputName = "input.1";
            tensorrtEngines[enginePath] = std::make_shared<tensorrt::Engine>(
                logger, filename, modelInputName, minShape, optShape, maxShape, poolSize);
            tensorrtEngines[enginePath]->SaveEngineToFile(enginePath);
        }
    }

    mRuntime = std::make_unique<tensorrt::TensorRTRuntime>(tensorrtEngines[enginePath], logger);

    mArcFaceSrc = cv::Mat::zeros(5, 2, CV_32F);
    float data_m[10] = {38.2946, 51.6963, 73.5318, 51.5014, 56.0252,
                        71.7366, 41.5493, 92.3655, 70.7299, 92.2041};
    int m = 0;
    for (int i = 0; i < 10; i++) {
        if (i % 2 == 0)
            mArcFaceSrc.ptr<float>(m)[0] = data_m[i];
        else {
            mArcFaceSrc.ptr<float>(m)[1] = data_m[i];
            m++;
        }
    }
}

cv::Mat face::CRecognition::Vector2matConvertor(std::vector<cv::Point> data) {
    cv::Mat mat_data = cv::Mat::zeros(5, 2, CV_32F);
    for (int i = 0; i < 5; i++) {
        mat_data.ptr<float>(i)[0] = float(data[i].x);
        mat_data.ptr<float>(i)[1] = float(data[i].y);
    }
    return mat_data;
}

face::FeatureArray face::CRecognition::EmbedExtractor(cv::Mat embed) {
    FeatureArray data{};
    for (int i = 0; i < DIM; i++)
        data[i] = embed.ptr<float>(0)[i];
    return data;
}

face::FeatureArray face::CRecognition::ExtractEmbeddings(const SFace& face, cv::Mat& image) {
    cv::Mat key_p = Vector2matConvertor(face.mKeypoint);
    cv::Mat out = cv::estimateRigidTransform(key_p, mArcFaceSrc, false); /* important false */
    //    cv::Mat out = cv::estimateAffine2D(key_p, arcface_src);

    cv::Mat out_img;
    cv::warpAffine(image, out_img, out, mModelShape);
    rt::tensorrt::FloatImageTensor input_tensor(
        std::vector<cv::Mat>{out_img}, mRuntime->GetInputShape(), true, 1.f / 128.f,
        -127.5f / 128.f); //TODO: change to a batch of images

    mRuntime->Inference(input_tensor);
    const auto& output_tensors = mRuntime->OutputTensors();

    std::vector<cv::Mat> outputs;
    for (auto& output : output_tensors) {
        const auto& shape = output->Shape();

        cv::Mat outputMat;
        if (shape.nbDims == 2) {
            size_t height = shape.d[0];
            size_t width = shape.d[1];
            outputMat = cv::Mat(height, width, CV_32F, output->MutableData());
        } else if (shape.nbDims == 3) {
            int height = shape.d[1];
            int width = shape.d[2];
            int size[3] = {(int)shape.d[0], height, width};
            outputMat = cv::Mat(shape.nbDims, size, CV_32F, output->MutableData());
        } else if (shape.nbDims == 4) {
            int height = shape.d[2];
            int width = shape.d[3];
            int size[3] = {(int)shape.d[1], height, width};
            outputMat = cv::Mat(shape.nbDims - 1, size, CV_32F, output->MutableData());
        }
        outputs.emplace_back(outputMat);
    }
    //    std::vector<cv::Mat> output;
    //    mNet->Inference(blob, output);
    return CRecognition::EmbedExtractor(outputs[0]);
}

face::CRecognition::~CRecognition() {
    //    mNet.reset();
}
