// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest_manager/guest_manager_reporter.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <unistd.h>

#include <utility>

#include <zircon/device/grt-wdt.h>

#include "lib/fxl/logging.h"

namespace {

constexpr uint32_t kGuestManagerHeartbeatIntervalSec = 10;
constexpr uint32_t kGuestManagerHeartbeatMissThreshold = 3;
constexpr uint32_t kGuestManagerWatchdogBootGraceSec = 30;

}  // namespace

GuestManagerReporter::GuestManagerReporter(async_t* async,
                                           HealthCheck health_check)
    : async_(async),
      health_check_(std::move(health_check)),
      weak_factory_(this) {}

GuestManagerReporter::~GuestManagerReporter() {
  Stop();
}

zx_status_t GuestManagerReporter::Start() {
  if (wdt_fd_ >= 0 || platform_reset_requested_) {
    return ZX_ERR_BAD_STATE;
  }

  wdt_fd_ = open(GRT_WDT_CONTROL_DEVICE, O_RDWR);
  if (wdt_fd_ < 0) {
    FXL_LOG(ERROR) << "Failed to open " << GRT_WDT_CONTROL_DEVICE
                   << " for guest_manager heartbeat reporting.";
    return ZX_ERR_IO;
  }

  const grt_gm_watchdog_config_t config = {
      .heartbeat_interval_sec = kGuestManagerHeartbeatIntervalSec,
      .miss_threshold = kGuestManagerHeartbeatMissThreshold,
      .boot_grace_sec = kGuestManagerWatchdogBootGraceSec,
      .enable = static_cast<uint8_t>(1),
  };
  const ssize_t config_result =
      ioctl_grt_wdt_set_gm_watchdog_config(wdt_fd_, &config);
  if (config_result < 0) {
    FXL_LOG(ERROR) << "Failed to configure guest_manager watchdog in driver: "
                   << config_result;
    close(wdt_fd_);
    wdt_fd_ = -1;
    return static_cast<zx_status_t>(config_result);
  }

  ++generation_;
  FXL_LOG(INFO) << "guest_manager platform watchdog reporter enabled, interval="
                << kGuestManagerHeartbeatIntervalSec << "s miss_threshold="
                << kGuestManagerHeartbeatMissThreshold << " boot_grace="
                << kGuestManagerWatchdogBootGraceSec << "s";
  SendHeartbeat(generation_, true);
  return ZX_OK;
}

void GuestManagerReporter::Stop() {
  weak_factory_.InvalidateWeakPtrs();
  ++generation_;

  if (wdt_fd_ < 0) {
    return;
  }

  if (platform_reset_requested_) {
    close(wdt_fd_);
    wdt_fd_ = -1;
    return;
  }

  FXL_LOG(INFO) << "guest_manager platform watchdog reporter disabled";

  const grt_gm_watchdog_config_t config = {
      .heartbeat_interval_sec = kGuestManagerHeartbeatIntervalSec,
      .miss_threshold = kGuestManagerHeartbeatMissThreshold,
      .boot_grace_sec = kGuestManagerWatchdogBootGraceSec,
      .enable = static_cast<uint8_t>(0),
  };
  const ssize_t config_result =
      ioctl_grt_wdt_set_gm_watchdog_config(wdt_fd_, &config);
  if (config_result < 0) {
    FXL_LOG(ERROR) << "Failed to disable guest_manager watchdog in driver: "
                   << config_result;
  }

  close(wdt_fd_);
  wdt_fd_ = -1;
}

void GuestManagerReporter::RequestPlatformReset() {
  if (platform_reset_requested_) {
    return;
  }
  platform_reset_requested_ = true;
  weak_factory_.InvalidateWeakPtrs();
  ++generation_;
  if (wdt_fd_ < 0) {
    wdt_fd_ = open(GRT_WDT_CONTROL_DEVICE, O_RDWR);
  }
  if (wdt_fd_ < 0) {
    FXL_LOG(ERROR) << "Failed to open watchdog control for platform reset; "
                     "guest_manager heartbeats remain stopped.";
    return;
  }
  const ssize_t result = ioctl_grt_wdt_trigger_reset(wdt_fd_);
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to request platform reset: " << result
                   << "; guest_manager heartbeats remain stopped.";
  }
}

void GuestManagerReporter::ScheduleNextHeartbeat() {
  auto weak = weak_factory_.GetWeakPtr();
  const uint64_t generation = generation_;
  async::PostDelayedTask(
      async_,
      [weak, generation]() {
        if (weak) {
          weak->SendHeartbeat(generation);
        }
      },
      zx::sec(kGuestManagerHeartbeatIntervalSec));
}

void GuestManagerReporter::SendHeartbeat(uint64_t generation, bool force) {
  if (platform_reset_requested_ || generation != generation_ || wdt_fd_ < 0) {
    return;
  }

  if (!force && health_check_ && !health_check_()) {
    FXL_LOG(ERROR) << "Skip guest_manager watchdog heartbeat because required "
                      "guest health check failed.";
    if (!platform_reset_requested_) {
      ScheduleNextHeartbeat();
    }
    return;
  }

  const ssize_t heartbeat_result = ioctl_grt_wdt_set_gm_heartbeat(wdt_fd_);
  if (heartbeat_result < 0) {
    FXL_LOG(ERROR)
        << "Failed to report guest_manager heartbeat to watchdog driver: "
        << heartbeat_result;
    return;
  }

  ScheduleNextHeartbeat();
}
