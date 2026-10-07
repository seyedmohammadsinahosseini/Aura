#include "CameraStreamer.h"
#include "CameraDiscovery.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <future>

namespace camera {

namespace {

int getMaxResolution(const std::string& devicePath, int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;
    int fd = open(devicePath.c_str(), O_RDONLY);
    if (fd < 0)
        return -1;

    static const uint32_t formats[] = {V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_UYVY,
                                       V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_NV21};

    for (auto pixFmt : formats) {
        v4l2_frmsizeenum frmsize;
        std::memset(&frmsize, 0, sizeof(frmsize));
        frmsize.pixel_format = pixFmt;

        for (int i = 0;; ++i) {
            frmsize.index = i;
            if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) < 0)
                break;

            int w = 0, h = 0;
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                w = frmsize.discrete.width;
                h = frmsize.discrete.height;
            } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
                w = frmsize.stepwise.max_width;
                h = frmsize.stepwise.max_height;
            } else if (frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
                w = frmsize.stepwise.max_width;
                h = frmsize.stepwise.max_height;
            }

            if (w > 0 && h > 0 &&
                static_cast<int64_t>(w) * h > static_cast<int64_t>(outWidth) * outHeight) {
                outWidth = w;
                outHeight = h;
            }
        }
    }

    close(fd);
    return (outWidth > 0 && outHeight > 0) ? 0 : -1;
}

static std::string buildGstPipeline(const std::string& streamUrl, const std::string& encoding,
                                    const std::string& transport) {
    bool isH265 =
        (encoding.find("265") != std::string::npos || encoding.find("HEVC") != std::string::npos ||
         encoding.find("hevc") != std::string::npos);

    std::string caps = isH265 ? "H265" : "H264";
    std::string depay = isH265 ? "rtph265depay" : "rtph264depay";
    std::string parse = isH265 ? "h265parse" : "h264parse";
    std::string decode = isH265 ? "avdec_h265" : "avdec_h264";

    std::string pipeline = "rtspsrc location=" + streamUrl + " latency=0 protocols=" + transport +
        " timeout=5 drop-on-latency=true"
        " ! application/x-rtp,media=video,encoding-name=" +
        caps + " ! " + depay +
        " ! rtpjitterbuffer latency=0 drop-on-latency=true"
        " ! " +
        parse + " ! " + decode +
        " ! videoconvert"
        " ! video/x-raw,format=BGR"
        " ! appsink max-buffers=1 drop=true sync=false";

    return pipeline;
}

} // anonymous namespace

CameraStreamer::CameraStreamer(const DeviceInfo& info, size_t bufferSize)
    : m_info(info), m_lastFrameTime(std::chrono::steady_clock::now()) {
    m_frameBuffer = std::make_unique<backend::safe_circular_buffer<cv::Mat>>(bufferSize);
}

CameraStreamer::~CameraStreamer() {
    stop();
}

