// Copyright 2025 Veera Omid Company Limited
// Author: Muhammad Reza Heidary (muhammadreza.heidary@gmail.com)
//
// Licensed under the Veera Omid Company Limited License.
// This file is proprietary and confidential.
// Unauthorized copying of this file, via any medium, is strictly prohibited.
// All rights reserved.

#include <gtest/gtest.h>
#include "log_entry.h"

namespace xbox::base_component::monitoring::log {
namespace {

TEST(LogEntryTest, DefaultConstruction) {
  LogEntry entry;
  EXPECT_TRUE(entry.Empty());
  EXPECT_EQ(entry.line_number, 0);
  EXPECT_EQ(entry.level, LogLevel::Level::kInfo);
}

TEST(LogEntryTest, ClearMethod) {
  LogEntry entry;
  entry.filename = "test.cc";
  entry.function_name = "TestFunction";
  entry.log_provider = "TestProvider";
  entry.message = "Test message";
  entry.thread_id = "123";
  entry.process_id = "456";
  entry.line_number = 42;
  entry.level = LogLevel::Level::kError;

  entry.Clear();

  EXPECT_TRUE(entry.Empty());
  EXPECT_EQ(entry.line_number, 0);
  EXPECT_EQ(entry.level, LogLevel::Level::kInfo);
}

TEST(LogEntryTest, EmptyCheck) {
  LogEntry entry;
  EXPECT_TRUE(entry.Empty());

  entry.filename = "test.cc";
  EXPECT_FALSE(entry.Empty());

  entry.Clear();
  EXPECT_TRUE(entry.Empty());

  entry.message = "test message";
  EXPECT_FALSE(entry.Empty());
}

TEST(LogEntryTest, TimestampInitialization) {
  LogEntry entry;
  auto now = std::chrono::system_clock::now();
  entry.timestamp = now;
  
  EXPECT_EQ(entry.timestamp, now);
}

TEST(LogEntryTest, PartialPopulation) {
  LogEntry entry;
  entry.filename = "test.cc";
  entry.line_number = 42;
  entry.function_name = "TestFunction";
  
  EXPECT_FALSE(entry.Empty());
  EXPECT_EQ(entry.filename, "test.cc");
  EXPECT_EQ(entry.line_number, 42);
  EXPECT_EQ(entry.function_name, "TestFunction");
  EXPECT_TRUE(entry.log_provider.empty());
  EXPECT_TRUE(entry.message.empty());
  EXPECT_TRUE(entry.thread_id.empty());
  EXPECT_TRUE(entry.process_id.empty());
}

}  // namespace
}  // namespace xbox::base_component::monitoring::log 