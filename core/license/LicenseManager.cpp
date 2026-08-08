#include "LicenseManager.h"

#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace backend {
namespace {

std::chrono::system_clock::time_point parseDate(const std::string& value) {
    std::tm tm{};
    std::istringstream stream(value);
    stream >> std::get_time(&tm, "%Y-%m-%d");
    if (stream.fail()) {
        throw std::runtime_error("invalid license expiry format");
    }
    tm.tm_hour = 23;
    tm.tm_min = 59;
    tm.tm_sec = 59;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

} // namespace

bool EntitlementSet::canLoad(const std::string& moduleName) const {
    return allowedModules.count(moduleName) > 0 &&
           std::chrono::system_clock::now() < expiry;
}

LicenseManager::LicenseManager(std::string licensePath)
    : license_path_(std::move(licensePath)) {
    reload();
}

bool LicenseManager::reload() {
    valid_ = false;
    last_error_.clear();
    entitlements_ = EntitlementSet{};

    if (license_path_.empty()) {
        last_error_ = "license path is empty";
        return false;
    }

    std::ifstream file(license_path_);
    if (!file.is_open()) {
        last_error_ = "failed to open license file";
        return false;
    }

    try {
        nlohmann::json license;
        file >> license;

        for (const auto& item : license.value("modules", nlohmann::json::array())) {
            if (item.is_string()) {
                entitlements_.allowedModules.insert(item.get<std::string>());
            }
        }
        entitlements_.maxCameras = license.value("max_cameras", entitlements_.maxCameras);
        entitlements_.expiry = parseDate(license.value("expires", std::string("1970-01-01")));
        valid_ = !entitlements_.allowedModules.empty();
        if (!valid_) {
            last_error_ = "license does not allow any modules";
        }
    } catch (const std::exception& exception) {
        last_error_ = exception.what();
        valid_ = false;
    }

    return valid_;
}

bool LicenseManager::isValid() const {
    return valid_;
}

const EntitlementSet& LicenseManager::entitlements() const {
    return entitlements_;
}

const std::string& LicenseManager::lastError() const {
    return last_error_;
}

} // namespace backend