bool CameraStreamer::start() {
    try {
        if (m_running)
            return true;

        if (m_cap.isOpened()) {
            m_running = true;
            m_thread = std::thread(&CameraStreamer::captureLoop, this);
            return true;
        }

        bool opened = false;

        if (m_info.type != ConnectionType::LOCAL) {
            std::string streamUrl = CameraDiscovery::tryRtspDescribe(m_info.streamUri, 3000);
            if (streamUrl.empty()) {
                std::cerr << "RTSP DESCRIBE failed for " << m_info.streamUri
                          << ", trying direct open" << std::endl;
                streamUrl = m_info.streamUri;
            }

            std::string encoding = "H264";
            if (!m_info.mediaProfiles.empty() && !m_info.mediaProfiles[0].encoding.empty())
                encoding = m_info.mediaProfiles[0].encoding;

            auto tryOpen = [&](const std::string& pipeline, const std::string& label, int timeoutMs,
                               int apiPref) -> bool {
                auto p = std::make_shared<std::promise<bool>>();
                auto f = p->get_future();
                std::thread t([p, this, pipeline, apiPref]() {
                    try {
                        p->set_value(m_cap.open(pipeline, apiPref));
                    } catch (...) {
                        p->set_value(false);
                    }
                });
                if (f.wait_for(std::chrono::milliseconds(timeoutMs)) ==
                    std::future_status::timeout) {
                    t.detach();
                    std::cerr << label << " timed out (>" << (timeoutMs / 1000) << "s)"
                              << std::endl;
                    return false;
                }
                t.join();
                if (f.get()) {
                    m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
                    return true;
                }
                return false;
            };

            auto tryGst = [&](const std::string& enc) -> bool {
                return tryOpen(buildGstPipeline(streamUrl, enc, m_info.transportProtocol),
                               "GStreamer (" + enc + ")", 8000, cv::CAP_GSTREAMER);
            };

            if (tryGst(encoding)) {
                opened = true;
            } else {
                std::string altEncoding = (encoding == "H264") ? "H265" : "H264";
                if (tryGst(altEncoding)) {
                    opened = true;
                }
            }

            if (!opened) {
                std::cerr << "GStreamer failed, trying FFMPEG for: " << streamUrl << std::endl;
                setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS",
                       "rtsp_transport;udp|fflags;nobuffer+discardcorrupt|"
                       "flags;low_delay|analyzeduration;0|probesize;32",
                       1);

                if (tryOpen(streamUrl, "FFMPEG", 8000, cv::CAP_FFMPEG)) {
                    opened = true;
                }
            }
        } else {
            int maxW = 0, maxH = 0;
            getMaxResolution(m_info.devicePath, maxW, maxH);
            {
                auto p = std::make_shared<std::promise<bool>>();
                auto f = p->get_future();
                std::thread t([p, this]() {
                    try {
                        p->set_value(m_cap.open(m_info.index, cv::CAP_V4L2));
                    } catch (...) {
                        p->set_value(false);
                    }
                });
                if (f.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
                    t.detach();
                    std::cerr << "V4L2 open timed out for " << m_info.devicePath << std::endl;
                    return false;
                }
                t.join();
                if (!f.get())
                    return false;
            }
            m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
            m_cap.set(cv::CAP_PROP_FPS, 30);
            if (maxW > 0 && maxH > 0) {
                m_cap.set(cv::CAP_PROP_FRAME_WIDTH, maxW);
                m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, maxH);
            }
            if (!m_cap.isOpened())
                return false;
        }

        if (m_info.type != ConnectionType::LOCAL && !opened) {
            return false;
        }

        if (m_info.preferredFps > 0)
            m_cap.set(cv::CAP_PROP_FPS, m_info.preferredFps);
        if (m_info.preferredWidth > 0)
            m_cap.set(cv::CAP_PROP_FRAME_WIDTH, m_info.preferredWidth);
        if (m_info.preferredHeight > 0)
            m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, m_info.preferredHeight);
        if (m_desiredFps > 0)
            m_cap.set(cv::CAP_PROP_FPS, m_desiredFps);
        if (m_desiredWidth > 0)
            m_cap.set(cv::CAP_PROP_FRAME_WIDTH, m_desiredWidth);
        if (m_desiredHeight > 0)
            m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, m_desiredHeight);

        m_running = true;
        m_thread = std::thread(&CameraStreamer::captureLoop, this);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Streamer startup failed: " << e.what() << std::endl;
        return false;
    }
}

void CameraStreamer::stop() {
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    if (m_cap.isOpened())
        m_cap.release();
}

