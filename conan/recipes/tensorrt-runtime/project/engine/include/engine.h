#ifndef TENSORRT_RUNTIME_ENGINE_INCLUDE_ENGINE_H_
#define TENSORRT_RUNTIME_ENGINE_INCLUDE_ENGINE_H_

#include "logger/include/logger.h"
#include <NvInfer.h>

#include <string>
#include <vector>

namespace xbox::base_component::ai::runtime_handler::tensorrt {

    class Engine {
    public:
        explicit Engine(Logger &logger, const std::string &engine_file_path);

        explicit Engine(Logger &logger, const std::string &onnxFilePath, const std::string &inputName,
                        const nvinfer1::Dims4 &minShape, const nvinfer1::Dims4 &optShape,
                        const nvinfer1::Dims4 &maxShape, std::size_t poolSize);

        nvinfer1::IExecutionContext *createExecutionContext();

        [[nodiscard]] int32_t getNbIOTensors() const noexcept;

        [[nodiscard]] char const *getIOTensorName(int32_t index) const noexcept;

        nvinfer1::TensorIOMode getTensorIOMode(char const *tensorName) const noexcept;

        nvinfer1::Dims getTensorShape(char const *tensorName) const noexcept;

        nvinfer1::DataType getTensorDataType(char const *tensorName) const noexcept;

        bool SaveEngineToFile(const std::string &filePath);

    private:
        std::unique_ptr<nvinfer1::ICudaEngine> cudaEngine_;

        static void ReadFile(const std::string &file_path, std::vector<char> &output);
    };

}

#endif //TENSORRT_RUNTIME_ENGINE_INCLUDE_ENGINE_H_
