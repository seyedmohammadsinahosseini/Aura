#include "OnvifDiscovery.h"
#include "core/xml/XmlParser.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace onvif {

static std::string generateUuidV4() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    auto r1 = dist(gen);
    auto r2 = dist(gen);
    auto r3 = dist(gen);
    auto r4 = dist(gen);

    char buf[37];
    snprintf(buf, sizeof(buf), "%08x-%04x-4%03x-%04x-%08x%04x", r1, (r2 >> 16) & 0xFFFF,
             r2 & 0x0FFF, (0x8000 | (r3 & 0x3FFF)), r3 >> 16, r4 & 0xFFFF);
    return "uuid:" + std::string(buf);
}

struct InterfaceInfo {
    std::string name;
    in_addr addr{};
};

static std::vector<InterfaceInfo> getActiveInterfaces() {
    std::vector<InterfaceInfo> interfaces;

    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) < 0)
        return interfaces;

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING))
            continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;

        InterfaceInfo info;
        info.name = ifa->ifa_name;
        info.addr = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
        interfaces.push_back(info);
    }

    freeifaddrs(ifaddr);
    return interfaces;
}

static int createBoundSocket(in_addr interfaceAddr) {
    int sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (sock < 0)
        return -1;

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    int ttl = 4;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    struct sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = 0;
    bindAddr.sin_addr = interfaceAddr;
    if (bind(sock, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
        close(sock);
        return -1;
    }

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &interfaceAddr, sizeof(interfaceAddr)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

std::vector<DiscoveredDevice> OnvifDiscovery::probe(int timeoutMs) {
    std::vector<DiscoveredDevice> found;
    std::set<std::string> seenIps;

    // ---- Step 1: discover active network interfaces ----
    auto interfaces = getActiveInterfaces();
    if (interfaces.empty()) {
        std::cerr << "[WS-Discovery] No active network interfaces found" << std::endl;
        return found;
    }

    // ---- Step 2: create one bound socket per interface ----
    struct SocketEntry {
        int fd;
        std::string ifaceName;
    };
    std::vector<SocketEntry> sockets;
    sockets.reserve(interfaces.size());

    for (const auto& iface : interfaces) {
        int fd = createBoundSocket(iface.addr);
        if (fd >= 0) {
            sockets.push_back({fd, iface.name});
        } else {
            std::cerr << "[WS-Discovery] Failed to bind socket for " << iface.name << std::endl;
        }
    }

    if (sockets.empty()) {
        std::cerr << "[WS-Discovery] No sockets could be bound" << std::endl;
        return found;
    }

    // ---- Step 3: build the Probe XML once ----
    std::string messageId = generateUuidV4();

    std::string probeMsg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                           "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
                           "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
                           "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
                           "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
                           "<e:Header>"
                           "<w:MessageID>" +
        messageId +
        "</w:MessageID>"
        "<w:To e:mustUnderstand=\"true\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>"
        "<w:Action e:mustUnderstand=\"true\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>"
        "</e:Header>"
        "<e:Body>"
        "<d:Probe>"
        "<d:Types>dn:NetworkVideoTransmitter</d:Types>"
        "</d:Probe>"
        "</e:Body>"
        "</e:Envelope>";

    // ---- Step 4: send Probe multicast through each interface ----
    struct sockaddr_in mcast{};
    mcast.sin_family = AF_INET;
    mcast.sin_port = htons(3702);
    mcast.sin_addr.s_addr = inet_addr("239.255.255.250");

    for (const auto& entry : sockets) {
        for (int i = 0; i < 3; ++i) {
            sendto(entry.fd, probeMsg.data(), probeMsg.size(), 0, (struct sockaddr*)&mcast,
                   sizeof(mcast));
        }
    }

    // ---- Step 5: receive responses using select() ----
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    char buffer[65536];

    while (std::chrono::steady_clock::now() < deadline) {
        fd_set readSet;
        FD_ZERO(&readSet);
        int maxFd = 0;
        for (const auto& entry : sockets) {
            FD_SET(entry.fd, &readSet);
            if (entry.fd > maxFd)
                maxFd = entry.fd;
        }

        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                             deadline - std::chrono::steady_clock::now())
                             .count();
        if (remaining <= 0)
            break;

        struct timeval tv{};
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int ret = select(maxFd + 1, &readSet, nullptr, nullptr, &tv);
        if (ret <= 0)
            continue;

        for (const auto& entry : sockets) {
            if (!FD_ISSET(entry.fd, &readSet))
                continue;

            struct sockaddr_in from{};
            socklen_t addrLen = sizeof(from);
            int n = recvfrom(entry.fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&from,
                             &addrLen);
            if (n <= 0)
                continue;
            buffer[n] = '\0';

            std::string ip = inet_ntoa(from.sin_addr);
            if (!seenIps.insert(ip).second)
                continue;

            std::string resp(buffer, n);
            if (resp.find("ProbeMatches") == std::string::npos)
                continue;

            DiscoveredDevice device;
            device.ip = ip;
            device.port = 80;
            device.name = "ONVIF Device (" + ip + ")";

            auto doc = xml::parse(resp);

            auto xaddrs = xml::allText(doc, "XAddrs");
            if (!xaddrs.empty()) {
                std::istringstream ss(xaddrs[0]);
                std::string url;
                while (ss >> url) {
                    if (url.find("http://") == 0 || url.find("https://") == 0) {
                        device.xaddrs = url;
                        break;
                    }
                }
                if (!device.xaddrs.empty()) {
                    size_t portStart = device.xaddrs.find("://") + 3;
                    size_t colon = device.xaddrs.find(':', portStart);
                    size_t slash = device.xaddrs.find('/', portStart);
                    if (colon != std::string::npos &&
                        (slash == std::string::npos || colon < slash)) {
                        try {
                            device.port =
                                std::stoi(device.xaddrs.substr(colon + 1, slash - colon - 1));
                        } catch (...) {
                        }
                    }
                }
            }

            auto scopesList = xml::allText(doc, "Scopes");
            if (!scopesList.empty()) {
                std::istringstream ss(scopesList[0]);
                std::string s;
                while (ss >> s) {
                    if (!s.empty())
                        device.scopes.push_back(s);
                    auto hw = s.find("/hardware/");
                    if (hw != std::string::npos && hw + 10 < s.size())
                        device.name = s.substr(hw + 10);
                }
            }

            found.push_back(std::move(device));
        }
    }

    // ---- cleanup ----
    for (const auto& entry : sockets) {
        close(entry.fd);
    }

    return found;
}

} // namespace onvif
