#ifndef ONVIF_DISCOVERY_H
#define ONVIF_DISCOVERY_H

#include <string>
#include <vector>

namespace onvif {

struct DiscoveredDevice {
    std::string ip;
    int port{80};
    std::string xaddrs;
    std::vector<std::string> scopes;
    std::string name;
};

class OnvifDiscovery {
public:
    static std::vector<DiscoveredDevice> probe(int timeoutMs = 3000);
};

} // namespace onvif

#endif // ONVIF_DISCOVERY_H
