#ifndef FACE_CPP_RECOGNITION_UTILS_H
#define FACE_CPP_RECOGNITION_UTILS_H

#include <opencv2/opencv.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace face {

inline void ensureDirectoryExists(const std::string& dirPath) {
    std::error_code errorCode;
    std::filesystem::create_directories(dirPath, errorCode);
    if (errorCode) {
        throw std::runtime_error("failed to create directory: " + dirPath + " - " + errorCode.message());
    }
}

inline std::string buildCropPath(const std::string& dirPath,
                                 int64_t timestamp,
                                 int64_t personId,
                                 const std::string& trackId) {
    const std::string personToken = personId >= 0 ? std::to_string(personId) : "unknown";
    return dirPath + "/" + std::to_string(timestamp) + "_" + personToken + "_" + trackId + ".jpg";
}

inline bool saveCroppedFace(const cv::Mat& frame, const cv::Rect& bbox, const std::string& path) {
    if (frame.empty() || bbox.width <= 0 || bbox.height <= 0) {
        return false;
    }

    const cv::Rect frameRect(0, 0, frame.cols, frame.rows);
    const cv::Rect safeBbox = bbox & frameRect;
    if (safeBbox.width <= 0 || safeBbox.height <= 0) {
        return false;
    }

    const cv::Mat cropped = frame(safeBbox).clone();
    return cv::imwrite(path, cropped);
}

}  // namespace face

#endif  // FACE_CPP_RECOGNITION_UTILS_H
