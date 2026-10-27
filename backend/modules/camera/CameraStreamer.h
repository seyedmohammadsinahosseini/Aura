#ifndef CAMERA_STREAMER_H
#define CAMERA_STREAMER_H

#include <opencv2/opencv.hpp>
#include <chrono>
#include <thread>
#include <atomic>
#include <memory>
#include "services/buffer/safe_circular_buffer.h"
#include "CameraDiscovery.h"

namespace camera {

    class CameraStreamer {
    public:
        explicit CameraStreamer(const DeviceInfo& info, size_t bufferSize = 1);
        ~CameraStreamer();

        // Prevent copying
        CameraStreamer(const CameraStreamer&) = delete;
        CameraStreamer& operator=(const CameraStreamer&) = delete;

        bool start();
        void stop();
        
        // Get the latest frame from memory
        bool getLatestFrame(cv::Mat& frame, uint64_t& seq);
        bool isRunning() const { return m_running; }
        uint64_t frameSeq() const { return m_frameSeq; }
        // Runtime stream adjustments
        void setStreamParams(int fps, int width, int height);

    private:
        void captureLoop();
        bool reopenCapture();

        DeviceInfo m_info;
        cv::VideoCapture m_cap;
        // runtime desired params
        int m_desiredFps{0};
        int m_desiredWidth{0};
        int m_desiredHeight{0};
        std::thread m_thread;
        std::atomic<bool> m_running{false};
        std::chrono::steady_clock::time_point m_lastFrameTime;
        std::atomic<uint64_t> m_frameSeq{0};

        // Using your project's safe_circular_buffer
        std::unique_ptr<veera::safe_circular_buffer<cv::Mat>> m_frameBuffer;
    };
}

#endif
