#include "float_image_tensor/include/float_image_tensor.h"
#include <vector>

using namespace xbox::base_component::ai::runtime_handler::tensorrt;

FloatImageTensor::FloatImageTensor(const std::vector<cv::Mat>& images, const nvinfer1::Dims& dims, bool BGR2RGB, double alpha, double beta, int interpolation) {
    batch_size_ = images.size();
    if (dims.nbDims == 3) {
        channels_count_ = dims.d[0];
        height_   = dims.d[1];
        width_    = dims.d[2];
    }
    else if (dims.nbDims == 4) {
        channels_count_ = dims.d[1];
        height_   = dims.d[2];
        width_    = dims.d[3];
    }
    else
        throw std::invalid_argument("dims.nbDims can only be equal to 3 or 4");

    int image_type;
    if (channels_count_ == 3)
        image_type = CV_32FC3;
    else if (channels_count_ == 1)
        image_type = CV_32FC1;
    else
        throw std::runtime_error("Undefined image type with channels count " + std::to_string(channels_count_));

    int image_index = 0;
    batch_input_tensor_.resize(images.size() * channels_count_ * height_ * width_, 0.0f);
    for (const auto& image : images) {
        cv::Mat resized_image;
        cv::resize(image, resized_image, cv::Size(width_, height_), 0, 0, interpolation);

        if (BGR2RGB)
            cv::cvtColor(resized_image, resized_image, cv::COLOR_BGR2RGB);

        //TODO: do this with cuda
        resized_image.convertTo(resized_image, image_type, alpha, beta);

        //TODO: change this snippet to have better performance
        std::vector<cv::Mat> channels(channels_count_);
        cv::split(resized_image, channels);

        long long offset = image_index * channels_count_ * height_ * width_;
        for (int i = 0; i < channels_count_; ++i) {
            memcpy(batch_input_tensor_.data() + offset + i * height_ * width_,
                   channels[i].data,
                   height_ * width_ * sizeof(float));
        }
        image_index++;
    }
}

std::size_t FloatImageTensor::Size() const {
    return batch_input_tensor_.size() * sizeof(float);
}

const void* FloatImageTensor::Data() const {
    return batch_input_tensor_.data();
}

void *FloatImageTensor::MutableData() {
    throw std::runtime_error("Do not use this method");
}

nvinfer1::Dims FloatImageTensor::Shape() const {
    return nvinfer1::Dims4{batch_size_, channels_count_, height_, width_};
}

const char *FloatImageTensor::Name() const {
    return nullptr; //TODO
}

void *FloatImageTensor::CudaData() const {
    throw std::runtime_error("Do not use this method");
}
