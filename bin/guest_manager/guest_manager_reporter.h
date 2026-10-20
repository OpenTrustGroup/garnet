// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <lib/async/dispatcher.h>
#include <zircon/types.h>

#include <functional>

#include "lib/fxl/memory/weak_ptr.h"

class GuestManagerReporter {
 public:
  using HealthCheck = std::function<bool()>;

  explicit GuestManagerReporter(async_t* async,
                                HealthCheck health_check = nullptr);
  ~GuestManagerReporter();

  zx_status_t Start();
  void RequestPlatformReset();
  void Stop();
  bool running() const { return wdt_fd_ >= 0; }

 private:
  void ScheduleNextHeartbeat();
  void SendHeartbeat(uint64_t generation, bool force = false);

  async_t* const async_;
  HealthCheck health_check_;
  int wdt_fd_ = -1;
  uint64_t generation_ = 0;
  bool platform_reset_requested_ = false;
  fxl::WeakPtrFactory<GuestManagerReporter> weak_factory_;
};
