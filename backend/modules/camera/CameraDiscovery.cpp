#include "CameraDiscovery.h"
#include "OnvifClient.h"
#include "OnvifDiscovery.h"
#include "core/crypto/Md5.h"

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
#include <cctype>
#include <cstdlib>
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

// Common default credentials tried automatically ONLY on devices that
// explicitly answer with an auth challenge (HTTP 401 / RTSP 401 / SOAP auth fault)
static const char* kCommonCreds[][2] = {
    {"admin", "admin"},
    {"admin", "12345"},
    {"admin", "123456"},
    {"admin", "password"},
    {"admin", ""},
    {"admin", "admin123"},
    {"admin", "Admin123"},
    {"admin", "Admin@123"},
    {"admin", "Admin2018"},
    {"root", "root"},
    {"root", "12345"},
    {"user", "user"},
    {"viewer", "viewer"},
};

static std::string base64Encode(const std::string& input) {
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    const size_t len = input.size();
    while (i < len) {
        unsigned char c0 = input[i];
        unsigned char c1 = (i + 1 < len) ? input[i + 1] : 0;
        unsigned char c2 = (i + 2 < len) ? input[i + 2] : 0;
        out += b64[c0 >> 2];
        out += b64[((c0 & 0x03) << 4) | (c1 >> 4)];
        out += (i + 1 < len) ? b64[((c1 & 0x0F) << 2) | (c2 >> 6)] : '=';
        out += (i + 2 < len) ? b64[c2 & 0x3F] : '=';
        i += 3;
    }
    return out;
}

struct AuthChallenge {
    bool isDigest{false};
    bool isBasic{false};
    std::string realm;
    std::string nonce;
    std::string qop;
    std::string opaque;
};

static std::string extractQuoteValue(const std::string& src, const std::string& key) {
    auto pos = src.find(key + "=\"");
    if (pos == std::string::npos) return {};
    pos += key.size() + 2;
    auto end = src.find('"', pos);
    if (end == std::string::npos) return {};
    return src.substr(pos, end - pos);
}

static AuthChallenge parseWwwAuthenticate(const std::string& response) {
    AuthChallenge ch;
    auto pos = response.find("WWW-Authenticate:");
    if (pos == std::string::npos) return ch;
    auto lineEnd = response.find("\r\n", pos);
    std::string header = response.substr(pos, lineEnd == std::string::npos
                                            ? std::string::npos : lineEnd - pos);
    if (header.find("Digest") != std::string::npos) {
        ch.isDigest = true;
        ch.realm  = extractQuoteValue(header, "realm");
        ch.nonce  = extractQuoteValue(header, "nonce");
        ch.opaque = extractQuoteValue(header, "opaque");
        auto qopPos = header.find("qop=\"");
        if (qopPos != std::string::npos) {
            std::string q = extractQuoteValue(header, "qop");
            if (q.find("auth") != std::string::npos) ch.qop = "auth";
        } else if (header.find("qop=auth") != std::string::npos) {
            ch.qop = "auth";
        }
    } else if (header.find("Basic") != std::string::npos) {
        ch.isBasic = true;
    }
    return ch;
}

static std::string digestAuthorizationHeader(const std::string& method,
                                             const std::string& uri,
                                             const std::string& user,
                                             const std::string& pass,
                                             const AuthChallenge& ch) {
    std::string ha1 = crypto::md5Hex(user + ":" + ch.realm + ":" + pass);
    std::string ha2 = crypto::md5Hex(method + ":" + uri);
    std::string resp;
    std::string header = "Digest username=\"" + user + "\", realm=\"" + ch.realm +
                         "\", nonce=\"" + ch.nonce + "\", uri=\"" + uri + "\"";
    if (!ch.qop.empty()) {
        std::string cnonce = crypto::md5Hex(std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))
                                 .substr(0, 16);
        resp = crypto::md5Hex(ha1 + ":" + ch.nonce + ":00000001:" + cnonce + ":" +
                              ch.qop + ":" + ha2);
        header += ", qop=" + ch.qop + ", nc=00000001, cnonce=\"" + cnonce + "\"";
    } else {
        resp = crypto::md5Hex(ha1 + ":" + ch.nonce + ":" + ha2);
    }
    header += ", response=\"" + resp + "\"";
    if (!ch.opaque.empty()) header += ", opaque=\"" + ch.opaque + "\"";
    return header;
}

