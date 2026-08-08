#include "CHead.h"
#include "core/utils.h"
#include <float_image_tensor/include/float_image_tensor.h>

face::CHead::CHead(const std::shared_ptr<face::FaceConfig>& config,
                   std::map<std::string, std::shared_ptr<rt::tensorrt::Engine>>& tensorrtEngines) {
    using namespace rt;
    tensorrt::Logger logger(config->LogProvider(), config->LogCallback());
    const std::string &enginePath = config->HeadDetectionModelEngine();
    const std::string &onnxPath = config->HeadDetectionModelOnnx();

    if (enginePath.empty())
        throw std::runtime_error("Engine path cannot be empty");

    if (!tensorrtEngines.contains(enginePath)) {
        if (std::filesystem::exists(enginePath))
            tensorrtEngines[enginePath] = std::make_shared<tensorrt::Engine>(logger, enginePath.data());
        else {
            auto filename = WriteOnnxFile(onnxPath);
            nvinfer1::Dims4 minShape{1, 3, mModelShape.height, mModelShape.width};
            nvinfer1::Dims4 optShape{1, 3, mModelShape.height, mModelShape.width};
            nvinfer1::Dims4 maxShape{1, 3, mModelShape.height, mModelShape.width};

            std::size_t poolSize = 1ULL << 30; // 1GB GPU Memory
            std::string modelInputName = "images";
            tensorrtEngines[enginePath] = std::make_shared<tensorrt::Engine>(logger,
                                                                             filename,
                                                                             modelInputName,
                                                                             minShape,
                                                                             optShape,
                                                                             maxShape,
                                                                             poolSize);
            tensorrtEngines[enginePath]->SaveEngineToFile(enginePath);
        }
    }

    mRuntime = std::make_unique<tensorrt::TensorRTRuntime>(tensorrtEngines[enginePath], logger);
    setConfidence(config->HeadDetectionThreshold());
}

std::any face::CHead::Inference(const cv::Mat &input) {
    cv::Mat modelInput = input;
    if (mLetterBoxForSquare && mModelShape.width == mModelShape.height)
        modelInput = FormatToSquare(modelInput);

    rt::tensorrt::FloatImageTensor input_tensor(std::vector<cv::Mat>{modelInput},
                                                                                       mRuntime->GetInputShape(),
                                                                                       true); //TODO: change to a batch of images

    mRuntime->Inference(input_tensor);
    const auto &output_tensors = mRuntime->OutputTensors();

    std::vector<cv::Mat> outputs;
    for (auto &output : output_tensors) {
        const auto &shape = output->Shape();

        cv::Mat outputMat;
        if (shape.nbDims == 2) {
            size_t height = shape.d[0];
            size_t width = shape.d[1];
            outputMat = cv::Mat(height, width, CV_32F, output->MutableData());
        } else if (shape.nbDims == 3) {
            int height = shape.d[1];
            int width = shape.d[2];
            int size[3] = {(int) shape.d[0], height, width};
            outputMat = cv::Mat(shape.nbDims, size, CV_32F, output->MutableData());
        } else if (shape.nbDims == 4) {
            int height = shape.d[2];
            int width = shape.d[3];
            int size[3] = {(int) shape.d[1], height, width};
            outputMat = cv::Mat(shape.nbDims - 1, size, CV_32F, output->MutableData());
        }
        outputs.emplace_back(outputMat);
    }

    int rows = outputs[0].size[1];
    int dimensions = outputs[0].size[2];

    bool yolov8 = false;
    // yolov5 has an output of shape (batchSize, 25200, 85) (Num classes + box[x,y,w,h] + confidence[c])
    // yolov8 has an output of shape (batchSize, 84,  8400) (Num classes + box[x,y,w,h])
    if (dimensions > rows) // Check if the shape[2] is more than shape[1] (yolov8)
    {
        yolov8 = true;
        rows = outputs[0].size[2];
        dimensions = outputs[0].size[1];

        outputs[0] = outputs[0].reshape(1, dimensions);
        cv::transpose(outputs[0], outputs[0]);
    }
    float *data_output = (float *) outputs[0].data;

    float x_factor = modelInput.cols / mModelShape.width;
    float y_factor = modelInput.rows / mModelShape.height;

    std::vector<int> class_ids;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;

    for (int i = 0; i < rows; ++i) {
        if (yolov8) {
            float *classes_scores = data_output + 4;

            cv::Mat scores(1, mClasses.size(), CV_32FC1, classes_scores);
            cv::Point class_id;
            double maxClassScore;

            minMaxLoc(scores, 0, &maxClassScore, 0, &class_id);

            if (maxClassScore > mModelConfidenceThreshold) {
                confidences.push_back(maxClassScore);
                class_ids.push_back(class_id.x);

                float x = data_output[0];
                float y = data_output[1];
                float w = data_output[2];
                float h = data_output[3];

                int left = int((x - 0.5 * w) * x_factor);
                int top = int((y - 0.5 * h) * y_factor);

                int width = int(w * x_factor);
                int height = int(h * y_factor);

                boxes.push_back(cv::Rect(left, top, width, height));
            }
        } else // yolov5
        {
            float confidence = data_output[4];

            if (confidence >= mModelConfidenceThreshold) {
                float *classes_scores = data_output + 5;

                cv::Mat scores(1, mClasses.size(), CV_32FC1, classes_scores);
                cv::Point class_id;
                double max_class_score;

                minMaxLoc(scores, 0, &max_class_score, 0, &class_id);

                if (max_class_score > mModelConfidenceThreshold) {
                    confidences.push_back(confidence);
                    class_ids.push_back(class_id.x);

                    float x = data_output[0];
                    float y = data_output[1];
                    float w = data_output[2];
                    float h = data_output[3];

                    int left = int((x - 0.5 * w) * x_factor);
                    int top = int((y - 0.5 * h) * y_factor);

                    int width = int(w * x_factor);
                    int height = int(h * y_factor);

                    boxes.push_back(cv::Rect(left, top, width, height));
                }
            }
        }

        data_output += dimensions;
    }

    std::vector<int> nms_result;
    cv::dnn::NMSBoxes(boxes, confidences, mModelConfidenceThreshold, mModelNMSThreshold, nms_result);

    const uint16_t MAX_WIDTH = 200;
    const uint16_t MAX_HEIGHT = 200;

    std::vector<SBox> detections{};
    for (int idx : nms_result) {
        if (boxes[idx].width > MAX_WIDTH || boxes[idx].height > MAX_HEIGHT) {
            continue;
        }
        SBox result{};
        result.classId = class_ids[idx];
        result.confidence = confidences[idx];

//        std::random_device rd;
//        std::uniform_int_distribution<int> dis(100, 255);
        result.x1 = boxes[idx].x;
        result.y1 = boxes[idx].y;
        result.width = boxes[idx].width;
        result.height = boxes[idx].height;
        result.x2 = boxes[idx].x + boxes[idx].width;
        result.y2 = boxes[idx].x + boxes[idx].height;

        detections.push_back(result);
    }

    return detections;
}

cv::Mat face::CHead::FormatToSquare(const cv::Mat &source) {
    int col = source.cols;
    int row = source.rows;
    int _max = MAX(col, row);
    cv::Mat result = cv::Mat::zeros(_max, _max, CV_8UC3);
    source.copyTo(result(cv::Rect(0, 0, col, row)));
    return result;
}

face::CHead::~CHead() {
//    mNet.reset();
}

void face::CHead::setConfidence(const float &confidence) {
    mModelConfidenceThreshold = confidence;

}
