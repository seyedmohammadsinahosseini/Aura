#include "engine/include/engine.h"
#include <filesystem>
#include <fstream>
#include <NvOnnxParser.h>

using namespace xbox::base_component::ai::runtime_handler::tensorrt;

Engine::Engine(Logger& logger, const std::string &engine_file_path) {
    std::vector<char> engineData{};
    ReadFile(engine_file_path, engineData);

    std::unique_ptr<nvinfer1::IRuntime> runtime(nvinfer1::createInferRuntime(logger));
    if (!runtime) {
        logger.log(nvinfer1::ILogger::Severity::kERROR, "Could not create infer runtime");
        throw std::runtime_error("Could not create infer runtime");
    }

    cudaEngine_.reset(runtime->deserializeCudaEngine(engineData.data(), engineData.size()));
    if (!cudaEngine_) {
        logger.log(nvinfer1::ILogger::Severity::kERROR, "Could not create engine");
        throw std::runtime_error("Could not create engine");
    }
}

void Engine::ReadFile(const std::string &file_path, std::vector<char> &output) {
    if (!std::filesystem::exists(file_path))
        throw std::runtime_error("Error: " + file_path + " does not exist");

    if (!std::filesystem::is_regular_file(file_path))
        throw std::runtime_error("Error: " + file_path + " is not a regular file");

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Error: Failed to open " + file_path);

    file.seekg(0, std::ifstream::end);
    long int file_size{file.tellg()};
    file.seekg(0, std::ifstream::beg);

    output.resize(file_size);
    file.read(output.data(), file_size);
    file.close();
}

Engine::Engine(Logger& logger, const std::string &onnxFilePath, const std::string &inputName, const nvinfer1::Dims4 &minShape,
               const nvinfer1::Dims4 &optShape, const nvinfer1::Dims4 &maxShape, std::size_t poolSize) {
    std::unique_ptr<nvinfer1::IBuilder> builder(nvinfer1::createInferBuilder(logger));
    if (!builder) {
        // Usually a missing/unloadable TensorRT builder resource library
        // (libnvinfer_builder_resource_smXX.so for this GPU's arch).
        logger.log(nvinfer1::ILogger::Severity::kERROR,
                   "Could not create infer builder (missing TensorRT builder resource for this GPU arch?)");
        throw std::runtime_error("Could not create infer builder");
    }

    std::unique_ptr<nvinfer1::INetworkDefinition> network(builder->createNetworkV2(0U));
    if (!network) {
        logger.log(nvinfer1::ILogger::Severity::kERROR, "Could not create network definition");
        throw std::runtime_error("Could not create network definition");
    }

    std::unique_ptr<nvonnxparser::IParser> parser(nvonnxparser::createParser(*network, logger));
    if (!parser) {
        logger.log(nvinfer1::ILogger::Severity::kERROR, "Could not create ONNX parser");
        throw std::runtime_error("Could not create ONNX parser");
    }
    if (!parser->parseFromFile(onnxFilePath.c_str(),static_cast<int32_t>(nvinfer1::ILogger::Severity::kWARNING))) {
        auto errorMsg = fmt::format("Could not parse onnx file {}", onnxFilePath);
        logger.log(nvinfer1::ILogger::Severity::kERROR, errorMsg.c_str());
        throw std::runtime_error(errorMsg);
    }

    std::unique_ptr<nvinfer1::IBuilderConfig> config(builder->createBuilderConfig());

    nvinfer1::IOptimizationProfile* profile = builder->createOptimizationProfile();
    profile->setDimensions(inputName.c_str(), nvinfer1::OptProfileSelector::kMIN, minShape);
    profile->setDimensions(inputName.c_str(), nvinfer1::OptProfileSelector::kOPT, optShape);
    profile->setDimensions(inputName.c_str(), nvinfer1::OptProfileSelector::kMAX, maxShape);
    config->addOptimizationProfile(profile);

    config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, poolSize);

    cudaEngine_.reset(builder->buildEngineWithConfig(*network, *config));
    if (!cudaEngine_) {
        logger.log(nvinfer1::ILogger::Severity::kERROR, "Could not create engine");
        throw std::runtime_error("Could not create engine");
    }
}

nvinfer1::IExecutionContext *Engine::createExecutionContext() {
    return cudaEngine_->createExecutionContext();
}

int32_t Engine::getNbIOTensors() const noexcept {
    return cudaEngine_->getNbIOTensors();
}

char const *Engine::getIOTensorName(int32_t index) const noexcept {
    return cudaEngine_->getIOTensorName(index);
}

nvinfer1::TensorIOMode Engine::getTensorIOMode(const char *tensorName) const noexcept {
    return cudaEngine_->getTensorIOMode(tensorName);
}

nvinfer1::Dims Engine::getTensorShape(const char *tensorName) const noexcept {
    return cudaEngine_->getTensorShape(tensorName);
}

nvinfer1::DataType Engine::getTensorDataType(const char *tensorName) const noexcept {
    return cudaEngine_->getTensorDataType(tensorName);
}

bool Engine::SaveEngineToFile(const std::string &filePath) {
    std::unique_ptr<nvinfer1::IHostMemory> serialized(cudaEngine_->serialize());
    if (!serialized) return false;

    const std::filesystem::path outputPath(filePath);
    if (!outputPath.parent_path().empty()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) return false;

    file.write((const char*)serialized->data(), serialized->size());
    return file.good();
}
