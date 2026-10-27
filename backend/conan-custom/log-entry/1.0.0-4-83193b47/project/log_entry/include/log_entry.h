// Copyright 2025 Veera Omid Company Limited
// Author: Muhammad Reza Heidary (muhammadreza.heidary@gmail.com)
//
// Licensed under the Veera Omid Company Limited License.
// This file is proprietary and confidential.
// Unauthorized copying of this file, via any medium, is strictly prohibited.
// All rights reserved.

#ifndef XBOX_base_component_MONITORING_LOG_LOG_ENTRY_H_
#define XBOX_base_component_MONITORING_LOG_LOG_ENTRY_H_

#include <chrono>
#include <iomanip>
#include <string>

#include "log_level.h"

namespace xbox::base_component::monitoring::log {

/**
 * @brief Log entry structure containing detailed logging information
 */
struct LogEntry {
  std::chrono::system_clock::time_point timestamp;      // When the log was created
  std::string filename;                                 // Source file name
  size_t line_number = 0;                               // Line number in source file
  std::string function_name;                            // Function name where log was created
  std::string log_provider;                             // Component/class providing the log
  LogLevel::Level level = LogLevel::Level::kInfo;       // Log level
  std::string message;                                  // Log message/details
  std::string thread_id;                                // Thread ID
  std::string process_id;                               // Process ID

  /**
   * @brief Checks if the log entry is empty
   *
   * @return bool True if the log entry is empty, false otherwise
   */
  bool Empty() const {
    return filename.empty() && function_name.empty() && log_provider.empty() &&
           message.empty() && thread_id.empty() && process_id.empty() && line_number == 0;
  }

  /**
   * @brief Clears all fields of the log entry
   */
  void Clear() {
    timestamp = std::chrono::system_clock::time_point{};
    filename.clear();
    line_number = 0;
    function_name.clear();
    log_provider.clear();
    level = LogLevel::Level::kInfo;
    message.clear();
    thread_id.clear();
    process_id.clear();
  }
};

}  // namespace xbox::base_component::monitoring::log

#endif  // XBOX_base_component_MONITORING_LOG_LOG_ENTRY_H_

