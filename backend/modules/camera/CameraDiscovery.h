#ifndef CAMERA_DISCOVERY_H
#define CAMERA_DISCOVERY_H

#include <string>
#include <vector>
#include <set>
#include <cstdint>

namespace camera {

enum class ConnectionType {
    LOCAL,
    RTSP,
    ONVIF
};

struct MediaProfileInfo {
    std::string token;
    std::string name;
    int width{0};
    int height{0};
    int fps{0};
    std::string encoding;
    std::string streamUri;
};

struct DeviceInfo {
    int index{-1};
    std::string devicePath;
    std::string streamUri;
    std::string cardName{"Unknown"};
    std::string driver;
    std::string busInfo;
    ConnectionType type{ConnectionType::LOCAL};
    bool isVideoCapture{false};

    // ONVIF-specific fields
    std::string xaddrs;
    std::vector<std::string> scopes;
    std::string manufacturer;
    std::string model;
    std::string firmwareVersion;
    std::string serialNumber;
    std::string hardwareId;
    std::string onvifDeviceServiceUrl;
    std::string onvifMediaServiceUrl;
    int onvifPort{80};
    std::vector<MediaProfileInfo> mediaProfiles;

    int preferredFps{0};
    int preferredWidth{0};
    int preferredHeight{0};
    std::string transportProtocol{"tcp"};

    // Credentials discovered automatically when the device requires auth
    bool requiresAuth{false};
    std::string username;
    std::string password;
};

class CameraDiscovery {
public:
    struct TestResult {
        bool ok{false};
        bool requiresAuth{false};
        std::string message;
    };

    static std::vector<DeviceInfo> discoverAll(bool deepSearch);
    static std::vector<DeviceInfo> discoverLocal();
    static std::vector<DeviceInfo> discoverOnvif(int timeoutMs = 5000, bool deepSearch = false);
    static bool probeIp(const std::string& ip, int port, DeviceInfo& outInfo);
    static std::string tryRtspDescribe(const std::string& url, int timeoutMs);

    // Verifies a stream/endpoint can be reached. If the device answers 401 and
    // no credentials are given (or they are wrong) → requiresAuth = true.
    static TestResult testConnection(const std::string& uri,
                                     const std::string& username = "",
                                     const std::string& password = "");

private:
    static std::vector<DeviceInfo> probeOnvifEndpoints(
        const std::vector<std::string>& ips,
        int port,
        int timeoutMs,
        int maxThreads = 30);
    static bool isOnvifEndpoint(const std::string& ip, int port, int timeoutMs);
    static bool populateOnvifMetadata(DeviceInfo& info);
    static std::vector<std::string> probeRtspPaths(const std::string& ip, int timeoutMs);
    static std::vector<std::string> probeRtspPaths(const std::string& ip, int port, int timeoutMs);
    static std::string detectHttpStream(const std::string& ip, int port, int timeoutMs);
    static void embedCredentials(DeviceInfo& info);
    static bool tryPopulateOnvifWithCreds(DeviceInfo& info);
    static bool isPortOpen(const std::string& ip, int port, int timeoutMs);
    static std::vector<std::string> generateIpRange(const std::string& subnet);
    static std::vector<std::string> generateCommonSubnets();
    static bool isIpInAnySubnet(const std::string& ip, const std::vector<std::string>& subnets);
};

} // namespace camera

#endif // CAMERA_DISCOVERY_H
