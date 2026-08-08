#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include "CameraDiscovery.h"
#include <opencv2/opencv.hpp>
#include <memory>
#include <optional>
#include <vector>
#include "CameraStreamer.h"
#include <map>

namespace camera {
    class CameraManager {
    public:
        CameraManager() = default;
        
        // Discovers all types of cameras with error handling
        std::vector<DeviceInfo> discoverAllCameras(const bool isDeepSearch);
        
        void printDevices(const std::vector<DeviceInfo>& devices) const;

        // Opens a camera and starts a dedicated background thread for it
        std::shared_ptr<CameraStreamer> openCamera(const DeviceInfo& info);

        // Closes a specific camera and removes it from the active registry
        void closeCamera(const std::string& devicePath);
        
        void closeAll();

    private:
        // Store streamers by their device path to manage them parallelly
        std::map<std::string, std::shared_ptr<CameraStreamer>> m_activeStreams;
    };
}

#endif
