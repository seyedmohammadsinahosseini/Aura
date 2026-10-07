#include "StaticModule.h"

namespace backend {

StaticModule::StaticModule(std::string moduleName) : module_name_(std::move(moduleName)) {}

std::string StaticModule::name() const {
    return module_name_;
}

bool StaticModule::init(const ModuleConfig& config) {
    config_ = config;
    setState(ModuleState::Initialized);
    return true;
}

bool StaticModule::start() {
    if (state() == ModuleState::Unloaded || state() == ModuleState::Stopped) {
        return false;
    }
    setState(ModuleState::Running);
    return true;
}

bool StaticModule::pause() {
    if (state() != ModuleState::Running && state() != ModuleState::Initialized) {
        return false;
    }
    setState(ModuleState::Paused);
    return true;
}

bool StaticModule::resume() {
    if (state() != ModuleState::Paused && state() != ModuleState::Initialized) {
        return false;
    }
    setState(ModuleState::Running);
    return true;
}

void StaticModule::stop() {
    setState(ModuleState::Stopped);
}

bool StaticModule::updateConfig(const ModuleConfig& config) {
    config_ = config;
    return true;
}

const ModuleConfig& StaticModule::config() const {
    return config_;
}

} // namespace backend
