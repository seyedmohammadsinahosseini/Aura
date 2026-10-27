#ifndef XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_FACTORY_H_
#define XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_FACTORY_H_

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <source_location>
#include <sstream>
#include <string>
#include <thread>

#include <base_component/monitoring/log/log_level.h>
#include <base_component/monitoring/log/log_entry.h>

#include "log_constants.h"

namespace xbox::base_component::monitoring::log {

/**
 * @brief A stateless logger class that provides logging functionality
 *
 * This class provides static methods to create and format log entries with complete context including
 * timestamp, filename, line number, function name, and log provider.
 * It supports multiple output formats:
 * - Formatted string using fmt library
 * - JSON object using nlohmann::json
 * - Raw LogEntry object
 *
 * The class is thread-safe as it maintains no internal state.
 */
class LogFactory {
 public:
  // Delete all constructors and assignment operators to prevent instantiation
  LogFactory() = delete;
  LogFactory(const LogFactory&) = delete;
  LogFactory& operator=(const LogFactory&) = delete;
  LogFactory(LogFactory&&) = delete;
  LogFactory& operator=(LogFactory&&) = delete;
  ~LogFactory() = delete;

  /**
   * @brief Formats a LogEntry into a human-readable string
   *
   * @param entry The LogEntry to format
   * @return std::string Formatted log string in the format:
   *         [LEVEL] |timestamp| (filename:line -> function) <pid:tid> {provider}: message
   */
  static std::string ToString(const xbox::base_component::monitoring::log::LogEntry& entry) {
    return FormatLogString(entry);
  }

  /**
   * @brief Converts a LogEntry to a JSON object
   *
   * @param entry The LogEntry to convert
   * @return nlohmann::json JSON object containing all log entry fields
   */
  static nlohmann::json ToJson(const xbox::base_component::monitoring::log::LogEntry& entry) {
    nlohmann::json json;
    json[constants::kFilename] = entry.filename;
    json[constants::kFunctionName] = entry.function_name;
    json[constants::kLevel] = xbox::base_component::monitoring::log::LogLevel::ToString(entry.level);
    json[constants::kLineNumber] = entry.line_number;
    json[constants::kLogProvider] = entry.log_provider;
    json[constants::kMessage] = entry.message;
    json[constants::kProcessId] = entry.process_id;
    json[constants::kThreadId] = entry.thread_id;
    json[constants::kTimestamp] = FormatTimestamp(entry.timestamp);
    return json;
  }

  /**
   * @brief Creates a new LogEntry with the given parameters
   *
   * @param log_provider Name of the component providing the logs
   * @param level Log level (TRACE, DEBUG, INFO, WARNING, ERROR, FATAL)
   * @param details Log message/details
   * @param location Source location information (automatically provided)
   * @return LogEntry A new log entry containing all context
   */
  static xbox::base_component::monitoring::log::LogEntry Create(const std::string& log_provider,
      const xbox::base_component::monitoring::log::LogLevel::Level level, const std::string& details,
      const std::source_location location = std::source_location::current()) {
    return CreateLogEntry(log_provider, level, details, location);
  }

 private:
  /**
   * @brief Creates a LogEntry with the given parameters
   *
   * @param log_provider Name of the component providing the logs
   * @param level Log level
   * @param details Log message/details
   * @param location Source location information
   * @return LogEntry A new log entry with all fields populated
   */
  static xbox::base_component::monitoring::log::LogEntry CreateLogEntry(const std::string& log_provider,
      const xbox::base_component::monitoring::log::LogLevel::Level level, const std::string& details,
      const std::source_location& location) {
    return xbox::base_component::monitoring::log::LogEntry{
        std::chrono::system_clock::now(),
        GetShortFilename(location.file_name()),
        static_cast<size_t>(location.line()),
        GetCleanFunctionName(location.function_name()),
        log_provider,
        level,
        details,
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())),
        std::to_string(getpid())
    };
  }

  /**
 * @brief Formats timestamp to string
 *
 * @param timestamp timestamp raw value
 * @return std::string Formatted timestamp
 */
  static std::string FormatTimestamp(const std::chrono::system_clock::time_point& timestamp) {
    const std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), constants::kTimestampFormat);
    return oss.str();
  }

  /**
   * @brief Formats a log entry into a string using fmt
   *
   * @param entry The log entry to format
   * @return std::string Formatted log string, or empty string if entry is empty
   */
  static std::string FormatLogString(const xbox::base_component::monitoring::log::LogEntry& entry) {
    if (entry.Empty()) {
      return "";
    }

    // Format the log string
    return fmt::format(constants::kLogFormat,
        xbox::base_component::monitoring::log::LogLevel::ToString(entry.level),
        FormatTimestamp(entry.timestamp),
        entry.filename,
        entry.line_number,
        entry.function_name,
        entry.process_id,
        entry.thread_id,
        entry.log_provider,
        entry.message);
  }

  /**
   * @brief Extracts the filename from a full path
   *
   * @param full_path The full file path
   * @return std::string Just the filename component
   */
  static std::string GetShortFilename(const std::string& full_path) {
    return std::filesystem::path(full_path).filename().string();
  }

  /**
   * @brief Cleans up a function signature by removing namespaces and simplifying types
   *
   * @param full_function The full function signature
   * @return std::string Cleaned up function name with simplified parameter types
   */
  static std::string GetCleanFunctionName(const std::string& full_function) {
    // Step 1: Find opening parenthesis
    const size_t paren_pos = full_function.find('(');
    if (paren_pos == std::string::npos) {
      return full_function;
    }

    // Step 2: Separate into prefix (return type + name) and params
    const std::string prefix = full_function.substr(0, paren_pos);
    const std::string params = full_function.substr(paren_pos);

    // Step 3: Extract return type and function name
    const size_t last_space = prefix.find_last_of(' ');
    if (last_space == std::string::npos) {
      return full_function;  // No space found, return as is
    }

    std::string return_type = prefix.substr(0, last_space);
    std::string func_name = prefix.substr(last_space + 1);

    // Step 4: Remove namespaces from function name
    const size_t last_colon = func_name.rfind("::");
    if (last_colon != std::string::npos) {
      func_name = func_name.substr(last_colon + 2);
    }

    // Step 5: Clean up parameter types (remove namespaces)
    std::string cleaned_params;
    bool inside_angle = false;
    bool in_word = false;
    std::string token;

    for (char c : params) {
      if (c == '<') inside_angle = true;
      if (c == '>') inside_angle = false;

      if (std::isalnum(c) || c == ':' || c == '_') {
        token += c;
        in_word = true;
      } else {
        if (in_word) {
          // Strip namespaces (e.g., remove "xbox::...::Level" → "Level")
          size_t pos = token.rfind("::");
          if (pos != std::string::npos && !inside_angle)
            token = token.substr(pos + 2);
          cleaned_params += token;
          token.clear();
        }
        cleaned_params += c;
        in_word = false;
      }
    }

    // Catch leftover token
    if (!token.empty()) {
      size_t pos = token.rfind("::");
      if (pos != std::string::npos && !inside_angle)
        token = token.substr(pos + 2);
      cleaned_params += token;
    }

    return return_type + " " + func_name + cleaned_params;
  }
};

}  // namespace xbox::base_component::monitoring::log

#endif  // XBOX_BASE_COMPONENT_MONITORING_LOG_LOG_FACTORY_LOG_FACTORY_H_