static std::vector<std::string> getVideoDevicePaths() {
    std::vector<std::string> paths;
    try {
        if (!fs::exists("/dev")) return paths;
        for (const auto& entry : fs::directory_iterator("/dev")) {
            std::string name = entry.path().filename().string();
            if (name.compare(0, 5, "video") == 0)
                paths.push_back(entry.path().string());
        }
        std::sort(paths.begin(), paths.end());
    } catch (...) {}
    return paths;
}

static bool isRealVideoDevice(int fd) {
    v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) return false;
    uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    return (caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
}

std::vector<DeviceInfo> CameraDiscovery::discoverLocal() {
    std::vector<DeviceInfo> result;
    for (const auto& path : getVideoDevicePaths()) {
        int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
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
            try { info.index = std::stoi(path.substr(10)); } catch (...) { info.index = -1; }
            result.push_back(info);
        }
        close(fd);
    }
    return result;
}

bool CameraDiscovery::isPortOpen(const std::string& ip, int port, int timeoutMs) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

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

// Sends a GetCapabilities SOAP request to `path` on ip:port.
// Returns HTTP status line + body start (may be empty on failure).
static std::string onvifSoapProbe(const std::string& ip, int port,
                                  const std::string& path, int timeoutMs) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return {};

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
    if (select(fd + 1, nullptr, &fdset, nullptr, &tv) != 1) {
        close(fd);
        return {};
    }

    std::string soapBody =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\">"
        "<s:Body>"
        "<tds:GetCapabilities>"
        "<tds:Category>All</tds:Category>"
        "</tds:GetCapabilities>"
        "</s:Body>"
        "</s:Envelope>";

    std::string request =
        "POST " + path + " HTTP/1.1\r\n"
        "Host: " + ip + ":" + std::to_string(port) + "\r\n"
        "Content-Type: application/soap+xml; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(soapBody.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        soapBody;

    fcntl(fd, F_SETFL, 0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (write(fd, request.data(), request.size()) <= 0) {
        close(fd);
        return {};
    }

    std::string response;
    char buf[4096];
    while (response.size() < 65536) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        response.append(buf, n);
        if (response.find("</s:Envelope>") != std::string::npos ||
            response.find("</SOAP-ENV:Envelope>") != std::string::npos)
            break;
    }
    close(fd);
    return response;
}

bool CameraDiscovery::isOnvifEndpoint(const std::string& ip, int port, int timeoutMs) {
    static const char* kPaths[] = {"/onvif/device_service", "/onvif/device", "/device_service"};
    for (const char* p : kPaths) {
        std::string resp = onvifSoapProbe(ip, port, p, timeoutMs);
        if (resp.empty()) continue;
        // SOAP response (even an auth fault) or HTTP 401 => ONVIF-capable device
        if (resp.find("Envelope") != std::string::npos ||
            resp.find("onvif.org") != std::string::npos ||
            resp.find("401") == 0 ||
            resp.find("HTTP/1.") == 0 && resp.find("401") != std::string::npos)
            return true;
    }
    return false;
}

void CameraDiscovery::embedCredentials(DeviceInfo& info) {
    if (info.username.empty()) return;
    auto embed = [&](std::string& uri) {
        auto pos = uri.find("://");
        if (pos == std::string::npos) return;
        if (uri.find('@') != std::string::npos &&
            uri.find('@') < uri.find('/', pos + 3))
            return; // already has credentials
        uri.insert(pos + 3, info.username + ":" + info.password + "@");
    };
    embed(info.streamUri);
    for (auto& p : info.mediaProfiles)
        embed(p.streamUri);
}

bool CameraDiscovery::tryPopulateOnvifWithCreds(DeviceInfo& info) {
    // Called only when the unauthenticated attempt failed → device needs auth.
    info.requiresAuth = true;
    for (auto& creds : kCommonCreds) {
        info.username = creds[0];
        info.password = creds[1];
        if (populateOnvifMetadata(info)) {
            std::cout << "      [auth] ONVIF " << info.devicePath
                      << " accepted " << info.username << ":"
                      << (info.password.empty() ? "<empty>" : info.password)
                      << std::endl;
            embedCredentials(info);
            return true;
        }
    }
    info.username.clear();
    info.password.clear();
    return false;
}

