// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_LOG_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_LOG_H_

#include <cstddef>

// Logging utilities for the host library. This provides a common abstraction
// over Zircon DDK debug utilities (used when btlib runs in a driver) and
// printf.
//
// USAGE:
//
// Functions have been provided to check if logging has been enabled at a
// certain severity and to log a message using a tag, file name, and line
// number:
//
//     if (IsLogLevelEnabled(LogSeverity::TRACE)) {
//       LogMessage(__FILE__, __LINE__, LogSeverity::TRACE, "bt-host", "oops:
//                  %d", foo);
//     }
//
// or using the bt_log convenience macro:
//
//     bt_log(TRACE, "bt-host", "oops: %d", foo);
//
// DRIVER MODE:
//
// By default, the log messages use <ddk/debug.h> as its backend. In this mode
// the ERROR, WARN, INFO, TRACE, and SPEW severity levels directly correspond to
// the DDK severity levels. Log levels are supplied to the kernel commandline,
// e.g. to disable INFO level and enable TRACE level messages in the bt-host
// driver use the following:
//
//     driver.bthost.log=-info,+trace
//
// In driver mode, the "tag" argument to bt_log is informational and gets
// included in the log message.
//
// (refer to
// https://fuchsia.googlesource.com/zircon/+/master/docs/kernel_cmdline.md#driver_name_log_flags
// for all supported DDK debug log flags).
//
// PRINTF MODE:
//
// When btlib code is run outside a driver (e.g. bt-host-unittests) log messages
// can be routed to stdout via printf instead of driver_printf. To enable this
// mode, call the UsePrintf() function at process start-up:
//
//    int main() {
//      ::btlib::common::UsePrintf(::btlib::common::LogSeverity::ERROR);
//
//      ...do stuff...
//
//      return 0;
//    }
//
// The |min_severity| parameter determines the smallest severity level that will
// be allowed. For example, passing LogSeverity::INFO will enable INFO, WARN,
// and ERROR severity levels.
//
// The UsePrintf() function is NOT thread-safe. This should be called EARLY
// and ONLY ONCE during initialization. Once the printf mode is enabled it
// cannot be toggled back to driver mode.
//
// CAVEATS:
//
// Since the logging mode is determined at run-time and not compile-time (due
// to build dependency reasons) users of these utilities will need to link a
// symbol for |__zircon_driver_rec__|. While this symbol will remain unused in
// printf-mode it is needed to pass compilation if the target is not a driver.
// Use the BT_DECLARE_FAKE_DRIVER macro for this purpose:
//
//    BT_DECLARE_FAKE_DRIVER();
//
//    int main() {
//      ::btlib::common::UsePrintf(::btlib::common::LogSeverity::TRACE);
//    }

namespace btlib {
namespace common {

// Log severity levels used by the host library, following the convention of
// <ddk/debug.h>
enum class LogSeverity {
  // Indicates unexpected failures.
  ERROR = 0,

  // Indicates a situation that is not an error but may be indicative of an
  // impending problem.
  WARN = 1,

  // Terse information messages for startup, shutdown, or other infrequent state
  // changes.
  INFO = 2,

  // Verbose messages for transactions and state changes
  TRACE = 3,

  // Very verbose messages.
  SPEW = 4,

  // Reserved for highly verbose messages (corresponds to the "debug1" driver
  // log flag).
  DEBUG = 5,
};

constexpr size_t kNumLogSeverities = 6;

bool IsLogLevelEnabled(LogSeverity severity);
void LogMessage(const char* file, int line, LogSeverity severity,
                const char* tag, const char* fmt, ...);

void UsePrintf(LogSeverity min_severity);

}  // namespace common
}  // namespace btlib

#define bt_log(flag, tag, fmt...)                                            \
  do {                                                                       \
    if (::btlib::common::IsLogLevelEnabled(                                  \
            ::btlib::common::LogSeverity::flag)) {                           \
      ::btlib::common::LogMessage(                                           \
          __FILE__, __LINE__, ::btlib::common::LogSeverity::flag, tag, fmt); \
    }                                                                        \
  } while (0)

#define BT_DECLARE_FAKE_DRIVER() zx_driver_rec_t __zircon_driver_rec__ = {};

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_LOG_H_
