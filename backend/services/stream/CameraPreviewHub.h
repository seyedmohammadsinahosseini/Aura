#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <opencv2/opencv.hpp>

namespace backend {

class CameraPreviewHub {
public:
    struct EncodedJpegFrame {
        std::vector<uint8_t> jpegBytes{};
        int width{0};
        int height{0};
        int64_t timestamp{0};
        float fps{0.0f};
    };

    struct RawBgrFrame {
        std::vector<uint8_t> bgrBytes{};
        int width{0};
        int height{0};
        int stride{0};
        int64_t timestamp{0};
        float fps{0.0f};
    };

    void subscribe(const std::string& cameraUid);
    void unsubscribe(const std::string& cameraUid);
    bool hasSubscribers(const std::string& cameraUid) const;
    int subscriberCount(const std::string& cameraUid) const;

    void publishFrame(const std::string& cameraUid, const cv::Mat& frame, float fps = 0.0f);
    void clearCamera(const std::string& cameraUid);
    void removeCamera(const std::string& cameraUid);
    std::optional<EncodedJpegFrame> getLatestJpeg(const std::string& cameraUid) const;
    std::optional<RawBgrFrame> getLatestBgr(const std::string& cameraUid) const;

private:
    struct PreviewFrame {
        std::vector<uint8_t> jpegBytes{};
        std::vector<uint8_t> bgrBytes{};
        int width{0};
        int height{0};
        int stride{0};
        int64_t timestamp{0};
        float fps{0.0f};
    };

    mutable std::mutex mutex_{};
    std::unordered_map<std::string, int> subscriberCounts_{};
    std::unordered_map<std::string, PreviewFrame> latestFrames_{};
};

}  // namespace backend
