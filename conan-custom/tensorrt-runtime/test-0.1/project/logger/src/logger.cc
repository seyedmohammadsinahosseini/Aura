#include "logger/include/logger.h"
#include <base_component/monitoring/log/log_utils.h>

#include <source_location>

using namespace xbox::base_component::ai::runtime_handler::tensorrt;

Logger::Logger(const std::string& log_provider, const xbox::base_component::monitoring::log::LogCallback& log_callback) :
log_callback_{log_callback},
log_provider_{log_provider}
{ }

void Logger::log(nvinfer1::ILogger::Severity severity, const char *msg) noexcept {
    switch (severity) {
        case Severity::kINFO:
            log_callback_(xbox::base_component::monitoring::log::info(log_provider_, fmt::format("[TensorRT] {}", msg), std::source_location::current()));
            break;
        case Severity::kVERBOSE:
            log_callback_(xbox::base_component::monitoring::log::debug(log_provider_, fmt::format("[TensorRT] {}", msg), std::source_location::current()));
            break;
        case Severity::kERROR:
            log_callback_(xbox::base_component::monitoring::log::err(log_provider_, fmt::format("[TensorRT] {}", msg), std::source_location::current()));
            break;
        case Severity::kINTERNAL_ERROR:
            log_callback_(xbox::base_component::monitoring::log::fatal(log_provider_, fmt::format("[TensorRT] {}", msg), std::source_location::current()));
            break;
        case Severity::kWARNING:
            log_callback_(xbox::base_component::monitoring::log::warn(log_provider_, fmt::format("[TensorRT] {}", msg), std::source_location::current()));
            break;
    }
}