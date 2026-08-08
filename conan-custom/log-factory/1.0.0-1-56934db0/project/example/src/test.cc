#include <iostream>
#include <thread>
#include <chrono>

#include <log_utils.h>

using namespace xbox::base_component::monitoring::log;

// Example callback function to handle logs
void LogHandler(const LogEntry& entry) {
  // Print log in string format
  std::cout << LogFactory::ToString(entry) << std::endl;
  
  // Print log in JSON format
  std::cout << LogFactory::ToJson(entry).dump(2) << std::endl;
}

int main() {
  const std::string provider = "ExampleApp";
  
  // Demonstrate different log levels
  auto debug_log = debug(provider, "This is a debug message");
  auto info_log = info(provider, "This is an info message");
  auto warn_log = warn(provider, "This is a warning message");
  auto error_log = err(provider, "This is an error message");
  auto trace_log = trace(provider, "This is a trace message");
  auto fatal_log = fatal(provider, "This is a fatal message");
  auto critical_log = critical(provider, "This is a critical message");

  // Process all logs through the handler
  LogHandler(debug_log);
  LogHandler(info_log);
  LogHandler(warn_log);
  LogHandler(error_log);
  LogHandler(trace_log);
  LogHandler(fatal_log);
  LogHandler(critical_log);

  // Example of logging with exception
  try {
    throw std::runtime_error("Example exception");
  } catch (const std::exception& e) {
    auto error_log = err(provider, GetExceptionMessage("Error runtime", e.what()));
    LogHandler(error_log);
  }

  // Example of logging from different threads
  std::thread t1([provider]() {
    auto thread_log = info(provider, "Log from thread 1");
    LogHandler(thread_log);
  });

  std::thread t2([provider]() {
    auto thread_log = info(provider, "Log from thread 2");
    LogHandler(thread_log);
  });

  t1.join();
  t2.join();

  return 0;
}