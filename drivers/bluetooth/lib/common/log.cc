// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log.h"

#include <stdarg.h>
#include <algorithm>

#include <ddk/debug.h>

#include "lib/fxl/strings/string_printf.h"

namespace btlib {
namespace common {
namespace {

std::atomic_int g_printf_min_severity(-1);

uint32_t kDdkSeverities[kNumLogSeverities] = {
    DDK_LOG_ERROR, DDK_LOG_WARN, DDK_LOG_INFO,
    DDK_LOG_TRACE, DDK_LOG_SPEW, DDK_LOG_DEBUG1,
};

const char* const kLogSeverityNames[kNumLogSeverities] = {
    "ERROR", "WARN", "INFO", "TRACE", "SPEW", "DEBUG",
};

constexpr size_t LogSeverityToIndex(LogSeverity severity) {
  return std::min(kNumLogSeverities - 1, static_cast<size_t>(severity));
}

inline uint32_t LogSeverityToDdkLog(LogSeverity severity) {
  return kDdkSeverities[LogSeverityToIndex(severity)];
}

inline const char* LogSeverityToString(LogSeverity severity) {
  return kLogSeverityNames[LogSeverityToIndex(severity)];
}

const char* StripPath(const char* path) {
  const char* p = strrchr(path, '/');
  return p ? p + 1 : path;
}

bool IsPrintfEnabled() { return g_printf_min_severity >= 0; }

}  // namespace

bool IsLogLevelEnabled(LogSeverity severity) {
  if (IsPrintfEnabled()) {
    return static_cast<int>(severity) <= g_printf_min_severity;
  }
  return zxlog_level_enabled_etc(LogSeverityToDdkLog(severity));
}

void LogMessage(const char* file, int line, LogSeverity severity,
                const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::string msg = fxl::StringVPrintf(fmt, args);
  va_end(args);

  if (IsPrintfEnabled()) {
    printf("[%s:%s(%d)] %s: %s\n", LogSeverityToString(severity),
           StripPath(file), line, tag, msg.c_str());
  } else {
    driver_printf(LogSeverityToDdkLog(severity), "[%s:%s(%d)] %s: %s\n",
                  LogSeverityToString(severity), StripPath(file), line, tag,
                  msg.c_str());
  }
}

void UsePrintf(LogSeverity min_severity) {
  g_printf_min_severity = static_cast<int>(min_severity);
}

}  // namespace common
}  // namespace btlib
