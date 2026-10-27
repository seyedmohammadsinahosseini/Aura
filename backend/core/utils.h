#ifndef FACE_CPP_UTILS_H
#define FACE_CPP_UTILS_H

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

inline std::string WriteOnnxFile(const std::string& onnxPath) {
    if (!std::filesystem::exists(onnxPath)) {
        const auto absolutePath = std::filesystem::absolute(onnxPath).lexically_normal().string();
        std::cerr << "ONNX file not found: " << absolutePath << std::endl;
        throw std::runtime_error("ONNX file not found: " + absolutePath);
    }
    return onnxPath;
}

#endif  // FACE_CPP_UTILS_H
