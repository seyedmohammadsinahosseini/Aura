#ifndef TENSORRT_RUNTIME_FLOAT_IMAGE_TENSOR_INCLUDE_FLOAT_IMAGE_TENSOR_H_
#define TENSORRT_RUNTIME_FLOAT_IMAGE_TENSOR_INCLUDE_FLOAT_IMAGE_TENSOR_H_

#include "tensor/include/tensor.h"
#include <NvInferRuntimeBase.h>
#include <opencv2/opencv.hpp>
#include <vector>

namespace xbox::base_component::ai::runtime_handler::tensorrt {

class FloatImageTensor : public Tensor {
public:
    explicit FloatImageTensor(const std::vector<cv::Mat>& images, const nvinfer1::Dims& dims, bool BGR2RGB=false, double alpha=1.f/255.f, double beta=0, int interpolation = cv::INTER_LINEAR);

    [[nodiscard]] std::size_t Size() const override;

    [[nodiscard]] const void* Data() const override;

    [[nodiscard]] void* MutableData() override;

    [[nodiscard]] nvinfer1::Dims Shape() const override;

    [[nodiscard]] const char* Name() const override;

    [[nodiscard]] void* CudaData() const override;

private:
    std::vector<float> batch_input_tensor_{};
    std::int64_t batch_size_{};
    std::int64_t channels_count_{};
    std::int64_t height_{};
    std::int64_t width_{};
};

}
#endif //TENSORRT_RUNTIME_FLOAT_IMAGE_TENSOR_INCLUDE_FLOAT_IMAGE_TENSOR_H_
