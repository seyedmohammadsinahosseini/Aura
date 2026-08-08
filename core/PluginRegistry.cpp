#include "PluginRegistry.h"

namespace backend {

void PluginRegistry::registerFactory(const std::string& name, ModuleFactory factory) {
    if (name.empty() || !factory) {
        return;
    }
    factories_[name] = std::move(factory);
}

bool PluginRegistry::loadAll(const EntitlementSet& entitlements,
                             const std::unordered_map<std::string, ModuleConfig>& configs) {
    bool loadedAny = false;
    for (const auto& [name, factory] : factories_) {
        if (!entitlements.canLoad(name)) {
            continue;
        }

        auto module = factory();
        if (!module) {
            continue;
        }

        ModuleConfig config{};
        if (const auto it = configs.find(name); it != configs.end()) {
            config = it->second;
        }

        if (!module->init(config)) {
            continue;
        }

        if (config.enabled) {
            module->start();
        } else {
            module->pause();
        }

        modules_[name] = std::move(module);
        loadedAny = true;
    }
    return loadedAny;
}

bool PluginRegistry::setModuleEnabled(const std::string& name, bool enabled) {
    auto module = getModule(name);
    if (!module) {
        return false;
    }

    if (enabled) {
        if (module->state() == ModuleState::Paused || module->state() == ModuleState::Initialized) {
            return module->resume();
        }
        if (module->state() == ModuleState::Stopped) {
            return module->start();
        }
        return module->state() == ModuleState::Running;
    }

    if (module->state() == ModuleState::Running) {
        return module->pause();
    }
    return module->state() == ModuleState::Paused || module->state() == ModuleState::Initialized;
}

bool PluginRegistry::updateModuleConfig(const std::string& name, const ModuleConfig& config) {
    auto module = getModule(name);
    if (!module) {
        return false;
    }

    if (!module->updateConfig(config)) {
        return false;
    }

    return setModuleEnabled(name, config.enabled);
}

std::shared_ptr<IModule> PluginRegistry::getModule(const std::string& name) const {
    auto it = modules_.find(name);
    return it != modules_.end() ? it->second : nullptr;
}

const std::unordered_map<std::string, std::shared_ptr<IModule>>& PluginRegistry::modules() const {
    return modules_;
}

} // namespace backend
