#ifndef TENSORRT_RUNTIME_LOGGER_INCLUDE_LOGGER_H_
#define TENSORRT_RUNTIME_LOGGER_INCLUDE_LOGGER_H_

#include <base_component/monitoring/log/log_utils.h>

#include <NvInfer.h>
#include <string>

namespace xbox::base_component::ai::runtime_handler::tensorrt {

class Logger : public nvinfer1::ILogger {
public:
    explicit Logger(const std::string& log_provider, const xbox::base_component::monitoring::log::LogCallback& log_callback);
    void log(Severity severity, const char* msg) noexcept override;
private:
    xbox::base_component::monitoring::log::LogCallback log_callback_{};
    std::string log_provider_{};
};

}

#endif //TENSORRT_RUNTIME_LOGGER_INCLUDE_LOGGER_H_