bool CameraDiscovery::populateOnvifMetadata(DeviceInfo& info) {
    try {
        onvif::OnvifClient client(info.devicePath, info.onvifPort,
                                  info.username, info.password);

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
        // If we got neither device info nor any profile, the device most
        // likely rejected us (auth) or is not really ONVIF → report failure
        // so the caller can retry with credentials.
        return !info.mediaProfiles.empty() || !info.manufacturer.empty() ||
               !info.model.empty() || !info.hardwareId.empty();
    } catch (const std::exception& e) {
        std::cerr << "  ONVIF metadata failed for " << info.devicePath << ": " << e.what() << std::endl;
        return false;
    }
}

bool CameraDiscovery::probeIp(const std::string& ip, int port, DeviceInfo& outInfo) {
    if (!isOnvifEndpoint(ip, port, 1500)) return false;

    outInfo.devicePath = ip;
    outInfo.type = ConnectionType::ONVIF;
    outInfo.onvifPort = port;
    outInfo.cardName = "ONVIF Device (" + ip + ")";

    if (!populateOnvifMetadata(outInfo))
        tryPopulateOnvifWithCreds(outInfo);

    if (!outInfo.mediaProfiles.empty() && !outInfo.mediaProfiles[0].streamUri.empty())
        outInfo.streamUri = outInfo.mediaProfiles[0].streamUri;

    return true;
}

