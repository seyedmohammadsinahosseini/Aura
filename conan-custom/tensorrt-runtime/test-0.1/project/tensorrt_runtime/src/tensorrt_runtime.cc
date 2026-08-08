#include "tensorrt_runtime/include/tensorrt_runtime.h"

#include <NvInfer.h>

using namespace xbox::base_component::ai::runtime_handler::tensorrt;

TensorRTRuntime::TensorRTRuntime(std::shared_ptr<Engine> engine, Logger &logger) : logger_{logger}{
    engine_ = engine;
    context_.reset(engine_->createExecutionContext());
    if (!context_) {
        logger_.log(nvinfer1::ILogger::Severity::kERROR, "Could not create context");
        throw std::runtime_error("Could not create context");
    }
    auto status = cudaStreamCreate(&stream_);
    if (status != cudaSuccess) {
        logger_.log(nvinfer1::ILogger::Severity::kERROR, "Could not create cuda stream");
        throw std::runtime_error(fmt::format("Could not create cuda stream: {}", cudaGetErrorString(status)));
    }

    for (int i = 0; i < engine_->getNbIOTensors(); i++) {
        const char *name = engine_->getIOTensorName(i);
        auto io_mode = engine_->getTensorIOMode(name);
        if (io_mode == nvinfer1::TensorIOMode::kINPUT) {
            input_shape_ = engine_->getTensorShape(name);
            input_name_ = name; //TODO: maybe change this to a vector of input too
        }
    }
}

void TensorRTRuntime::Inference(const Tensor &input) { //TODO: maybe change this to InputTensor class

    if (output_size_ != engine_->getNbIOTensors()) {
        output_tensors_.clear();

        for (int i = 0; i < engine_->getNbIOTensors(); i++) {
            const char *name = engine_->getIOTensorName(i);
            auto io_mode = engine_->getTensorIOMode(name);
            if (io_mode == nvinfer1::TensorIOMode::kOUTPUT) {
                auto output_shape = engine_->getTensorShape(name);
//            output_shape.d[0] = input.Shape().d[0]; //Batch size //TODO: check why this line was included for batch, because it is a bug when output shape is [12800, 1]

                auto output_type = engine_->getTensorDataType(name);
                if (output_type == nvinfer1::DataType::kFLOAT)
                    output_tensors_.push_back(std::make_unique<OutputTensor<float>>(name, output_shape));
                else
                    throw std::runtime_error("Non-float output tensors are not supported right now");
            }
        }
        output_size_ = engine_->getNbIOTensors();
    }
    if (input_name_.empty() || output_tensors_.empty()) {
        throw std::runtime_error("input or output tensors not found");
    }

    if (input_size_ != input.Size()) {
        if (d_input_ != nullptr) {
            cudaFree(d_input_);
            d_input_ = nullptr;
        }

        if (cudaMalloc(&d_input_, input.Size()) != cudaSuccess)
            throw std::runtime_error("Could not cuda malloc for input tensor");
        input_size_ = input.Size();
    }

    if (cudaMemcpyAsync(d_input_, input.Data(), input.Size(), cudaMemcpyHostToDevice, stream_) != cudaSuccess) {
        throw std::runtime_error("Could not cuda memcpy for input tensor");
    }
    if (!context_->setInputShape(input_name_.c_str(), input.Shape())) {
        throw std::runtime_error("Could not set input shape");
    }

    if (!context_->setTensorAddress(input_name_.c_str(), d_input_)) {
        throw std::runtime_error("Could not set input tensor address");
    }

    for (const auto &output_tensor : output_tensors_) {
        if (!context_->setTensorAddress(output_tensor->Name(), output_tensor->CudaData()))
            throw std::runtime_error(fmt::format("Could not set output tensor with name {}", output_tensor->Name()));
    }

    if (!context_->enqueueV3(stream_))
        throw std::runtime_error("Could not run inference using enqueueV3");

    for (auto &output_tensor : output_tensors_) {
        if (cudaMemcpyAsync(output_tensor->MutableData(),
                            output_tensor->CudaData(),
                            output_tensor->Size(),
                            cudaMemcpyDeviceToHost,
                            stream_) != cudaSuccess)
            throw std::runtime_error("Could not cuda memcpy for output tensor");
    }
}

const std::vector<std::unique_ptr<Tensor>> &TensorRTRuntime::OutputTensors() const {
    cudaStreamSynchronize(stream_);

    if (output_tensors_.empty())
        throw std::runtime_error("Output tensors vector is empty. Run Inference first.");

    return output_tensors_;
}

TensorRTRuntime::~TensorRTRuntime() {
    if (d_input_ != nullptr) {
        cudaFree(d_input_);
    }
    if (stream_) cudaStreamDestroy(stream_);
}

const nvinfer1::Dims64 &TensorRTRuntime::GetInputShape() const {
    return input_shape_;
}
