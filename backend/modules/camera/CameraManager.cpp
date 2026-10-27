#include "CameraManager.h"
#include <iostream>
#include <type_traits>

namespace camera {

    std::vector<DeviceInfo> CameraManager::discoverAllCameras(const bool isDeepSearch) {
        try {
            return CameraDiscovery::discoverAll(isDeepSearch);
        } catch (const std::exception& e) {
            std::cerr << "Discovery Error: " << e.what() << std::endl;
            return {};
        }
    }

    std::shared_ptr<CameraStreamer> CameraManager::openCamera(const DeviceInfo& info) {
        try {
            // Check if already open
            if (m_activeStreams.count(info.devicePath)) {
                auto existing = m_activeStreams[info.devicePath];
                if (existing && existing->isRunning()) {
                    return existing;
                }
                m_activeStreams.erase(info.devicePath);
            }

            auto streamer = std::make_shared<CameraStreamer>(info);
            if (streamer->start()) {
                m_activeStreams[info.devicePath] = streamer;
                return streamer;
            }
        } catch (const std::exception& e) {
            std::cerr << "Manager failed to open camera: " << e.what() << std::endl;
        }
        return nullptr;
    }

    void CameraManager::closeCamera(const std::string& devicePath) {
        auto it = m_activeStreams.find(devicePath);
        if (it == m_activeStreams.end()) {
            return;
        }

        if (it->second) {
            it->second->stop();
        }
        m_activeStreams.erase(it);
    }

    void CameraManager::closeAll() {
        for (auto& [path, streamer] : m_activeStreams) {
            streamer->stop();
        }
        m_activeStreams.clear();
    }

    void CameraManager::printDevices(const std::vector<DeviceInfo>& devices) const {
        if (devices.empty()) {
            std::cout << "No cameras detected." << std::endl;
            return;
        }

        std::cout << "\n--- Camera List ---" << std::endl;
        for (size_t i = 0; i < devices.size(); ++i) {
            const auto& d = devices[i];
            std::string typeStr = (d.type == ConnectionType::LOCAL) ? "Local" : "Network";
            std::cout << i + 1 << ". [" << typeStr << "] " << d.cardName 
                      << " (Path: " << d.devicePath << ")" << "(" << d.streamUri << ")" <<std::endl;
        }
        std::cout << "-------------------\n" << std::endl;
    }
}
