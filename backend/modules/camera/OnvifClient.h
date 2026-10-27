#ifndef ONVIF_CLIENT_H
#define ONVIF_CLIENT_H

#include <string>
#include <vector>
#include <cstdint>

namespace onvif {

struct MediaProfile {
    std::string token;
    std::string name;
    int width{0};
    int height{0};
    int fps{0};
    std::string encoding;
    std::string streamUri;
};

struct DeviceInfo {
    std::string manufacturer;
    std::string model;
    std::string firmwareVersion;
    std::string serialNumber;
    std::string hardwareId;
};

struct Capabilities {
    std::string deviceServiceUrl;
    std::string mediaServiceUrl;
    std::string ptzServiceUrl;
    std::string imagingServiceUrl;
    std::string eventsServiceUrl;
};

struct FullDeviceInfo {
    DeviceInfo device;
    Capabilities caps;
    std::vector<MediaProfile> profiles;
};

class OnvifClient {
public:
    OnvifClient(const std::string& host, int port = 80,
                const std::string& username = "",
                const std::string& password = "");

    DeviceInfo getDeviceInformation();
    Capabilities getCapabilities();
    std::vector<MediaProfile> getProfiles(const std::string& mediaUrl);
    std::string getStreamUri(const std::string& mediaUrl, const std::string& profileToken);
    std::string getStreamUri(const std::string& mediaUrl, const std::string& profileToken,
                              const std::string& transportProtocol);

    // Snapshot (JPEG) URI from media service
    std::string getSnapshotUri(const std::string& mediaUrl, const std::string& profileToken);

    // Change encoding parameters on the camera itself
    bool setVideoEncoderConfiguration(const std::string& mediaUrl,
                                       const std::string& configToken,
                                       int width, int height, int fps,
                                       const std::string& encoding = "H264");

    // Full chain: GetCapabilities → GetProfiles → GetStreamUri (all profiles)
    std::vector<MediaProfile> getProfilesWithStreams(const std::string& mediaUrl);
    FullDeviceInfo discoverAll();

    static std::string buildDeviceServiceUrl(const std::string& host, int port);
    static std::string buildMediaServiceUrl(const std::string& host, int port);

private:
    std::string host_;
    int port_;
    std::string username_;
    std::string password_;

    struct HttpResponse {
        int statusCode{0};
        std::string body;
    };

    HttpResponse soapRequest(const std::string& url, const std::string& soapXml);
    std::string buildEndpointUrl(const std::string& url) const;
    std::string httpPost(const std::string& endpoint, const std::string& soapXml);

    std::string buildSoapEnvelope(const std::string& extraNs, const std::string& bodyXml) const;
    std::string buildWsseHeader() const;

    static std::string base64Encode(const std::string& input);
    static std::string base64Decode(const std::string& input);
};

} // namespace onvif

#endif // ONVIF_CLIENT_H
