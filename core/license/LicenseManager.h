#ifndef BACKEND_CORE_LICENSE_LICENSEMANAGER_H
#define BACKEND_CORE_LICENSE_LICENSEMANAGER_H

#include <chrono>
#include <string>
#include <unordered_set>

namespace backend {

struct EntitlementSet {
    std::unordered_set<std::string> allowedModules{};
    int maxCameras{1};
    std::chrono::system_clock::time_point expiry{};

    bool canLoad(const std::string& moduleName) const;
};

class LicenseManager {
public:
    explicit LicenseManager(std::string licensePath = {});

    bool reload();
    bool isValid() const;
    const EntitlementSet& entitlements() const;
    const std::string& lastError() const;

private:
    std::string license_path_{};
    EntitlementSet entitlements_{};
    bool valid_{false};
    std::string last_error_{};
};

} // namespace backend

#endif // BACKEND_CORE_LICENSE_LICENSEMANAGER_H
