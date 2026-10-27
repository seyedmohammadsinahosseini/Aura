#ifndef BACKEND_CORE_IMODULE_H
#define BACKEND_CORE_IMODULE_H

#include <atomic>
#include <string>
#include <unordered_map>
#include <string>

namespace backend {

enum class ModuleState {
    Unloaded,
    Initialized,
    Running,
    Paused,
    Stopped
};

struct ModuleConfig {
    bool enabled{true};
    std::unordered_map<std::string, std::string> parameters{};
    std::string rawJson{"{}"};
};

class IModule {
public:
    virtual ~IModule() = default;
    virtual std::string name() const = 0;
    virtual bool init(const ModuleConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual void stop() = 0;
    virtual bool updateConfig(const ModuleConfig& config) = 0;

    ModuleState state() const {
        return state_.load();
    }

protected:
    void setState(ModuleState state) {
        state_.store(state);
    }

    std::atomic<ModuleState> state_{ModuleState::Unloaded};
};

} // namespace backend

#endif // BACKEND_CORE_IMODULE_H
