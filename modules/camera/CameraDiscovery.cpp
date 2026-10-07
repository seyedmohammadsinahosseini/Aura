#include "CameraDiscovery.h"
#include "OnvifClient.h"
#include "OnvifDiscovery.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <linux/videodev2.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <future>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace camera {

static std::vector<std::string> getVideoDevicePaths() {
    std::vector<std::string> paths;
    try {
        if (!fs::exists("/dev"))
            return paths;
        for (const auto& entry : fs::directory_iterator("/dev")) {
            std::string name = entry.path().filename().string();
            if (name.compare(0, 5, "video") == 0)
                paths.push_back(entry.path().string());
        }
        std::sort(paths.begin(), paths.end());
    } catch (...) {
    }
    return paths;
}

static bool isRealVideoDevice(int fd) {
    v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
        return false;
    uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    return (caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
}

std::vector<DeviceInfo> CameraDiscovery::discoverLocal() {
    std::vector<DeviceInfo> result;
    for (const auto& path : getVideoDevicePaths()) {
        int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0)
            continue;
        if (isRealVideoDevice(fd)) {
            v4l2_capability cap;
            ioctl(fd, VIDIOC_QUERYCAP, &cap);
            DeviceInfo info;
            info.devicePath = path;
            info.streamUri = path;
            info.cardName = reinterpret_cast<const char*>(cap.card);
            info.driver = reinterpret_cast<const char*>(cap.driver);
            info.type = ConnectionType::LOCAL;
            info.isVideoCapture = true;
            try {
                info.index = std::stoi(path.substr(10));
            } catch (...) {
                info.index = -1;
            }
            result.push_back(info);
        }
        close(fd);
    }
    return result;
}

bool CameraDiscovery::isPortOpen(const std::string& ip, int port, int timeoutMs) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    fcntl(fd, F_SETFL, O_NONBLOCK);
    connect(fd, (struct sockaddr*)&addr, sizeof(addr));

    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(fd, &fdset);
    bool connected = (select(fd + 1, nullptr, &fdset, nullptr, &tv) == 1);

    close(fd);
    return connected;
}

bool CameraDiscovery::isOnvifEndpoint(const std::string& ip, int port, int timeoutMs) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    fcntl(fd, F_SETFL, O_NONBLOCK);
    connect(fd, (struct sockaddr*)&addr, sizeof(addr));

    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(fd, &fdset);
    bool connected = (select(fd + 1, nullptr, &fdset, nullptr, &tv) == 1);
    if (!connected) {
        close(fd);
        return false;
    }

    std::string soapBody = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                           "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
                           "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\">"
                           "<s:Body>"
                           "<tds:GetCapabilities>"
                           "<tds:Category>All</tds:Category>"
                           "</tds:GetCapabilities>"
                           "</s:Body>"
                           "</s:Envelope>";

    std::string request = "POST /onvif/device_service HTTP/1.1\r\n"
                          "Host: " +
        ip + ":" + std::to_string(port) +
        "\r\n"
        "Content-Type: application/soap+xml; charset=utf-8\r\n"
        "Content-Length: " +
        std::to_string(soapBody.size()) +
        "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        soapBody;

    fcntl(fd, F_SETFL, 0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    [[maybe_unused]] ssize_t sent = write(fd, request.data(), request.size());

    std::string response;
    char buf[1024];
    while (true) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        response.append(buf, n);
    }
    close(fd);

    return response.find("Envelope") != std::string::npos ||
        response.find("SOAP-ENV") != std::string::npos ||
        response.find("http://www.onvif.org") != std::string::npos;
}

