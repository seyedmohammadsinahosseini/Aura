#include "OnvifClient.h"
#include "core/crypto/Sha1.h"
#include "core/xml/XmlParser.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <random>
#include <sstream>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace onvif {

OnvifClient::OnvifClient(const std::string& host, int port, const std::string& username,
                         const std::string& password)
    : host_(host), port_(port), username_(username), password_(password) {}

std::string OnvifClient::buildDeviceServiceUrl(const std::string& host, int port) {
    return "http://" + host + ":" + std::to_string(port) + "/onvif/device_service";
}

std::string OnvifClient::buildMediaServiceUrl(const std::string& host, int port) {
    return "http://" + host + ":" + std::to_string(port) + "/onvif/media_service";
}

std::string OnvifClient::buildEndpointUrl(const std::string& url) const {
    if (url.find("http://") == 0 || url.find("https://") == 0)
        return url;
    if (url.find("/") == 0)
        return "http://" + host_ + ":" + std::to_string(port_) + url;
    return "http://" + host_ + ":" + std::to_string(port_) + "/" + url;
}

// ─── Low-level HTTP helpers ────────────────────────────────────────────────

static int connectToHost(const std::string& host, int port) {
    struct addrinfo hints, *res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (auto* rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;
        struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

static int recvAll(int fd, std::string& out) {
    char buf[4096];
    int total = 0;
    while (true) {
        int n = static_cast<int>(read(fd, buf, sizeof(buf)));
        if (n <= 0)
            break;
        out.append(buf, n);
        total += n;
    }
    return total;
}

// ─── Base64 ────────────────────────────────────────────────────────────────

std::string OnvifClient::base64Encode(const std::string& input) {
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "abcdefghijklmnopqrstuvwxyz"
                             "0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

std::string OnvifClient::base64Decode(const std::string& input) {
    static unsigned char kDecode[256] = {};
    static bool init = false;
    if (!init) {
        std::fill(kDecode, kDecode + 256, 0xFF);
        for (int c = 'A'; c <= 'Z'; ++c)
            kDecode[(unsigned char)c] = c - 'A';
        for (int c = 'a'; c <= 'z'; ++c)
            kDecode[(unsigned char)c] = c - 'a' + 26;
        for (int c = '0'; c <= '9'; ++c)
            kDecode[(unsigned char)c] = c - '0' + 52;
        kDecode[(unsigned char)'+'] = 62;
        kDecode[(unsigned char)'/'] = 63;
        init = true;
    }

    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (kDecode[c] == 0xFF)
            continue;
        val = (val << 6) + kDecode[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back((char)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// ─── WS-Security UsernameToken ─────────────────────────────────────────────

std::string OnvifClient::buildWsseHeader() const {
    if (username_.empty())
        return {};

    // Generate 16 random bytes for nonce
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    std::string nonceRaw;
    nonceRaw.reserve(16);
    for (int i = 0; i < 16; ++i)
        nonceRaw += static_cast<char>(dist(gen));

    // ISO 8601 timestamp
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    gmtime_r(&t, &tm);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", &tm);
    std::string created = std::string(timeBuf) + "." + std::to_string(ms.count()) + "Z";

    // Password Digest: Base64(SHA1(Nonce + Timestamp + Password))
    std::string digestInput = nonceRaw + created + password_;
    std::string hash = crypto::sha1(digestInput);
    std::string digest = base64Encode(hash);

    std::string nonceB64 = base64Encode(nonceRaw);

    return "<soap:Header>"
           "<wsse:Security xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
           "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-wssecurity-utility-1.0.xsd\">"
           "<wsse:UsernameToken wsu:Id=\"UsernameToken-1\">"
           "<wsse:Username>" +
        username_ +
        "</wsse:Username>"
        "<wsse:Password Type=\"http://docs.oasis-open.org/wss/2004/01/"
        "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">" +
        digest +
        "</wsse:Password>"
        "<wsse:Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/"
        "oasis-200401-wss-soap-message-security-1.0#Base64Binary\">" +
        nonceB64 +
        "</wsse:Nonce>"
        "<wsu:Created>" +
        created +
        "</wsu:Created>"
        "</wsse:UsernameToken>"
        "</wsse:Security>"
        "</soap:Header>";
}

std::string OnvifClient::buildSoapEnvelope(const std::string& extraNs,
                                           const std::string& bodyXml) const {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<soap:Envelope "
           "xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" " +
        extraNs + ">" + buildWsseHeader() + "<soap:Body>" + bodyXml +
        "</soap:Body>"
        "</soap:Envelope>";
}

// ─── HTTP POST ─────────────────────────────────────────────────────────────

std::string OnvifClient::httpPost(const std::string& endpoint, const std::string& soapXml) {
    std::string host = host_;
    int port = port_;
    std::string path = "/onvif/device_service";

    if (endpoint.find("http://") == 0) {
        std::string rest = endpoint.substr(7);
        auto slashPos = rest.find('/');
        auto hostPart = (slashPos != std::string::npos) ? rest.substr(0, slashPos) : rest;
        path = (slashPos != std::string::npos) ? rest.substr(slashPos) : "/";

        auto colonPos = hostPart.find(':');
        if (colonPos != std::string::npos) {
            host = hostPart.substr(0, colonPos);
            port = std::stoi(hostPart.substr(colonPos + 1));
        } else {
            host = hostPart;
            port = 80;
        }
    }

    int fd = connectToHost(host, port);
    if (fd < 0)
        return {};

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Content-Type: application/soap+xml; charset=utf-8\r\n"
        << "Content-Length: " << soapXml.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << soapXml;

    std::string request = req.str();
    [[maybe_unused]] ssize_t written = write(fd, request.data(), request.size());

    std::string response;
    recvAll(fd, response);
    close(fd);

    auto bodyPos = response.find("\r\n\r\n");
    if (bodyPos == std::string::npos)
        return {};

    auto statusLineEnd = response.find("\r\n");
    if (statusLineEnd != std::string::npos) {
        auto firstSpace = response.find(' ');
        if (firstSpace != std::string::npos && firstSpace < statusLineEnd) {
            auto secondSpace = response.find(' ', firstSpace + 1);
            if (secondSpace != std::string::npos && secondSpace < statusLineEnd) {
                std::string statusStr =
                    response.substr(firstSpace + 1, secondSpace - firstSpace - 1);
                if (!statusStr.empty() &&
                    std::all_of(statusStr.begin(), statusStr.end(), ::isdigit)) {
                    int status = std::stoi(statusStr);
                    if (status != 200) {
                        std::cerr << "ONVIF HTTP " << status << " for " << endpoint << std::endl;
                    }
                }
            }
        }
    }

    return response.substr(bodyPos + 4);
}

// ─── SOAP Request ──────────────────────────────────────────────────────────

OnvifClient::HttpResponse OnvifClient::soapRequest(const std::string& url,
                                                   const std::string& soapXml) {
    std::string body = httpPost(url, soapXml);
    HttpResponse resp;
    resp.body = body;
    return resp;
}

// ─── GetDeviceInformation ──────────────────────────────────────────────────

DeviceInfo OnvifClient::getDeviceInformation() {
    DeviceInfo info;
    std::string soap = buildSoapEnvelope("xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\"",
                                         "<tds:GetDeviceInformation/>");

    auto resp = soapRequest(buildDeviceServiceUrl(host_, port_), soap);
    if (resp.body.empty())
        return info;

    auto doc = xml::parse(resp.body);
    info.manufacturer = xml::text(doc, "Manufacturer");
    info.model = xml::text(doc, "Model");
    info.firmwareVersion = xml::text(doc, "FirmwareVersion");
    info.serialNumber = xml::text(doc, "SerialNumber");
    info.hardwareId = xml::text(doc, "HardwareId");
    return info;
}

// ─── GetCapabilities ───────────────────────────────────────────────────────

Capabilities OnvifClient::getCapabilities() {
    Capabilities caps;
    std::string soap = buildSoapEnvelope("xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\"",
                                         "<tds:GetCapabilities>"
                                         "<tds:Category>All</tds:Category>"
                                         "</tds:GetCapabilities>");

    auto resp = soapRequest(buildDeviceServiceUrl(host_, port_), soap);
    if (resp.body.empty())
        return caps;

    auto doc = xml::parse(resp.body);
    auto capsNode = xml::findNode(doc, "Capabilities");
    if (capsNode) {
        auto mediaNode = xml::findNode(capsNode, "Media");
        if (mediaNode)
            caps.mediaServiceUrl = xml::text(mediaNode, "XAddr");

        auto ptzNode = xml::findNode(capsNode, "PTZ");
        if (ptzNode)
            caps.ptzServiceUrl = xml::text(ptzNode, "XAddr");

        auto imagingNode = xml::findNode(capsNode, "Imaging");
        if (imagingNode)
            caps.imagingServiceUrl = xml::text(imagingNode, "XAddr");

        auto eventsNode = xml::findNode(capsNode, "Events");
        if (eventsNode)
            caps.eventsServiceUrl = xml::text(eventsNode, "XAddr");
    }

    caps.deviceServiceUrl = xml::text(doc, "XAddr");
    if (caps.mediaServiceUrl.empty())
        caps.mediaServiceUrl = buildMediaServiceUrl(host_, port_);
    if (caps.deviceServiceUrl.empty())
        caps.deviceServiceUrl = buildDeviceServiceUrl(host_, port_);

    return caps;
}

// ─── GetProfiles ───────────────────────────────────────────────────────────

std::vector<MediaProfile> OnvifClient::getProfiles(const std::string& mediaUrl) {
    std::vector<MediaProfile> profiles;
    std::string soap = buildSoapEnvelope("xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\"",
                                         "<trt:GetProfiles/>");

    auto resp = soapRequest(mediaUrl, soap);
    if (resp.body.empty())
        return profiles;

    auto doc = xml::parse(resp.body);
    auto profileNodes = xml::findNodes(doc, "Profiles");

    for (auto& pn : profileNodes) {
        MediaProfile mp;
        mp.token = xml::attr(pn, "token");
        mp.name = xml::text(pn, "Name");

        auto encNode = xml::findNode(pn, "VideoEncoderConfiguration");
        if (encNode) {
            auto wStr = xml::text(encNode, "Width");
            auto hStr = xml::text(encNode, "Height");
            auto fStr = xml::text(encNode, "FrameRateLimit");
            if (fStr.empty())
                fStr = xml::text(encNode, "FrameRate");
            mp.encoding = xml::text(encNode, "Encoding");

            if (!wStr.empty())
                mp.width = std::stoi(wStr);
            if (!hStr.empty())
                mp.height = std::stoi(hStr);
            if (!fStr.empty())
                mp.fps = std::stoi(fStr);
        }

        profiles.push_back(mp);
    }

    return profiles;
}

// ─── GetStreamUri ──────────────────────────────────────────────────────────

std::string OnvifClient::getStreamUri(const std::string& mediaUrl,
                                      const std::string& profileToken) {
    std::string soap =
        buildSoapEnvelope("xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
                          "xmlns:tt=\"http://www.onvif.org/ver10/schema\"",
                          "<trt:GetStreamUri>"
                          "<trt:StreamSetup>"
                          "<tt:Stream>RTP-Unicast</tt:Stream>"
                          "<tt:Transport><tt:Protocol>UDP</tt:Protocol></tt:Transport>"
                          "</trt:StreamSetup>"
                          "<trt:ProfileToken>" +
                              profileToken +
                              "</trt:ProfileToken>"
                              "</trt:GetStreamUri>");

    auto resp = soapRequest(mediaUrl, soap);
    if (resp.body.empty())
        return {};

    auto doc = xml::parse(resp.body);
    return xml::text(doc, "Uri");
}

// ─── GetStreamUri (with transport protocol) ────────────────────────────────

std::string OnvifClient::getStreamUri(const std::string& mediaUrl, const std::string& profileToken,
                                      const std::string& transportProtocol) {
    std::string soap = buildSoapEnvelope("xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
                                         "xmlns:tt=\"http://www.onvif.org/ver10/schema\"",
                                         "<trt:GetStreamUri>"
                                         "<trt:StreamSetup>"
                                         "<tt:Stream>RTP-Unicast</tt:Stream>"
                                         "<tt:Transport><tt:Protocol>" +
                                             transportProtocol +
                                             "</tt:Protocol></tt:Transport>"
                                             "</trt:StreamSetup>"
                                             "<trt:ProfileToken>" +
                                             profileToken +
                                             "</trt:ProfileToken>"
                                             "</trt:GetStreamUri>");

    auto resp = soapRequest(mediaUrl, soap);
    if (resp.body.empty())
        return {};

    auto doc = xml::parse(resp.body);
    return xml::text(doc, "Uri");
}

// ─── GetSnapshotUri ─────────────────────────────────────────────────────────

std::string OnvifClient::getSnapshotUri(const std::string& mediaUrl,
                                        const std::string& profileToken) {
    std::string soap = buildSoapEnvelope("xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\"",
                                         "<trt:GetSnapshotUri>"
                                         "<trt:ProfileToken>" +
                                             profileToken +
                                             "</trt:ProfileToken>"
                                             "</trt:GetSnapshotUri>");

    auto resp = soapRequest(mediaUrl, soap);
    if (resp.body.empty())
        return {};

    auto doc = xml::parse(resp.body);
    return xml::text(doc, "Uri");
}

// ─── SetVideoEncoderConfiguration ──────────────────────────────────────────

bool OnvifClient::setVideoEncoderConfiguration(const std::string& mediaUrl,
                                               const std::string& configToken, int width,
                                               int height, int fps, const std::string& encoding) {
    std::string soap = buildSoapEnvelope("xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
                                         "xmlns:tt=\"http://www.onvif.org/ver10/schema\"",
                                         "<trt:SetVideoEncoderConfiguration>"
                                         "<trt:Configuration token=\"" +
                                             configToken +
                                             "\">"
                                             "<tt:Encoding>" +
                                             encoding +
                                             "</tt:Encoding>"
                                             "<tt:Resolution>"
                                             "<tt:Width>" +
                                             std::to_string(width) +
                                             "</tt:Width>"
                                             "<tt:Height>" +
                                             std::to_string(height) +
                                             "</tt:Height>"
                                             "</tt:Resolution>"
                                             "<tt:RateControl>"
                                             "<tt:FrameRateLimit>" +
                                             std::to_string(fps) +
                                             "</tt:FrameRateLimit>"
                                             "<tt:EncodingInterval>1</tt:EncodingInterval>"
                                             "<tt:BitrateLimit>4096</tt:BitrateLimit>"
                                             "</tt:RateControl>"
                                             "<tt:ForcePersistence>true</tt:ForcePersistence>"
                                             "</trt:Configuration>"
                                             "</trt:SetVideoEncoderConfiguration>");

    auto resp = soapRequest(mediaUrl, soap);
    return resp.body.find("Fault") == std::string::npos && !resp.body.empty();
}

// ─── GetProfilesWithStreams ─────────────────────────────────────────────────

std::vector<MediaProfile> OnvifClient::getProfilesWithStreams(const std::string& mediaUrl) {
    auto profiles = getProfiles(mediaUrl);
    for (auto& p : profiles) {
        if (!p.token.empty())
            p.streamUri = getStreamUri(mediaUrl, p.token);
    }
    return profiles;
}

// ─── DiscoverAll (full chain) ──────────────────────────────────────────────

FullDeviceInfo OnvifClient::discoverAll() {
    FullDeviceInfo result;
    result.device = getDeviceInformation();
    result.caps = getCapabilities();
    if (!result.caps.mediaServiceUrl.empty())
        result.profiles = getProfilesWithStreams(result.caps.mediaServiceUrl);
    return result;
}

} // namespace onvif