std::vector<DeviceInfo> CameraDiscovery::probeOnvifEndpoints(
    const std::vector<std::string>& ips, int port, int timeoutMs, int maxThreads) {

    std::vector<DeviceInfo> results;
    std::mutex resultsMutex;
    std::atomic<int> nextIdx{0};
    int totalIps = static_cast<int>(ips.size());

    auto worker = [&]() {
        while (true) {
            int idx = nextIdx.fetch_add(1);
            if (idx >= totalIps) break;

            if (!isPortOpen(ips[idx], port, timeoutMs)) continue;

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
    "/stream1", "/stream2", "/stream3",
    "/ch1/main/av_stream", "/ch2/main/av_stream",
    "/cam/realmonitor", "/cam/realmonitor?channel=1&subtype=0",
    "/video1", "/video2", "/video3",
    "/h264", "/h264ESVideoTrack",
    "/h264_pcm.sdp", "/h264_ulaw.sdp",   // Android "IP Webcam" RTSP endpoints
    "/mpeg4", "/mpeg4media",
    "/media",
    "/video",
    "/user=admin_password=_channel=1_stream=0.sdp",
    "/1", "/2", "/3", "/11",
    "/live/ch0", "/live/ch1",
    "/live/main", "/live/sub",
    "/onvif-media", "/onvif1", "/onvif2",
    "/Streaming/Channels/101", "/Streaming/Channels/102",
    "/av0", "/av1",
    "/live0", "/live1",
    "/main", "/sub", "/profile1/media.smp", "/media/video1",
    "/H264?channel=1&subtype=0",
    "/h264/ch1/main/av_stream",
};

// RTSP ports commonly used by IP cameras and phone camera apps
static const int kRtspPorts[] = {554, 8554, 10554};
// ONVIF HTTP ports commonly used
static const int kOnvifPorts[] = {80, 8080, 8000, 8081, 8899};
// Ports worth probing for HTTP/MJPEG streams (e.g. phone camera apps)
static const int kHttpStreamPorts[] = {8080, 8081, 4747, 8000};

std::vector<std::string> CameraDiscovery::probeRtspPaths(const std::string& ip, int port, int timeoutMs) {
    std::vector<std::string> found;
    for (const auto& rtspPath : kCommonRtspPaths) {
        std::string url = "rtsp://" + ip + ":" + std::to_string(port) + rtspPath;
        std::string result = tryRtspDescribe(url, timeoutMs);
        if (!result.empty()) {
            found.push_back(result);
            return found;
        }
    }
    return found;
}

std::vector<std::string> CameraDiscovery::probeRtspPaths(const std::string& ip, int timeoutMs) {
    for (int port : kRtspPorts) {
        if (!isPortOpen(ip, port, timeoutMs)) continue;
        auto found = probeRtspPaths(ip, port, timeoutMs);
        if (!found.empty()) return found;
    }
    return {};
}

// Detects HTTP video streams used by phone camera apps (IP Webcam, DroidCam, ...)
// Returns a full stream URL or empty string.
std::string CameraDiscovery::detectHttpStream(const std::string& ip, int port, int timeoutMs) {
    static const char* kCandidates[] = {
        "/video",       // IP Webcam (MJPEG)
        "/videofeed",   // IP Webcam alt
        "/mjpegfeed",   // DroidCam
        "/cam.mjpg",
        "/mjpeg",
    };

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return {};

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    for (const char* path : kCandidates) {
        close(fd);
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return {};

        fcntl(fd, F_SETFL, O_NONBLOCK);
        connect(fd, (struct sockaddr*)&addr, sizeof(addr));

        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(fd, &fdset);
        if (select(fd + 1, nullptr, &fdset, nullptr, &tv) != 1) continue;

        std::string req =
            std::string("GET ") + path + " HTTP/1.1\r\n"
            "Host: " + ip + ":" + std::to_string(port) + "\r\n"
            "Connection: close\r\n\r\n";

        fcntl(fd, F_SETFL, 0);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (write(fd, req.data(), req.size()) <= 0) continue;

        char buf[2048];
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';
        std::string head(buf, n);

        bool ok200 = head.find(" 200") != std::string::npos;
        bool mjpeg = head.find("multipart/x-mixed-replace") != std::string::npos ||
                     head.find("image/jpeg") != std::string::npos;
        if (ok200 && mjpeg) {
            close(fd);
            return "http://" + ip + ":" + std::to_string(port) + path;
        }
    }
    close(fd);
    return {};
}

// Send one RTSP DESCRIBE, optionally with an Authorization header.
// Returns the full response text, or empty string on connection failure.
static std::string rtspDescribeExchange(const struct sockaddr_in& addr,
                                        const std::string& url,
                                        const std::string& authHeader,
                                        int timeoutMs) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return {};

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

    std::string req = "DESCRIBE " + url + " RTSP/1.0\r\nCSeq: 1\r\n";
    if (!authHeader.empty())
        req += "Authorization: " + authHeader + "\r\n";
    req += "Accept: application/sdp\r\nConnection: close\r\n\r\n";

    fcntl(fd, F_SETFL, 0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (write(fd, req.data(), req.size()) <= 0) {
        close(fd);
        return {};
    }

    // Read only until end of RTSP header (we don't need the whole SDP)
    std::string response;
    char buf[4096];
    while (response.find("\r\n\r\n") == std::string::npos &&
           response.size() < 16384) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        response.append(buf, n);
    }
    close(fd);
    return response;
}

std::string CameraDiscovery::tryRtspDescribe(const std::string& url, int timeoutMs) {
    std::string host = url;
    std::string path = "/live";
    std::string rtspPrefix = "rtsp://";
    int port = 554;

    if (url.find(rtspPrefix) == 0) {
        std::string rest = url.substr(rtspPrefix.size());
        auto slashPos = rest.find('/');
        auto hostPart = (slashPos != std::string::npos) ? rest.substr(0, slashPos) : rest;
        path = (slashPos != std::string::npos) ? rest.substr(slashPos) : "/";

        auto atPos = hostPart.find('@');
        if (atPos != std::string::npos)
            hostPart = hostPart.substr(atPos + 1);

        auto colonPos = hostPart.find(':');
        if (colonPos != std::string::npos) {
            host = hostPart.substr(0, colonPos);
            try { port = std::stoi(hostPart.substr(colonPos + 1)); } catch (...) {}
        } else {
            host = hostPart;
        }
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    // ---- 1) try without credentials ----
    std::string response = rtspDescribeExchange(addr, url, "", timeoutMs);
    if (response.find("RTSP/1.0 200") != std::string::npos ||
        response.find("RTSP/2.0 200") != std::string::npos) {
        return url;
    }

    // ---- 2) only if device demands auth (401): try common credentials ----
    if (response.find("401") == std::string::npos) return {};

    AuthChallenge challenge = parseWwwAuthenticate(response);

    for (auto& creds : kCommonCreds) {
        std::string user = creds[0], pass = creds[1];
        std::string authHeader;

        if (challenge.isDigest)
            authHeader = digestAuthorizationHeader("DESCRIBE", url, user, pass, challenge);
        else
            authHeader = "Basic " + base64Encode(user + ":" + pass);

        std::string authed = rtspDescribeExchange(addr, url, authHeader, timeoutMs);
        if (authed.find("RTSP/1.0 200") != std::string::npos ||
            authed.find("RTSP/2.0 200") != std::string::npos) {
            std::cout << "      [auth] " << host << ":" << port
                      << " accepted " << user << ":" << (pass.empty() ? "<empty>" : pass)
                      << (challenge.isDigest ? " (digest)" : " (basic)") << std::endl;
            return rtspPrefix + user + ":" + pass + "@" + host + ":" +
                   std::to_string(port) + path;
        }
        // If server answered without new 401, no point retrying others
        if (authed.empty()) break;
    }

    return {};
}

// ─── Connection test (used by the UI before adding a camera) ───────────────

static std::string httpGetExchange(const struct sockaddr_in& addr, int port,
                                   const std::string& path,
                                   const std::string& authHeader, int timeoutMs) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return {};

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

    std::string req = "GET " + path + " HTTP/1.1\r\n"
                      "Host: " + std::string(inet_ntoa(addr.sin_addr)) + ":" +
                      std::to_string(port) + "\r\n";
    if (!authHeader.empty())
        req += "Authorization: " + authHeader + "\r\n";
    req += "Connection: close\r\n\r\n";

    fcntl(fd, F_SETFL, 0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (write(fd, req.data(), req.size()) <= 0) {
        close(fd);
        return {};
    }

    std::string response;
    char buf[4096];
    while (response.find("\r\n\r\n") == std::string::npos &&
           response.size() < 16384) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        response.append(buf, n);
    }
    close(fd);
    return response;
}

static bool isHttp200(const std::string& resp) {
    return resp.find("HTTP/1.1 200") != std::string::npos ||
           resp.find("HTTP/1.0 200") != std::string::npos;
}

CameraDiscovery::TestResult CameraDiscovery::testConnection(
    const std::string& uri, const std::string& username,
    const std::string& password) {

    TestResult r;

    // ── RTSP endpoint ──
    if (uri.rfind("rtsp://", 0) == 0) {
        std::string rest = uri.substr(7);
        auto slashPos = rest.find('/');
        std::string hostPort = (slashPos != std::string::npos) ? rest.substr(0, slashPos) : rest;
        std::string path = (slashPos != std::string::npos) ? rest.substr(slashPos) : "/";

        auto atPos = hostPort.find('@');
        if (atPos != std::string::npos) hostPort = hostPort.substr(atPos + 1);

        std::string host = hostPort;
        int port = 554;
        auto colonPos = hostPort.find(':');
        if (colonPos != std::string::npos) {
            host = hostPort.substr(0, colonPos);
            try { port = std::stoi(hostPort.substr(colonPos + 1)); } catch (...) {}
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

        std::string bareUrl = "rtsp://" + host + ":" + std::to_string(port) + path;

        std::string resp = rtspDescribeExchange(addr, bareUrl, "", 3000);
        if (resp.empty()) {
            r.message = "به دوربین دسترسی نشد (عدم دسترسی به شبکه یا پورت بسته)";
            return r;
        }
        if (resp.find("401") != std::string::npos) {
            r.requiresAuth = true;
            if (username.empty()) {
                r.message = "این دوربین به نام کاربری و رمز عبور نیاز دارد";
                return r;
            }
            AuthChallenge ch = parseWwwAuthenticate(resp);
            std::string authHeader = ch.isDigest
                ? digestAuthorizationHeader("DESCRIBE", bareUrl, username, password, ch)
                : "Basic " + base64Encode(username + ":" + password);
            std::string resp2 = rtspDescribeExchange(addr, bareUrl, authHeader, 3000);
            if (resp2.find("200") != std::string::npos) {
                r.ok = true;
                r.message = "اتصال موفق با احراز هویت";
                return r;
            }
            r.message = "نام کاربری یا رمز عبور اشتباه است";
            return r;
        }
        if (resp.find("200") != std::string::npos) {
            r.ok = true;
            r.message = "اتصال موفق";
            return r;
        }
        r.message = "پاسخ نامعتبر از دوربین";
        return r;
    }

    // ── HTTP (MJPEG) endpoint ──
    if (uri.rfind("http://", 0) == 0) {
        std::string rest = uri.substr(7);
        auto slashPos = rest.find('/');
        std::string hostPort = (slashPos != std::string::npos) ? rest.substr(0, slashPos) : rest;
        std::string path = (slashPos != std::string::npos) ? rest.substr(slashPos) : "/";

        std::string host = hostPort;
        int port = 80;
        auto colonPos = hostPort.find(':');
        if (colonPos != std::string::npos) {
            host = hostPort.substr(0, colonPos);
            try { port = std::stoi(hostPort.substr(colonPos + 1)); } catch (...) {}
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

        std::string resp = httpGetExchange(addr, port, path, "", 3000);
        if (resp.empty()) {
            r.message = "به دوربین دسترسی نشد";
            return r;
        }
        if (resp.find("401") != std::string::npos) {
            r.requiresAuth = true;
            if (username.empty()) {
                r.message = "این دوربین به نام کاربری و رمز عبور نیاز دارد";
                return r;
            }
            AuthChallenge ch = parseWwwAuthenticate(resp);
            std::string authHeader = ch.isDigest
                ? digestAuthorizationHeader("GET", path, username, password, ch)
                : "Basic " + base64Encode(username + ":" + password);
            std::string resp2 = httpGetExchange(addr, port, path, authHeader, 3000);
            if (isHttp200(resp2)) {
                r.ok = true;
                r.message = "اتصال موفق با احراز هویت";
                return r;
            }
            r.message = "نام کاربری یا رمز عبور اشتباه است";
            return r;
        }
        if (isHttp200(resp)) {
            r.ok = true;
            r.message = "اتصال موفق";
            return r;
        }
        r.message = "پاسخ نامعتبر از دوربین";
        return r;
    }

    // ── ONVIF / host endpoint (no scheme or http without path) ──
    std::string host = uri;
    int port = 80;
    if (host.rfind("http://", 0) == 0) host = host.substr(7);
    auto slashPos = host.find('/');
    if (slashPos != std::string::npos) host = host.substr(0, slashPos);
    auto colonPos = host.find(':');
    if (colonPos != std::string::npos) {
        try { port = std::stoi(host.substr(colonPos + 1)); } catch (...) {}
        host = host.substr(0, colonPos);
    }

    if (!isOnvifEndpoint(host, port, 2500)) {
        r.message = "دستگاه ONVIF در این آدرس پیدا نشد";
        return r;
    }

    // No credentials: quick check whether the device demands auth
    {
        onvif::OnvifClient client(host, port);
        auto full = client.discoverAll();
        if (!full.device.manufacturer.empty() || !full.profiles.empty()) {
            r.ok = true;
            r.message = "اتصال موفق";
            return r;
        }
    }

    if (username.empty()) {
        r.requiresAuth = true;
        r.message = "این دوربین به نام کاربری و رمز عبور نیاز دارد";
        return r;
    }

    onvif::OnvifClient client(host, port, username, password);
    auto full = client.discoverAll();
    if (!full.device.manufacturer.empty() || !full.profiles.empty()) {
        r.ok = true;
        r.requiresAuth = true;
        r.message = "اتصال موفق با احراز هویت";
        return r;
    }

    r.requiresAuth = true;
    r.message = "نام کاربری یا رمز عبور اشتباه است";
    return r;
}

std::vector<std::string> CameraDiscovery::generateIpRange(const std::string& subnet) {
    std::vector<std::string> ips;
    ips.reserve(254);
    for (int i = 1; i <= 254; ++i)
        ips.push_back(subnet + "." + std::to_string(i));
    return ips;
}

std::vector<std::string> CameraDiscovery::generateCommonSubnets() {
    return {
        "192.168.0", "192.168.1", "192.168.2", "192.168.10",
        "10.0.0", "10.0.1",
        "172.16.0", "172.17.0"
    };
}

bool CameraDiscovery::isIpInAnySubnet(const std::string& ip, const std::vector<std::string>& subnets) {
    for (const auto& sn : subnets) {
        if (ip.compare(0, sn.length(), sn) == 0) return true;
    }
    return false;
}

// Maximum number of /24 blocks scanned per network so huge interfaces
// (e.g. docker bridges with /16) don't blow up the scan time.
constexpr uint32_t kMaxScanBlocksPerNet = 8;

// Adds the /24 blocks contained in `network`/`mask` (bounded by
// kMaxScanBlocksPerNet) as "a.b.c" prefixes.
static void addScanBlocksForNetwork(std::set<std::string>& subnets,
                                    uint32_t network, uint32_t mask) {
    uint32_t netSize = (mask == 0) ? (1u << 31) : (~mask + 1);  // power of two
    uint32_t blocks = netSize / 256;                            // /24 blocks
    if (blocks < 1) blocks = 1;
    if (blocks > kMaxScanBlocksPerNet) blocks = kMaxScanBlocksPerNet;

    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t base = network + i * 256;
        char buf[32];
        snprintf(buf, sizeof(buf), "%u.%u.%u",
                 (base >> 24) & 0xFF, (base >> 16) & 0xFF, (base >> 8) & 0xFF);
        subnets.insert(buf);
    }
}

// Extra subnets to scan, taken from the CAMERA_SCAN_SUBNETS environment
// variable (comma-separated). This is mainly for containers running on a
// Docker bridge network, where the real LAN is not visible through the
// container's own interfaces. Accepts "192.168.1", "192.168.1.0/24",
// "10.142.76.0/22" or a host address like "192.168.1.5".
static void addEnvScanSubnets(std::set<std::string>& subnets) {
    const char* env = std::getenv("CAMERA_SCAN_SUBNETS");
    if (env == nullptr || *env == '\0') return;

    std::stringstream ss(env);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item.erase(std::remove_if(item.begin(), item.end(),
                                  [](unsigned char c) { return std::isspace(c); }),
                   item.end());
        if (item.empty()) continue;

        uint32_t maskBits = 24;
        auto slash = item.find('/');
        std::string addrPart = item;
        if (slash != std::string::npos) {
            try { maskBits = std::stoul(item.substr(slash + 1)); }
            catch (...) { continue; }
            if (maskBits > 24) maskBits = 24;  // smaller nets: scan their /24
            addrPart = item.substr(0, slash);
        }

        // inet_aton accepts partial addresses: "192.168.1" → 192.168.1.0
        struct in_addr a{};
        if (inet_aton(addrPart.c_str(), &a) == 0) continue;

        uint32_t mask = (maskBits == 0) ? 0 : (0xFFFFFFFFu << (32 - maskBits));
        addScanBlocksForNetwork(subnets, ntohl(a.s_addr) & mask, mask);
    }
}

static std::vector<std::string> getLocalSubnets() {
    std::set<std::string> subnets;
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) < 0) return {};

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        // Skip local virtual bridge interfaces (docker0, docker's "br-<id>"
        // user bridges, veth pairs, libvirt's virbr*) — they only contain
        // containers/VMs, never the physical cameras we are looking for, and
        // scanning their large internal ranges wastes a lot of time. A real
        // LAN bridge named "br0" does NOT match (docker names contain a dash).
        const char* ifname = ifa->ifa_name;
        if (strncmp(ifname, "docker", 6) == 0 || strncmp(ifname, "br-", 3) == 0 ||
            strncmp(ifname, "veth", 4) == 0 || strncmp(ifname, "virbr", 5) == 0)
            continue;

        struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
        struct sockaddr_in* mask = (struct sockaddr_in*)ifa->ifa_netmask;

        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        uint32_t netmask = ntohl(mask->sin_addr.s_addr);
        uint32_t network = ip & netmask;

        uint8_t a = (network >> 24) & 0xFF;
        uint8_t b = (network >> 16) & 0xFF;
        if (a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168)) {
            // Scan the whole masked network, not just the first /24 —
            // VPNs are often wider than /24 (e.g. /22).
            addScanBlocksForNetwork(subnets, network, netmask);
        }
    }
    freeifaddrs(ifaddr);
    addEnvScanSubnets(subnets);
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
            if (!seenIps.insert(d.ip).second) continue;
            std::cout << "    Found ONVIF device: " << d.ip
                      << " (" << d.name << ")" << std::endl;

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
                std::thread t([&]() {
                    bool ok = populateOnvifMetadata(info);
                    if (!ok) ok = tryPopulateOnvifWithCreds(info);
                    p.set_value(ok);
                });
                if (f.wait_for(std::chrono::seconds(10)) == std::future_status::timeout) {
                    t.detach();
                    std::cout << "      Metadata fetch timed out" << std::endl;
                } else {
                    t.join();
                    if (f.get() && !info.mediaProfiles.empty()
                        && !info.mediaProfiles[0].streamUri.empty()) {
                        info.streamUri = info.mediaProfiles[0].streamUri;
                        std::cout << "      Stream: " << info.streamUri << std::endl;
                    }
                }

                results.push_back(std::move(info));
            }
        }
    }

    // Phase 2 (optional): deep search — scan local subnets for
    // RTSP (554/8554/10554), ONVIF HTTP (80/8080/8000/8081/8899) and
    // HTTP/MJPEG streams used by phone camera apps (8080/8081/4747/8000).
    if (deepSearch) {
        std::cout << "  Phase 2: deep subnet scan (RTSP + ONVIF + HTTP/MJPEG)..."
                  << std::endl;
        auto localSubnets = getLocalSubnets();
        if (localSubnets.empty()) localSubnets = {"192.168.1"};
        {
            std::string subnetList;
            for (const auto& sn : localSubnets) {
                if (!subnetList.empty()) subnetList += ", ";
                subnetList += sn + ".x";
            }
            std::cout << "    Scanning subnets: " << subnetList << std::endl;
        }

        std::vector<std::string> allIps;
        for (const auto& sn : localSubnets)
            for (int i = 1; i <= 254; ++i)
                allIps.push_back(sn + "." + std::to_string(i));

        // --- Step 1: fast port scan of all candidate ports ---
        struct HostPorts {
            std::string ip;
            std::set<int> openPorts;
        };
        std::vector<HostPorts> liveHosts;
        std::mutex liveMutex;
        std::atomic<int> scanIdx{0};

        std::vector<int> probePorts;
        for (int p : kRtspPorts) probePorts.push_back(p);
        for (int p : kOnvifPorts) probePorts.push_back(p);
        for (int p : kHttpStreamPorts)
            if (std::find(probePorts.begin(), probePorts.end(), p) == probePorts.end())
                probePorts.push_back(p);

        auto scanWorker = [&]() {
            while (true) {
                int i = scanIdx.fetch_add(1);
                if (i >= (int)allIps.size()) break;
                // Own host: skip nothing; keep discovery simple
                HostPorts hp{allIps[i], {}};
                for (int p : probePorts)
                    if (CameraDiscovery::isPortOpen(allIps[i], p, 250))
                        hp.openPorts.insert(p);
                if (!hp.openPorts.empty()) {
                    std::lock_guard<std::mutex> l(liveMutex);
                    liveHosts.push_back(std::move(hp));
                }
            }
        };

        int nThreads = std::min(80, (int)allIps.size());
        {
            std::vector<std::thread> workers;
            for (int i = 0; i < nThreads; ++i) workers.emplace_back(scanWorker);
            for (auto& t : workers) t.join();
        }

        std::cout << "    Found " << liveHosts.size()
                  << " live host(s) with camera-like ports" << std::endl;

        // --- Step 2: probe each live host (ONVIF → RTSP → HTTP/MJPEG) ---
        std::atomic<int> probeIdx{0};
        std::mutex resultMutex;
        int pThreads = std::min(12, std::max(1, (int)liveHosts.size()));

        auto probeWorker = [&]() {
            while (true) {
                int i = probeIdx.fetch_add(1);
                if (i >= (int)liveHosts.size()) break;
                const auto& hp = liveHosts[i];
                if (seenIps.count(hp.ip)) continue;

                // 2a) ONVIF endpoints
                bool found = false;
                for (int p : kOnvifPorts) {
                    if (!hp.openPorts.count(p)) continue;
                    DeviceInfo info;
                    if (probeIp(hp.ip, p, info)) {
                        info.xaddrs = "http://" + hp.ip + ":" +
                                      std::to_string(p) + "/onvif/device_service";
                        std::lock_guard<std::mutex> l(resultMutex);
                        results.push_back(std::move(info));
                        found = true;
                        break;
                    }
                }
                if (found) continue;

                // 2b) RTSP endpoints
                for (int p : kRtspPorts) {
                    if (!hp.openPorts.count(p)) continue;
                    auto paths = probeRtspPaths(hp.ip, p, 900);
                    if (!paths.empty()) {
                        DeviceInfo info;
                        info.devicePath = hp.ip;
                        info.streamUri = paths[0];
                        info.cardName = "RTSP Camera (" + hp.ip + ")";
                        info.type = ConnectionType::RTSP;
                        info.requiresAuth = (paths[0].find('@') != std::string::npos);
                        std::lock_guard<std::mutex> l(resultMutex);
                        results.push_back(std::move(info));
                        found = true;
                        break;
                    }
                }
                if (found) continue;

                // 2c) HTTP/MJPEG stream (phone camera apps, webcams, ...)
                for (int p : kHttpStreamPorts) {
                    if (!hp.openPorts.count(p)) continue;
                    std::string uri = detectHttpStream(hp.ip, p, 900);
                    if (!uri.empty()) {
                        DeviceInfo info;
                        info.devicePath = hp.ip;
                        info.streamUri = uri;
                        info.cardName = "HTTP/MJPEG Camera (" + hp.ip + ")";
                        info.type = ConnectionType::RTSP;
                        info.requiresAuth =
                            (uri.rfind("http://", 0) == 0 &&
                             uri.find('@') != std::string::npos);
                        std::lock_guard<std::mutex> l(resultMutex);
                        results.push_back(std::move(info));
                        found = true;
                        break;
                    }
                }
            }
        };

        {
            std::vector<std::thread> pWorkers;
            for (int i = 0; i < pThreads; ++i) pWorkers.emplace_back(probeWorker);
            for (auto& t : pWorkers) t.join();
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