bool CameraDiscovery::populateOnvifMetadata(DeviceInfo& info) {
    try {
        onvif::OnvifClient client(info.devicePath, info.onvifPort);

        auto full = client.discoverAll();
        info.onvifDeviceServiceUrl = full.caps.deviceServiceUrl;
        info.onvifMediaServiceUrl = full.caps.mediaServiceUrl;

        info.manufacturer = full.device.manufacturer;
        info.model = full.device.model;
        info.firmwareVersion = full.device.firmwareVersion;
        info.serialNumber = full.device.serialNumber;
        info.hardwareId = full.device.hardwareId;

        if (!info.manufacturer.empty() || !info.model.empty())
            info.cardName = info.manufacturer + (info.model.empty() ? "" : " " + info.model);
        else if (!info.hardwareId.empty())
            info.cardName = info.hardwareId;

        for (auto& mp : full.profiles) {
            MediaProfileInfo mpi;
            mpi.token = mp.token;
            mpi.name = mp.name;
            mpi.width = mp.width;
            mpi.height = mp.height;
            mpi.fps = mp.fps;
            mpi.encoding = mp.encoding;
            mpi.streamUri = mp.streamUri;
            info.mediaProfiles.push_back(mpi);
        }

        if (!info.mediaProfiles.empty()) {
            auto& first = info.mediaProfiles[0];
            info.preferredWidth = first.width;
            info.preferredHeight = first.height;
            info.preferredFps = first.fps;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "  ONVIF metadata failed for " << info.devicePath << ": " << e.what()
                  << std::endl;
        return false;
    }
}

bool CameraDiscovery::probeIp(const std::string& ip, int port, DeviceInfo& outInfo) {
    if (!isOnvifEndpoint(ip, port, 1500))
        return false;

    outInfo.devicePath = ip;
    outInfo.type = ConnectionType::ONVIF;
    outInfo.onvifPort = port;
    outInfo.cardName = "ONVIF Device (" + ip + ")";

    populateOnvifMetadata(outInfo);

    if (!outInfo.mediaProfiles.empty() && !outInfo.mediaProfiles[0].streamUri.empty())
        outInfo.streamUri = outInfo.mediaProfiles[0].streamUri;

    return true;
}

std::vector<DeviceInfo> CameraDiscovery::probeOnvifEndpoints(const std::vector<std::string>& ips,
                                                             int port, int timeoutMs,
                                                             int maxThreads) {
    std::vector<DeviceInfo> results;
    std::mutex resultsMutex;
    std::atomic<int> nextIdx{0};
    int totalIps = static_cast<int>(ips.size());

    auto worker = [&]() {
        while (true) {
            int idx = nextIdx.fetch_add(1);
            if (idx >= totalIps)
                break;

            if (!isPortOpen(ips[idx], port, timeoutMs))
                continue;

            DeviceInfo info;
            if (probeIp(ips[idx], port, info)) {
                std::lock_guard<std::mutex> lock(resultsMutex);
                results.push_back(std::move(info));
            }
        }
    };

    std::vector<std::thread> threads;
    int threadCount = std::min(totalIps, maxThreads);
    for (int i = 0; i < threadCount; ++i)
        threads.emplace_back(worker);
    for (auto& t : threads)
        t.join();

    return results;
}

static const char* kCommonRtspPaths[] = {
    "/live",
    "/stream1",
    "/stream2",
    "/stream3",
    "/ch1/main/av_stream",
    "/ch2/main/av_stream",
    "/cam/realmonitor",
    "/cam/realmonitor?channel=1&subtype=0",
    "/video1",
    "/video2",
    "/video3",
    "/h264",
    "/h264ESVideoTrack",
    "/mpeg4",
    "/mpeg4media",
    "/media",
    "/video",
    "/1",
    "/2",
    "/3",
    "/live/ch0",
    "/live/ch1",
    "/live/main",
    "/live/sub",
    "/onvif-media",
    "/Streaming/Channels/101",
    "/Streaming/Channels/102",
    "/av0",
    "/av1",
    "/live0",
    "/live1",
    "/main",
    "/sub",
    "/H264?channel=1&subtype=0",
    "/h264/ch1/main/av_stream",
};

std::string CameraDiscovery::tryRtspDescribe(const std::string& url, int timeoutMs) {
    std::string host = url;
    std::string rtspPrefix = "rtsp://";
    int port = 554;

    if (url.find(rtspPrefix) == 0) {
        std::string rest = url.substr(rtspPrefix.size());
        auto slashPos = rest.find('/');
        auto hostPart = (slashPos != std::string::npos) ? rest.substr(0, slashPos) : rest;

        auto atPos = hostPart.find('@');
        if (atPos != std::string::npos) {
            // Strip any userinfo ("user:pass@") so it is never used for
            // automatic authentication. Credentials must be provided by the
            // operator through the camera configuration.
            hostPart = hostPart.substr(atPos + 1);
        }

        auto colonPos = hostPart.find(':');
        if (colonPos != std::string::npos) {
            host = hostPart.substr(0, colonPos);
            try {
                port = std::stoi(hostPart.substr(colonPos + 1));
            } catch (...) {
            }
        } else {
            host = hostPart;
        }
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return {};

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    fcntl(fd, F_SETFL, O_NONBLOCK);
    connect(fd, (struct sockaddr*)&addr, sizeof(addr));

    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(fd, &fdset);
    if (select(fd + 1, nullptr, &fdset, nullptr, &tv) != 1) {
        close(fd);
        return {};
    }

    std::string req = "DESCRIBE " + url +
        " RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "Accept: application/sdp\r\n"
        "Connection: close\r\n"
        "\r\n";

    fcntl(fd, F_SETFL, 0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    [[maybe_unused]] ssize_t sent = write(fd, req.data(), req.size());

    std::string response;
    char buf[4096];
    while (true) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        response.append(buf, n);
    }

    if (response.find("200 OK") != std::string::npos) {
        close(fd);
        return url;
    }

    // Unauthorized (or any other non-success response): authentication is never
    // attempted automatically with built-in/default credentials. Credentials,
    // when required, must be supplied by the operator (e.g. via the camera
    // configuration) instead of being guessed by the software.
    close(fd);
    return {};
}

std::vector<std::string> CameraDiscovery::probeRtspPaths(const std::string& ip, int timeoutMs) {
    std::vector<std::string> found;
    for (const auto& rtspPath : kCommonRtspPaths) {
        std::string url = "rtsp://" + ip + ":554" + rtspPath;
        std::string result = tryRtspDescribe(url, timeoutMs);
        if (!result.empty()) {
            found.push_back(result);
            return found;
        }
    }
    return found;
}

std::vector<std::string> CameraDiscovery::generateIpRange(const std::string& subnet) {
    std::vector<std::string> ips;
    ips.reserve(254);
    for (int i = 1; i <= 254; ++i)
        ips.push_back(subnet + "." + std::to_string(i));
    return ips;
}

std::vector<std::string> CameraDiscovery::generateCommonSubnets() {
    return {"192.168.0", "192.168.1", "192.168.2", "192.168.10",
            "10.0.0",    "10.0.1",    "172.16.0",  "172.17.0"};
}

bool CameraDiscovery::isIpInAnySubnet(const std::string& ip,
                                      const std::vector<std::string>& subnets) {
    for (const auto& sn : subnets) {
        if (ip.compare(0, sn.length(), sn) == 0)
            return true;
    }
    return false;
}

static std::vector<std::string> getLocalSubnets() {
    std::set<std::string> subnets;
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) < 0)
        return {};

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;

        struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
        struct sockaddr_in* mask = (struct sockaddr_in*)ifa->ifa_netmask;

        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        uint32_t netmask = ntohl(mask->sin_addr.s_addr);
        uint32_t network = ip & netmask;

        uint8_t a = (network >> 24) & 0xFF;
        uint8_t b = (network >> 16) & 0xFF;
        if (a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d.%d.%d", a, b, (network >> 8) & 0xFF);
            subnets.insert(buf);
        }
    }
    freeifaddrs(ifaddr);
    return {subnets.begin(), subnets.end()};
}

