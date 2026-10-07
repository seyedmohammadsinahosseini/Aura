#ifndef XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_CONSTANTS_H_
#define XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_CONSTANTS_H_

#include <string>

namespace xbox::base_component::monitoring::log {

  namespace constants {
    // Timestamp format
    static constexpr const char* kTimestampFormat = "%Y-%m-%d, %H:%M:%S";
    static constexpr const char* kLogFormat = "[{}] |{}| ({}:{} -> {}) <{}:{}> {{{}}}: {}";
    static constexpr auto kExceptionFormat = "{} - Exception[{}]";
    static constexpr auto kDefaultException = "Unknown Exception!";
    // JSON field names
    static constexpr const char* kTimestamp = "Timestamp";
    static constexpr const char* kFilename = "Filename";
    static constexpr const char* kLineNumber = "LineNumber";
    static constexpr const char* kFunctionName = "FunctionName";
    static constexpr const char* kLogProvider = "LogProvider";
    static constexpr const char* kLevel = "LogLevel";
    static constexpr const char* kMessage = "LogDetails";
    static constexpr const char* kThreadId = "ThreadId";
    static constexpr const char* kProcessId = "ProcessId";
  }  // namespace constants

} // namespace xbox::base_component::monitoring::log

#endif  // XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_CONSTANTS_H_