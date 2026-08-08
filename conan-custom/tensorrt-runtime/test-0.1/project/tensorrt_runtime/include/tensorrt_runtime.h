#ifndef TENSORRT_RUNTIME_TENSORRT_RUNTIME_INCLUDE_TENSORRT_RUNTIME_H_
#define TENSORRT_RUNTIME_TENSORRT_RUNTIME_INCLUDE_TENSORRT_RUNTIME_H_

#include "engine/include/engine.h"
#include "logger/include/logger.h"
#include "tensor/include/tensor.h"
#include "output_tensor/include/output_tensor.h"

namespace xbox::base_component::ai::runtime_handler::tensorrt {

class TensorRTRuntime {
 public:
    explicit TensorRTRuntime(std::shared_ptr<Engine> engine, Logger &logger);

    // No copy
    TensorRTRuntime(const TensorRTRuntime &) = delete;
    TensorRTRuntime &operator=(const TensorRTRuntime &) = delete;

    // Allow move
    TensorRTRuntime(TensorRTRuntime &&) noexcept = default;
    TensorRTRuntime &operator=(TensorRTRuntime &&) noexcept = default;

    void Inference(const Tensor &input);

    [[nodiscard]] const std::vector<std::unique_ptr<Tensor>> &OutputTensors() const;

    [[nodiscard]] const nvinfer1::Dims64 &GetInputShape() const;

    ~TensorRTRuntime();
 private:

    std::shared_ptr<Engine> engine_;
    std::unique_ptr<nvinfer1::IExecutionContext> context_;

    Logger logger_;
    cudaStream_t stream_{};

    nvinfer1::Dims64 input_shape_{};
    std::string input_name_{};
    std::vector<std::unique_ptr<Tensor>> output_tensors_{};

    std::size_t input_size_ = -1;
    std::size_t output_size_ = -1;

    void* d_input_;
};

}
#endif //TENSORRT_RUNTIME_TENSORRT_RUNTIME_INCLUDE_TENSORRT_RUNTIME_H_