std::vector<DeviceInfo> CameraDiscovery::discoverOnvif(int timeoutMs, bool deepSearch) {
    std::vector<DeviceInfo> results;
    std::set<std::string> seenIps;

    // Phase 1: WS-Discovery multicast (ONVIF standard §4)
    std::cout << "  Phase 1: WS-Discovery multicast..." << std::endl;
    {
        auto wsDevices = onvif::OnvifDiscovery::probe(std::min(timeoutMs, 3000));
        for (auto& d : wsDevices) {
            if (!seenIps.insert(d.ip).second)
                continue;
            std::cout << "    Found ONVIF device: " << d.ip << " (" << d.name << ")" << std::endl;

            // Convert DiscoveredDevice → DeviceInfo, populate metadata
            {
                DeviceInfo info;
                info.devicePath = d.ip;
                info.type = ConnectionType::ONVIF;
                info.xaddrs = d.xaddrs;
                info.scopes = d.scopes;
                info.onvifPort = d.port;
                info.cardName = d.name;

                std::promise<bool> p;
                auto f = p.get_future();
                std::thread t([&]() { p.set_value(populateOnvifMetadata(info)); });
                if (f.wait_for(std::chrono::seconds(6)) == std::future_status::timeout) {
                    t.detach();
                    std::cout << "      Metadata fetch timed out" << std::endl;
                } else {
                    t.join();
                    if (f.get() && !info.mediaProfiles.empty() &&
                        !info.mediaProfiles[0].streamUri.empty()) {
                        info.streamUri = info.mediaProfiles[0].streamUri;
                        std::cout << "      Stream: " << info.streamUri << std::endl;
                    }
                }

                results.push_back(std::move(info));
            }
        }
    }

    // Phase 2 (optional): RTSP fallback for deep search
    if (deepSearch) {
        std::cout << "  Phase 2: RTSP fallback scan..." << std::endl;
        auto localSubnets = getLocalSubnets();
        if (localSubnets.empty())
            localSubnets = {"192.168.1"};

        std::vector<std::string> allIps;
        for (const auto& sn : localSubnets)
            for (int i = 1; i <= 254; ++i)
                allIps.push_back(sn + "." + std::to_string(i));

        std::atomic<int> scanIdx{0};
        std::set<std::string> liveHosts;
        std::mutex liveMutex;
        int nThreads = std::min(50, (int)allIps.size());

        auto scanWorker = [&]() {
            while (true) {
                int i = scanIdx.fetch_add(1);
                if (i >= (int)allIps.size())
                    break;
                if (CameraDiscovery::isPortOpen(allIps[i], 554, 200)) {
                    std::lock_guard<std::mutex> l(liveMutex);
                    liveHosts.insert(allIps[i]);
                }
            }
        };

        std::vector<std::thread> workers;
        for (int i = 0; i < nThreads; ++i)
            workers.emplace_back(scanWorker);
        for (auto& t : workers)
            t.join();

        std::cout << "    Found " << liveHosts.size() << " hosts with port 554 open" << std::endl;

        std::vector<std::string> rtspIps;
        for (const auto& ip : liveHosts)
            if (!seenIps.count(ip))
                rtspIps.push_back(ip);

        if (!rtspIps.empty()) {
            std::atomic<int> probeIdx{0};
            std::mutex resultMutex;
            int pThreads = std::min(10, (int)rtspIps.size());

            auto probeWorker = [&]() {
                while (true) {
                    int i = probeIdx.fetch_add(1);
                    if (i >= (int)rtspIps.size())
                        break;
                    auto paths = CameraDiscovery::probeRtspPaths(rtspIps[i], 800);
                    if (!paths.empty()) {
                        DeviceInfo info;
                        info.devicePath = rtspIps[i];
                        info.streamUri = paths[0];
                        info.cardName = "RTSP Camera (" + rtspIps[i] + ")";
                        info.type = ConnectionType::RTSP;
                        std::lock_guard<std::mutex> l(resultMutex);
                        results.push_back(std::move(info));
                    }
                }
            };

            std::vector<std::thread> pWorkers;
            for (int i = 0; i < pThreads; ++i)
                pWorkers.emplace_back(probeWorker);
            for (auto& t : pWorkers)
                t.join();
        }
    }

    return results;
}

std::vector<DeviceInfo> CameraDiscovery::discoverAll(bool deepSearch) {
    std::vector<DeviceInfo> all;

    std::cout << "  Local cameras..." << std::endl;
    auto local = discoverLocal();
    all.insert(all.end(), local.begin(), local.end());

    std::cout << "  Network (ONVIF/RTSP)..." << std::endl;
    auto network = discoverOnvif(5000, deepSearch);
    all.insert(all.end(), network.begin(), network.end());

    return all;
}

} // namespace camera
