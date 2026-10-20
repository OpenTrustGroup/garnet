// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_LOGGING_RATE_LIMITER_H_
#define LIB_FXL_LOGGING_RATE_LIMITER_H_

#include "lib/fxl/logging.h"
#include <atomic>
#include <chrono>
#include <mutex>

namespace fxl {

/// Default: max 10 logs per 5 seconds, drop excess logs silently.
class LogRateLimiter {
 public:
  using Clock = std::chrono::steady_clock;
  using Duration = Clock::duration;

  static constexpr uint64_t DEFAULT_INTERVAL_MS = 5000;
  static constexpr uint32_t DEFAULT_BURST = 10;

  LogRateLimiter()
      : window_(std::chrono::milliseconds(DEFAULT_INTERVAL_MS)),
        max_count_(DEFAULT_BURST) {}

  bool AllowLog() {
    auto now = Clock::now();
    std::lock_guard<std::mutex> lock(mutex_);

    if (now - window_start_ >= window_) {
      window_start_ = now;
      count_ = 0;
    }

    if (count_ < max_count_) {
      count_++;
      return true;
    }

    return false;
  }

 private:
  Duration window_;
  uint32_t max_count_;
  std::mutex mutex_;
  Clock::time_point window_start_ = Clock::now();
  uint32_t count_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogRateLimiter);
};

}  // namespace fxl

#define FXL_LOG_RATELIMITED(severity) \
  static ::fxl::LogRateLimiter __log_rate_limiter; \
  FXL_LAZY_STREAM( \
    FXL_LOG_STREAM(severity), \
    __log_rate_limiter.AllowLog() \
  )

#define FXL_LOG_ERR_RATELIMITED() \
  FXL_LOG_RATELIMITED(ERROR)

#define FXL_LOG_WARN_RATELIMITED() \
  FXL_LOG_RATELIMITED(WARNING)

#define FXL_LOG_INFO_RATELIMITED() \
  FXL_LOG_RATELIMITED(INFO)

#endif  // LIB_FXL_LOGGING_RATE_LIMITER_H_
