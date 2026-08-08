#ifndef BACKEND_CORE_STATICMODULE_H
#define BACKEND_CORE_STATICMODULE_H

#include "IModule.h"

namespace backend {

class StaticModule : public IModule {
public:
    explicit StaticModule(std::string moduleName);

    std::string name() const override;
    bool init(const ModuleConfig& config) override;
    bool start() override;
    bool pause() override;
    bool resume() override;
    void stop() override;
    bool updateConfig(const ModuleConfig& config) override;

    const ModuleConfig& config() const;

private:
    std::string module_name_;
    ModuleConfig config_{};
};

} // namespace backend

#endif // BACKEND_CORE_STATICMODULE_H
