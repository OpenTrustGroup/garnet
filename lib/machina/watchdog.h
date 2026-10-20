// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <atomic>
#include <functional>
#include <lib/zx/event.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>
#include <mutex>
#include <thread>
#include <utility>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

using DumpStateCallback = std::function<void(uint32_t reason)>;
using StopCallback = std::function<void(void)>;

namespace machina {
constexpr uint32_t kVmControlWdtCheckInterval = 30000;

class Watchdog {
 public:
  struct Options {
    std::function<zx_status_t(zx::resource*)> get_root_resource;
    std::function<zx::time()> now;
    std::function<zx_status_t(zx_handle_t resource, int32_t vmid,
                              int32_t* pm_state)>
        get_pm_state;
    zx::duration check_interval = zx::msec(kVmControlWdtCheckInterval);
    zx::duration wakeup_delay_tolerance = zx::sec(2);
  };

  Watchdog();
  explicit Watchdog(Options options);
  ~Watchdog();

  zx_status_t Init(int32_t vmid);
  void SetCheckInterval(uint32_t interval_ms);
  zx_status_t StartMonitoring();
  void Heartbeat();
  void OnSuspend();
  void OnResume();
  void DumpState(uint32_t reason);

  void RegisterDumpStateCallback(DumpStateCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    dump_state_callback_ = std::move(callback);
  }

  void RegisterStopCallback(StopCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_callback_ = std::move(callback);
  }

  enum class PowerState : uint8_t {
    RUNNING,
    SUSPENDING,
    SUSPENDED,
    RESUMING,
  };

 private:
  void SetPowerState(PowerState newState) {
    power_state_.store(static_cast<uint8_t>(newState));
  }

  PowerState GetPowerState() const {
    uint8_t value = power_state_.load();
    return static_cast<PowerState>(value);
  }

  void StartMonitoringLocked(zx::time now);
  void SignalWorkerThread();
  bool ShouldStopThreadLocked() const;
  void ProcessExpiredDeadline(int32_t vmid, zx::time deadline);
  bool ShouldSkipTimeoutCheckLocked(int32_t vmid);
  void UpdateDeadlineLocked(zx::time now);

  friend class WatchdogTestPeer;

  zx::resource root_resource;
  zx::event wake_event_;
  std::thread wdt_thread_;
  mutable std::mutex mutex_;
  bool monitoring_started_ = false;
  bool monitoring_active_ = false;
  bool stop_thread_ = false;
  bool initialized_ = false;
  int32_t vmid_ = -1;
  uint64_t heartbeat_count_ = 0;
  zx::time last_heartbeat_at_ = zx::time(0);
  zx::time check_deadline_ = zx::time(0);
  DumpStateCallback dump_state_callback_;
  StopCallback stop_callback_;
  Options options_;
  std::atomic<uint8_t> power_state_{
      static_cast<uint8_t>(PowerState::RUNNING)};
};
}  // namespace machina
