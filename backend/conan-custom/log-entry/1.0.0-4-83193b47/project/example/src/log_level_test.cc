// Copyright 2025 Veera Omid Company Limited
// Author: Muhammad Reza Heidary (muhammadreza.heidary@gmail.com)
//
// Licensed under the Veera Omid Company Limited License.
// This file is proprietary and confidential.
// Unauthorized copying of this file, via any medium, is strictly prohibited.
// All rights reserved.

#include <gtest/gtest.h>
#include "log_level.h"

namespace xbox::base_component::monitoring::log {
namespace {

TEST(LogLevelTest, ToStringConversion) {
  EXPECT_EQ(LogLevel::ToString(LogLevel::Level::kTrace), "TRACE");
  EXPECT_EQ(LogLevel::ToString(LogLevel::Level::kDebug), "DEBUG");
  EXPECT_EQ(LogLevel::ToString(LogLevel::Level::kInfo), "INFO");
  EXPECT_EQ(LogLevel::ToString(LogLevel::Level::kWarning), "WARNING");
  EXPECT_EQ(LogLevel::ToString(LogLevel::Level::kError), "ERROR");
  EXPECT_EQ(LogLevel::ToString(LogLevel::Level::kFatal), "FATAL");
  EXPECT_EQ(LogLevel::ToString(LogLevel::Level::kCritical), "CRITICAL");
}

TEST(LogLevelTest, FromStringConversion) {
  EXPECT_EQ(LogLevel::FromString("Trace"), LogLevel::Level::kTrace);
  EXPECT_EQ(LogLevel::FromString("DeBuG"), LogLevel::Level::kDebug);
  EXPECT_EQ(LogLevel::FromString("InFO"), LogLevel::Level::kInfo);
  EXPECT_EQ(LogLevel::FromString("WARNING"), LogLevel::Level::kWarning);
  EXPECT_EQ(LogLevel::FromString("eRroR"), LogLevel::Level::kError);
  EXPECT_EQ(LogLevel::FromString("FATAL"), LogLevel::Level::kFatal);
  EXPECT_EQ(LogLevel::FromString("CRITICAL"), LogLevel::Level::kCritical);
}

TEST(LogLevelTest, InvalidStringConversion) {
  EXPECT_EQ(LogLevel::FromString("INVALID"), LogLevel::Level::kFatal);
  EXPECT_EQ(LogLevel::FromString(""), LogLevel::Level::kFatal);
}

TEST(LogLevelTest, CaseInsensitiveConversion) {
  EXPECT_EQ(LogLevel::FromString("trace"), LogLevel::Level::kTrace);
  EXPECT_EQ(LogLevel::FromString("debug"), LogLevel::Level::kDebug);
  EXPECT_EQ(LogLevel::FromString("info"), LogLevel::Level::kInfo);
}

}  // namespace
}  // namespace xbox::base_component::monitoring::log 