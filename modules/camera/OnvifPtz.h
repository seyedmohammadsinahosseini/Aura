#ifndef ONVIF_PTZ_H
#define ONVIF_PTZ_H

#include <string>
#include <vector>

namespace onvif {

struct PtzPreset {
    std::string token;
    std::string name;
};

class OnvifPtz {
public:
    OnvifPtz(const std::string& ptzServiceUrl, const std::string& username = "",
             const std::string& password = "");

    bool continuousMove(const std::string& profileToken, float panSpeed, float tiltSpeed,
                        float zoomSpeed);

    bool stop(const std::string& profileToken);

    bool gotoPreset(const std::string& profileToken, const std::string& presetToken);

    std::vector<PtzPreset> getPresets(const std::string& profileToken);

private:
    std::string ptzUrl_;
    std::string username_;
    std::string password_;

    std::string buildWsseHeader() const;
    std::string buildSoapEnvelope(const std::string& extraNs, const std::string& bodyXml) const;
    std::string httpPost(const std::string& soapXml);
    static std::string base64Encode(const std::string& input);
    static std::string base64Decode(const std::string& input);
};

} // namespace onvif

#endif // ONVIF_PTZ_H
