// Copyright 2025 Veera Omid Company Limited
// Author: Muhammad Reza Heidary (muhammadreza.heidary@gmail.com)
//
// Licensed under the Veera Omid Company Limited License.
// This file is proprietary and confidential.
// Unauthorized copying of this file, via any medium, is strictly prohibited.
// All rights reserved.

#ifndef XBOX_base_component_MONITORING_LOG_LOG_ENTRY_LOG_LEVEL_H_
#define XBOX_base_component_MONITORING_LOG_LOG_ENTRY_LOG_LEVEL_H_

#include <string>
#include <algorithm>

namespace xbox::base_component::monitoring::log {

/**
 * @brief Enumeration of available log levels.
 *
 * Log levels are used to indicate the severity or importance of a log message.
 * The levels are ordered from least to most severe:
 * - TRACE: Detailed information for debugging
 * - DEBUG: Debugging information
 * - INFO: General information
 * - WARNING: Warning messages
 * - ERROR: Error messages
 * - FATAL: Fatal errors
 */
class LogLevel {
 public:
  // Delete copy constructor and assignment operator
  LogLevel(const LogLevel&) = delete;
  LogLevel& operator=(const LogLevel&) = delete;

  // Scoped enum for log levels
  enum class Level {
    kFatal,    // Fatal errors
    kCritical, // Crticial errors
    kError,    // Error messages
    kWarning,  // Warning messages
    kInfo,     // General information
    kDebug,    // Debugging information
    kTrace     // Detailed information for debugging
  };

  /**
   * @brief Converts log level to string
   *
   * @param level Log level to convert
   * @return std::string String representation of the log level
   */
  static std::string ToString(const Level level) {
    switch (level) {
      case Level::kTrace:
        return kTraceLevelString;
      case Level::kDebug:
        return kDebugLevelString;
      case Level::kInfo:
        return kInfoLevelString;
      case Level::kWarning:
        return kWarningLevelString;
      case Level::kError:
        return kErrorLevelString;
      case Level::kCritical:
        return kCriticalLevelString;
      case Level::kFatal:
        return kFatalLevelString;
      default:
        return kUnknownLevelString;
    }
  }

  /**
   * @brief Converts string to log level
   *
   * @param level std::string Log level to convert
   * @return Level representation of the log level
   */
  static Level FromString(const std::string& level) {
    const auto converted = ToUpper(level);
    if (converted == kTraceLevelString) {
      return Level::kTrace;
    }
    if (converted == kDebugLevelString) {
      return Level::kDebug;
    }
    if (converted == kInfoLevelString) {
      return Level::kInfo;
    }
    if (converted == kWarningLevelString) {
      return Level::kWarning;
    }
    if (converted == kErrorLevelString) {
      return Level::kError;
    }
    if (converted == kFatalLevelString) {
      return Level::kFatal;
    }
    if (converted == kCriticalLevelString) {
      return Level::kCritical;
    }
    return Level::kFatal;
  }

  // Static constants for log level strings
  static auto constexpr kTraceLevelString = "TRACE";
  static auto constexpr kDebugLevelString = "DEBUG";
  static auto constexpr kInfoLevelString = "INFO";
  static auto constexpr kWarningLevelString = "WARNING";
  static auto constexpr kErrorLevelString = "ERROR";
  static auto constexpr kFatalLevelString = "FATAL";
  static auto constexpr kCriticalLevelString = "CRITICAL";
  static auto constexpr kUnknownLevelString = "UNKNOWN";

private:
  static std::string ToUpper(std::string str) {
    std::ranges::transform(str, str.begin(), [](const unsigned char c) {
        return std::toupper(c);
    });
    return str;
  }
};

}  // namespace xbox::base_component::monitoring::log

#endif  // XBOX_base_component_MONITORING_LOG_LOG_ENTRY_LOG_LEVEL_H_
