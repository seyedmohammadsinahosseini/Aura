#ifndef XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_ERROR_H_
#define XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_ERROR_H_

#include <functional>
#include <fmt/core.h>

#include <base_component/monitoring/log/log_entry.h>

#include "log_constants.h"

namespace xbox::base_component::monitoring::log {

static std::string GetExceptionMessage(std::string error_info, std::string error = constants::kDefaultException) {
  return fmt::format(constants::kExceptionFormat, error_info, error);
}

} // namespace xbox::base_component::monitoring::log

#endif // XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_ERROR_H_