#ifndef TENSORRT_RUNTIME_TENSOR_INCLUDE_TENSOR_H_
#define TENSORRT_RUNTIME_TENSOR_INCLUDE_TENSOR_H_

#include <cstddef>
#include <NvInfer.h>

namespace xbox::base_component::ai::runtime_handler::tensorrt {
class Tensor {
public:
    virtual ~Tensor() = default;

    [[nodiscard]] virtual std::size_t Size() const = 0;

    [[nodiscard]] virtual const void* Data() const = 0;

    [[nodiscard]] virtual void* MutableData() = 0;

    [[nodiscard]] virtual nvinfer1::Dims Shape() const = 0;

    [[nodiscard]] virtual const char* Name() const = 0;

    [[nodiscard]] virtual void* CudaData() const = 0;

};
}

#endif //TENSORRT_RUNTIME_TENSOR_INCLUDE_TENSOR_H_
