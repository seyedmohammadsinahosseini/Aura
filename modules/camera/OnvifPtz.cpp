#include "OnvifPtz.h"
#include "core/crypto/Sha1.h"
#include "core/xml/XmlParser.h"

#include <algorithm>
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

OnvifPtz::OnvifPtz(const std::string& ptzServiceUrl,
                   const std::string& username,
                   const std::string& password)
    : ptzUrl_(ptzServiceUrl), username_(username), password_(password) {}

std::string OnvifPtz::base64Encode(const std::string& input) {
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
    if (valb > -6) out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string OnvifPtz::base64Decode(const std::string& input) {
    static unsigned char kDecode[256] = {};
    static bool init = false;
    if (!init) {
        std::fill(kDecode, kDecode + 256, 0xFF);
        for (int c = 'A'; c <= 'Z'; ++c) kDecode[(unsigned char)c] = c - 'A';
        for (int c = 'a'; c <= 'z'; ++c) kDecode[(unsigned char)c] = c - 'a' + 26;
        for (int c = '0'; c <= '9'; ++c) kDecode[(unsigned char)c] = c - '0' + 52;
        kDecode[(unsigned char)'+'] = 62;
        kDecode[(unsigned char)'/'] = 63;
        init = true;
    }

    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (kDecode[c] == 0xFF) continue;
        val = (val << 6) + kDecode[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back((char)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string OnvifPtz::buildWsseHeader() const {
    if (username_.empty()) return {};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    std::string nonceRaw;
    nonceRaw.reserve(16);
    for (int i = 0; i < 16; ++i)
        nonceRaw += static_cast<char>(dist(gen));

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    gmtime_r(&t, &tm);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", &tm);
    std::string created = std::string(timeBuf) + "." +
                          std::to_string(ms.count()) + "Z";

    std::string digestInput = nonceRaw + created + password_;
    std::string hash = crypto::sha1(digestInput);
    std::string digest = base64Encode(hash);
    std::string nonceB64 = base64Encode(nonceRaw);

    return
        "<soap:Header>"
        "<wsse:Security xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
        "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
        "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
        "oasis-200401-wss-wssecurity-utility-1.0.xsd\">"
        "<wsse:UsernameToken wsu:Id=\"UsernameToken-1\">"
        "<wsse:Username>" + username_ + "</wsse:Username>"
        "<wsse:Password Type=\"http://docs.oasis-open.org/wss/2004/01/"
        "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">"
        + digest + "</wsse:Password>"
        "<wsse:Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/"
        "oasis-200401-wss-soap-message-security-1.0#Base64Binary\">"
        + nonceB64 + "</wsse:Nonce>"
        "<wsu:Created>" + created + "</wsu:Created>"
        "</wsse:UsernameToken>"
        "</wsse:Security>"
        "</soap:Header>";
}

std::string OnvifPtz::buildSoapEnvelope(const std::string& extraNs,
                                         const std::string& bodyXml) const {
    return
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<soap:Envelope "
        "xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" "
        + extraNs + ">"
        + buildWsseHeader() +
        "<soap:Body>"
        + bodyXml +
        "</soap:Body>"
        "</soap:Envelope>";
}

static int connectToHost(const std::string& url) {
    std::string host = url;
    int port = 80;
    std::string path;

    if (url.find("http://") == 0) {
        std::string rest = url.substr(7);
        auto slashPos = rest.find('/');
        auto hostPart = (slashPos != std::string::npos) ? rest.substr(0, slashPos) : rest;
        auto colonPos = hostPart.find(':');
        if (colonPos != std::string::npos) {
            host = hostPart.substr(0, colonPos);
            port = std::stoi(hostPart.substr(colonPos + 1));
        } else {
            host = hostPart;
        }
    }

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
        if (fd < 0) continue;
        struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

std::string OnvifPtz::httpPost(const std::string& soapXml) {
    int fd = connectToHost(ptzUrl_);
    if (fd < 0) return {};

    std::string host = ptzUrl_;
    std::string path = "/onvif/ptz_service";
    if (ptzUrl_.find("http://") == 0) {
        std::string rest = ptzUrl_.substr(7);
        auto slashPos = rest.find('/');
        host = (slashPos != std::string::npos) ? rest.substr(0, slashPos) : rest;
        path = (slashPos != std::string::npos) ? rest.substr(slashPos) : "/";
        auto colonPos = host.find(':');
        if (colonPos != std::string::npos)
            host = host.substr(0, colonPos);
    }

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Content-Type: application/soap+xml; charset=utf-8\r\n"
        << "Content-Length: " << soapXml.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << soapXml;

    std::string request = req.str();
    [[maybe_unused]] ssize_t written = write(fd, request.data(), request.size());

    std::string response;
    char buf[4096];
    while (true) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        response.append(buf, n);
    }
    close(fd);

    auto bodyPos = response.find("\r\n\r\n");
    if (bodyPos == std::string::npos) return {};
    return response.substr(bodyPos + 4);
}

bool OnvifPtz::continuousMove(const std::string& profileToken,
                               float panSpeed, float tiltSpeed, float zoomSpeed) {
    std::string soap = buildSoapEnvelope(
        "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" "
        "xmlns:tt=\"http://www.onvif.org/ver10/schema\"",
        "<tptz:ContinuousMove>"
        "<tptz:ProfileToken>" + profileToken + "</tptz:ProfileToken>"
        "<tptz:Velocity>"
        "<tt:PanTilt x=\"" + std::to_string(panSpeed) +
                  "\" y=\"" + std::to_string(tiltSpeed) + "\"/>"
        "<tt:Zoom x=\"" + std::to_string(zoomSpeed) + "\"/>"
        "</tptz:Velocity>"
        "</tptz:ContinuousMove>");

    auto resp = httpPost(soap);
    return resp.find("Fault") == std::string::npos && !resp.empty();
}

bool OnvifPtz::stop(const std::string& profileToken) {
    std::string soap = buildSoapEnvelope(
        "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\"",
        "<tptz:Stop>"
        "<tptz:ProfileToken>" + profileToken + "</tptz:ProfileToken>"
        "<tptz:PanTilt>true</tptz:PanTilt>"
        "<tptz:Zoom>true</tptz:Zoom>"
        "</tptz:Stop>");

    auto resp = httpPost(soap);
    return resp.find("Fault") == std::string::npos && !resp.empty();
}

bool OnvifPtz::gotoPreset(const std::string& profileToken,
                           const std::string& presetToken) {
    std::string soap = buildSoapEnvelope(
        "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\"",
        "<tptz:GotoPreset>"
        "<tptz:ProfileToken>" + profileToken + "</tptz:ProfileToken>"
        "<tptz:PresetToken>" + presetToken + "</tptz:PresetToken>"
        "</tptz:GotoPreset>");

    auto resp = httpPost(soap);
    return resp.find("Fault") == std::string::npos && !resp.empty();
}

std::vector<PtzPreset> OnvifPtz::getPresets(const std::string& profileToken) {
    std::vector<PtzPreset> presets;
    std::string soap = buildSoapEnvelope(
        "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\"",
        "<tptz:GetPresets>"
        "<tptz:ProfileToken>" + profileToken + "</tptz:ProfileToken>"
        "</tptz:GetPresets>");

    auto resp = httpPost(soap);
    if (resp.empty()) return presets;

    auto doc = xml::parse(resp);
    auto presetNodes = xml::findNodes(doc, "Preset");
    for (auto& pn : presetNodes) {
        PtzPreset p;
        p.token = xml::attr(pn, "token");
        p.name = xml::text(pn, "Name");
        presets.push_back(p);
    }
    return presets;
}

} // namespace onvif
