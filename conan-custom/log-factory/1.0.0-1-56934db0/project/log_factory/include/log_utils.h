#ifndef XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_H_
#define XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_H_

#include "log_constants.h"
#include "log_error.h"
#include "log_factory.h"

namespace xbox::base_component::monitoring::log {

typedef std::function<void(const xbox::base_component::monitoring::log::LogEntry&)> LogCallback;

static xbox::base_component::monitoring::log::LogEntry
debug(const std::string &provider, const std::string &message,
      const std::source_location location = std::source_location::current()) {
  return LogFactory::Create(provider, xbox::base_component::monitoring::log::LogLevel::Level::kDebug, message, location);
}

static xbox::base_component::monitoring::log::LogEntry
info(const std::string &provider, const std::string &message,
     const std::source_location location = std::source_location::current()) {
  return LogFactory::Create(provider, xbox::base_component::monitoring::log::LogLevel::Level::kInfo, message, location);
}

static xbox::base_component::monitoring::log::LogEntry
warn(const std::string &provider, const std::string &message,
     const std::source_location location = std::source_location::current()) {
  return LogFactory::Create(provider, xbox::base_component::monitoring::log::LogLevel::Level::kWarning, message, location);
}

static xbox::base_component::monitoring::log::LogEntry
err(const std::string &provider, const std::string &message,
    const std::source_location location = std::source_location::current()) {
  return LogFactory::Create(provider, xbox::base_component::monitoring::log::LogLevel::Level::kError, message, location);
}

static xbox::base_component::monitoring::log::LogEntry
trace(const std::string &provider, const std::string &message,
    const std::source_location location = std::source_location::current()) {
  return LogFactory::Create(provider, xbox::base_component::monitoring::log::LogLevel::Level::kTrace, message, location);
}

static xbox::base_component::monitoring::log::LogEntry
fatal(const std::string &provider, const std::string &message,
    const std::source_location location = std::source_location::current()) {
  return LogFactory::Create(provider, xbox::base_component::monitoring::log::LogLevel::Level::kFatal, message, location);
}

static xbox::base_component::monitoring::log::LogEntry
critical(const std::string &provider, const std::string &message,
    const std::source_location location = std::source_location::current()) {
  return LogFactory::Create(provider, xbox::base_component::monitoring::log::LogLevel::Level::kCritical, message, location);
}

} // namespace xbox::base_component::monitoring::log

#endif // XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_H_