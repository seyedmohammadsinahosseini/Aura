#include "CDetection.h"
#include "core/utils.h"
#include <float_image_tensor/include/float_image_tensor.h>

face::CDetection::CDetection(
    const std::shared_ptr<FaceConfig>& config,
    std::map<std::string, std::shared_ptr<rt::tensorrt::Engine>>& tensorrtEngines) {
    using namespace rt;
    tensorrt::Logger logger(config->LogProvider(), config->LogCallback());
    const std::string& enginePath = config->DetectionModelEngine();
    const std::string& onnxPath = config->DetectionModelOnnx();

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

    mRuntime =
        std::make_unique<tensorrt::TensorRTRuntime>(std::move(tensorrtEngines[enginePath]), logger);
    ConfidenceSet(config->FaceDetectionThreshold());
}
void face::CDetection::ConfidenceSet(float thr) {
    mConfidenceThreshold = thr;
}
cv::Mat face::CDetection::Grid(int height, int width) {
    auto grid_x = cv::Mat(height, width, CV_32FC1);
    auto grid_y = cv::Mat(height, width, CV_32FC1);
    for (auto j = 0; j < height; j++) {
        for (auto i = 0; i < width; i++) {
            grid_x.ptr<float>(j)[i] = float(i);
            grid_y.ptr<float>(j)[i] = float(j);
        }
    }
    std::vector<cv::Mat> grids;
    cv::Mat grid;
    grids.push_back(grid_x);
    grids.push_back(grid_y);
    cv::merge(grids, grid);

    return grid;
}

std::vector<cv::Point> face::CDetection::RepeatVector(const std::vector<cv::Point>& vec1) {
    std::vector<cv::Point> vec2;
    for (cv::Point val : vec1) {
        for (int i = 0; i < 2; i++) {
            vec2.push_back(val);
        }
    }
    return vec2;
}

std::vector<face::SFace>
face::CDetection::Mat2vecConvertor(const cv::Mat& box, const cv::Mat& scores,
                                   const cv::Mat& k_point,
                                   const std::vector<cv::Point>& keypointVector) const {
    int rows = box.rows;
    std::vector<SFace> faces;
    for (int i = 0; i < rows; i++) {
        SFace box_data;
        float score = scores.ptr<float>(i)[0];
        if (score < mConfidenceThreshold) {
            continue;
        }
        float x1 = (float(keypointVector[i].x) - box.ptr<float>(i)[0]) / mDetectScale;
        float y1 = (float(keypointVector[i].y) - box.ptr<float>(i)[1]) / mDetectScale;
        float x2 = (float(keypointVector[i].x) + box.ptr<float>(i)[2]) / mDetectScale;
        float y2 = (float(keypointVector[i].y) + box.ptr<float>(i)[3]) / mDetectScale;
        float w = x2 - x1;
        float h = y2 - y1;
        x1 = std::max(x1, float(0.0));
        y1 = std::max(y1, float(0.0));
        box_data.mBbox = cv::Rect(x1, y1, w, h);

        box_data.mConfidence = score;

        for (int j = 0; j < 10; j += 2) {
            cv::Point point;

            point.x = (keypointVector[i].x + k_point.ptr<float>(i)[j]) / mDetectScale;
            point.y = (keypointVector[i].y + k_point.ptr<float>(i)[j + 1]) / mDetectScale;

            box_data.mKeypoint.push_back(point);
        }
        faces.push_back(box_data);
    }
    return faces;
}

cv::Mat face::CDetection::PrepareImage(const cv::Mat& image) {
    cv::Size img_size = image.size();
    cv::Mat model_input = cv::Mat::zeros(mModelShape, CV_8UC3);
    cv::Size new_size;
    if (img_size.width > img_size.height) {
        new_size.width = mModelShape.width;
        new_size.height = img_size.height * new_size.width / img_size.width;
        auto temp = image.clone();
        cv::resize(image, temp, new_size);
        temp.copyTo(model_input(cv::Rect(0, 0, temp.cols, temp.rows)));
    } else {
        new_size.height = mModelShape.height;
        new_size.width = img_size.width * new_size.height / img_size.height;
        auto temp = image.clone();
        cv::resize(image, temp, new_size);
        temp.copyTo(model_input(cv::Rect(0, 0, temp.cols, temp.rows)));
    }
    mDetectScale = float(new_size.height) / float(img_size.height);
    return model_input;
}

std::vector<face::SFace> face::CDetection::Forward(const cv::Mat& model_input) {
    rt::tensorrt::FloatImageTensor input_tensor(
        std::vector<cv::Mat>{model_input}, mRuntime->GetInputShape(), true, 1.f / 128.f,
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
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> NMSResult;
    std::vector<SFace> faceBoxes;
    std::vector<std::vector<cv::Point>> keyPoints;

    int k = 3;
    for (int i = 0; i < 3; i++) {
        cv::Mat score = outputs[i];
        cv::Mat bbox = outputs[i + 1 * k] * mStride[i];
        cv::Mat k_point = outputs[i + 2 * k] * mStride[i];

        int W = model_input.cols / mStride[i];
        int H = model_input.rows / mStride[i];
        cv::Mat grid = Grid(W, H) * mStride[i];

        cv::Mat flat = grid.reshape(2, grid.total());
        std::vector<cv::Point> vec = grid.isContinuous() ? flat : flat.clone();
        vec = RepeatVector(vec);

        std::vector<SFace> vec_bbox;
        vec_bbox = Mat2vecConvertor(bbox, score, k_point, vec);
        copy(vec_bbox.begin(), vec_bbox.end(), std::back_inserter(faceBoxes));
    }

    for (auto& item : faceBoxes) {
        boxes.emplace_back(item.mBbox);
        confidences.emplace_back(item.mConfidence);
        keyPoints.emplace_back(item.mKeypoint);
    }

    cv::dnn::NMSBoxes(boxes, confidences, mConfidenceThreshold, mIOUThreshold, NMSResult);

    std::vector<SFace> results{};
    for (int idx : NMSResult) {
        SFace result{};
        result.mConfidence = confidences[idx];
        result.mKeypoint = keyPoints[idx];
        result.mBbox = boxes[idx];

        results.push_back(result);
    }

    return results;
}

std::vector<face::SFace> face::CDetection::Inference(cv::Mat& frame) {
    cv::Mat image_processed = PrepareImage(frame);
    return Forward(image_processed);
}

face::CDetection::~CDetection() {
    //    mNet.reset();
}
