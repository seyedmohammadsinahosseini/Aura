#ifndef TENSORRT_RUNTIME_OUTPUT_TENSOR_INCLUDE_OUTPUT_TENSOR_H_
#define TENSORRT_RUNTIME_OUTPUT_TENSOR_INCLUDE_OUTPUT_TENSOR_H_

#include "tensor/include/tensor.h"
#include <NvInfer.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace xbox::base_component::ai::runtime_handler::tensorrt {

template<typename T>
class OutputTensor : public Tensor {
public:
    explicit OutputTensor(const char *name, const nvinfer1::Dims64 &dims);

    [[nodiscard]] std::size_t Size() const override;

    [[nodiscard]] const void* Data() const override;

    [[nodiscard]] void* MutableData() override;

    [[nodiscard]] nvinfer1::Dims Shape() const override;

    [[nodiscard]] const char* Name() const override;

    [[nodiscard]] void* CudaData() const override;

    ~OutputTensor() override;

private:
    std::string name_{};
    nvinfer1::Dims64 dims_{};
    std::vector<T> data_{};
    void* cuda_data_{};
    size_t size_{1};
};


template<typename T>
OutputTensor<T>::OutputTensor(const char *name, const nvinfer1::Dims64 &dims) : name_{name}, dims_{dims} {
    if (dims_.d[0] < 0)
        throw std::runtime_error("Batch size cannot be a negative number");

    for (int i = 0; i < dims_.nbDims; i++)
        size_ *= dims_.d[i];

    data_.resize(size_);
    size_ *= sizeof(T);

    if(cudaMalloc(&cuda_data_, size_) != cudaSuccess) //TODO: why leak?
        throw std::runtime_error("Could cuda malloc for output tensor");
}

template<typename T>
std::size_t OutputTensor<T>::Size() const {
    return size_;
}

template<typename T>
const void *OutputTensor<T>::Data() const {
    return data_.data();
}

template<typename T>
void *OutputTensor<T>::MutableData() {
    return data_.data();
}

template<typename T>
nvinfer1::Dims OutputTensor<T>::Shape() const {
    return dims_;
}

template<typename T>
const char *OutputTensor<T>::Name() const {
    return name_.c_str();
}

template<typename T>
void *OutputTensor<T>::CudaData() const {
    return cuda_data_;
}

template<typename T>
OutputTensor<T>::~OutputTensor() {
    cudaFree(cuda_data_);
}

}

#endif //TENSORRT_RUNTIME_OUTPUT_TENSOR_INCLUDE_OUTPUT_TENSOR_H_