bool CameraStreamer::reopenCapture() {
    if (m_cap.isOpened())
        m_cap.release();
    m_lastFrameTime = std::chrono::steady_clock::now();

    if (m_info.type == ConnectionType::LOCAL) {
        auto p = std::make_shared<std::promise<bool>>();
        auto f = p->get_future();
        std::thread t([p, this]() { p->set_value(m_cap.open(m_info.index, cv::CAP_V4L2)); });
        if (f.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
            t.detach();
            return false;
        }
        t.join();
        if (!f.get())
            return false;
        m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
        return true;
    }

    std::string streamUrl = CameraDiscovery::tryRtspDescribe(m_info.streamUri, 3000);
    if (streamUrl.empty()) {
        std::cerr << "RTSP DESCRIBE failed for " << m_info.streamUri << ", trying direct open"
                  << std::endl;
        streamUrl = m_info.streamUri;
    }

    std::string encoding = "H264";
    if (!m_info.mediaProfiles.empty() && !m_info.mediaProfiles[0].encoding.empty())
        encoding = m_info.mediaProfiles[0].encoding;

    auto tryGst = [&](const std::string& enc) -> bool {
        std::string gst_pipeline = buildGstPipeline(streamUrl, enc, m_info.transportProtocol);
        std::cout << "  GStreamer pipeline (" << enc << "): " << gst_pipeline << std::endl;
        auto p = std::make_shared<std::promise<bool>>();
        auto f = p->get_future();
        std::thread t([p, this, gst_pipeline]() {
            try {
                p->set_value(m_cap.open(gst_pipeline, cv::CAP_GSTREAMER));
            } catch (...) {
                p->set_value(false);
            }
        });
        if (f.wait_for(std::chrono::seconds(8)) != std::future_status::timeout) {
            t.join();
            if (f.get()) {
                m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
                return true;
            }
        } else {
            t.detach();
        }
        return false;
    };

    if (tryGst(encoding))
        return true;
    std::string altEncoding = (encoding == "H264") ? "H265" : "H264";
    if (tryGst(altEncoding))
        return true;

    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS",
           "rtsp_transport;udp|fflags;nobuffer+discardcorrupt|"
           "flags;low_delay|analyzeduration;0|probesize;32",
           1);
    {
        auto p = std::make_shared<std::promise<bool>>();
        auto f = p->get_future();
        std::thread t([p, this, streamUrl]() {
            try {
                p->set_value(m_cap.open(streamUrl, cv::CAP_FFMPEG));
            } catch (...) {
                p->set_value(false);
            }
        });
        if (f.wait_for(std::chrono::seconds(8)) != std::future_status::timeout) {
            t.join();
            if (f.get()) {
                m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
                return true;
            }
        } else {
            t.detach();
        }
    }
    return false;
}

void CameraStreamer::captureLoop() {
    // Flush stale frames only for network streams (V4L2 has no buffer)
    if (m_info.type != ConnectionType::LOCAL) {
        for (int i = 0; i < 5; ++i) {
            if (!m_cap.grab())
                break;
        }
    }

    int retryCount = 0;
    const int maxRetries = 3;

    try {
        while (m_running) {
            cv::Mat frame;

            // Extra grabs discard stale buffered frames (network only — V4L2 is live)
            if (m_info.type != ConnectionType::LOCAL) {
                for (int i = 0; i < 3; ++i) {
                    if (!m_cap.grab())
                        break;
                }
            }

            if (m_cap.grab() && m_cap.retrieve(frame) && !frame.empty()) {
                retryCount = 0;
                m_lastFrameTime = std::chrono::steady_clock::now();
                m_frameSeq++;

                if (m_frameBuffer->is_full()) {
                    cv::Mat dummy;
                    m_frameBuffer->pop();
                }
                m_frameBuffer->push(frame);
            } else {
                auto now = std::chrono::steady_clock::now();
                auto elapsed =
                    std::chrono::duration_cast<std::chrono::seconds>(now - m_lastFrameTime).count();

                if (elapsed > 5) {
                    if (retryCount < maxRetries) {
                        std::cerr << "[CameraStreamer] Stream lost, reconnecting (attempt "
                                  << retryCount + 1 << "/" << maxRetries << ")..." << std::endl;
                        retryCount++;
                        std::this_thread::sleep_for(std::chrono::seconds(1 << retryCount));
                        if (!reopenCapture()) {
                            std::cerr << "[CameraStreamer] Reconnect failed" << std::endl;
                            break;
                        }
                        // Flush stale frames after successful reconnect
                        for (int i = 0; i < 5; ++i) {
                            if (!m_cap.grab())
                                break;
                        }
                    } else {
                        // Max retries reached, exit loop to prevent CPU lockup
                        std::cerr << "[CameraStreamer] Max retries reached, giving up."
                                  << std::endl;
                        m_running = false;
                        break;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception in capture thread: " << e.what() << std::endl;
        m_running = false;
    }
}

void CameraStreamer::setStreamParams(int fps, int width, int height) {
    m_desiredFps = fps;
    m_desiredWidth = width;
    m_desiredHeight = height;
    try {
        if (m_cap.isOpened()) {
            if (m_desiredFps > 0)
                m_cap.set(cv::CAP_PROP_FPS, m_desiredFps);
            if (m_desiredWidth > 0)
                m_cap.set(cv::CAP_PROP_FRAME_WIDTH, m_desiredWidth);
            if (m_desiredHeight > 0)
                m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, m_desiredHeight);
        }
    } catch (...) {
    }
}

bool CameraStreamer::getLatestFrame(cv::Mat& frame, uint64_t& seq) {
    seq = m_frameSeq.load();
    return m_frameBuffer->back(frame);
}
} // namespace camera
