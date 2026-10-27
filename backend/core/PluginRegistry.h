#ifndef BACKEND_CORE_PLUGINREGISTRY_H
#define BACKEND_CORE_PLUGINREGISTRY_H

#include "IModule.h"
#include "license/LicenseManager.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace backend {

class PluginRegistry {
public:
    using ModuleFactory = std::function<std::shared_ptr<IModule>()>;

    void registerFactory(const std::string& name, ModuleFactory factory);
    bool loadAll(const EntitlementSet& entitlements,
                 const std::unordered_map<std::string, ModuleConfig>& configs);
    bool setModuleEnabled(const std::string& name, bool enabled);
    bool updateModuleConfig(const std::string& name, const ModuleConfig& config);
    std::shared_ptr<IModule> getModule(const std::string& name) const;
    const std::unordered_map<std::string, std::shared_ptr<IModule>>& modules() const;

private:
    std::unordered_map<std::string, ModuleFactory> factories_;
    std::unordered_map<std::string, std::shared_ptr<IModule>> modules_;
};

} // namespace backend

#endif // BACKEND_CORE_PLUGINREGISTRY_H
