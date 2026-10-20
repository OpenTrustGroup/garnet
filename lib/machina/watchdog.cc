// SPDX-License-Identifier: BSD-3-Clause

#include <fcntl.h>
#include <unistd.h>

#include <lib/zx/event.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>
#include <zircon/device/sysinfo.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/vmid.h>

#include "garnet/lib/machina/watchdog.h"
#include "lib/fxl/logging.h"

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
#define GUEST_PM_SYSTEM_OFF          0
#define GUEST_PM_RUNNING             1
#define GUEST_PM_SUSPEND_TO_RAM      2
#define GUEST_PM_SUSPEND_TO_IDLE     3

static zx_status_t get_root_resource(zx::resource* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle = ZX_HANDLE_INVALID;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  close(fd);
  if (n < 0) {
    resource->reset();
    return ZX_ERR_IO;
  }
  resource->reset(rsc_handle);
  return ZX_OK;
}

static zx_status_t get_pm_state(zx_handle_t resource, int32_t vmid,
                                int32_t* pm_state) {
  return zx_get_pmstate_form_vmid(resource, vmid, pm_state);
}

namespace machina {

namespace {

constexpr zx_signals_t kWatchdogWakeSignal = ZX_USER_SIGNAL_0;
constexpr zx::time kNoWatchdogDeadline = zx::time(0);

zx::time MonotonicNow() {
  return zx::clock::get(ZX_CLOCK_MONOTONIC);
}

}  // namespace

Watchdog::Watchdog() : Watchdog(Options{}) {}

Watchdog::Watchdog(Options options) : options_(std::move(options)) {
  if (!options_.get_root_resource) {
    options_.get_root_resource = get_root_resource;
  }
  if (!options_.now) {
    options_.now = MonotonicNow;
  }
  if (!options_.get_pm_state) {
    options_.get_pm_state = get_pm_state;
  }
  const zx_status_t status = zx::event::create(0, &wake_event_);
  FXL_CHECK(status == ZX_OK) << "Failed to create watchdog wake event: "
                             << status;
  last_heartbeat_at_ = kNoWatchdogDeadline;
  check_deadline_ = kNoWatchdogDeadline;
}

Watchdog::~Watchdog() {
  {
    std::unique_lock<std::mutex> lk(mutex_);
    stop_thread_ = true;
  }
  SignalWorkerThread();
  if (wdt_thread_.joinable()) {
    wdt_thread_.join();
  }
}

void Watchdog::SetCheckInterval(uint32_t interval_ms) {
  if (interval_ms == 0) {
    FXL_LOG(WARNING) << "Watchdog check interval must be non-zero";
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  options_.check_interval = zx::msec(interval_ms);
}

zx_status_t Watchdog::Init(int32_t vmid) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (initialized_) {
      FXL_LOG(ERROR) << "Watchdog already initialized for vmid: "
                     << zx_log_guest_vmid(vmid);
      return ZX_ERR_BAD_STATE;
    }
  }

  zx_status_t status = options_.get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get hypervisor resource for vmid "
                   << zx_log_guest_vmid(vmid)
                   << ": " << status;
    return status;
  }

  FXL_LOG(INFO) << "Watchdog init for vmid: " << zx_log_guest_vmid(vmid);
  {
    std::lock_guard<std::mutex> lk(mutex_);
    vmid_ = vmid;
    stop_thread_ = false;
    initialized_ = true;
  }
  wdt_thread_ = std::thread([this, vmid]() {
    while (true) {
      zx::time deadline;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ShouldStopThreadLocked()) {
          break;
        }
        deadline = monitoring_active_ ? check_deadline_ : zx::time::infinite();
      }

      const zx_status_t wait_status =
          wake_event_.wait_one(kWatchdogWakeSignal, deadline, nullptr);
      wake_event_.signal(kWatchdogWakeSignal, 0);

      if (wait_status == ZX_OK) {
        continue;
      }
      if (wait_status == ZX_ERR_TIMED_OUT) {
        ProcessExpiredDeadline(vmid, deadline);
        continue;
      }

      FXL_LOG(ERROR) << "Watchdog wait failed for vmid "
                     << zx_log_guest_vmid(vmid)
                     << ": " << wait_status;
      break;
    }
  });
  return ZX_OK;
}

zx_status_t Watchdog::StartMonitoring() {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!initialized_) {
    FXL_LOG(ERROR) << "Cannot start VM watchdog monitoring before init";
    return ZX_ERR_BAD_STATE;
  }

  monitoring_started_ = true;
  monitoring_active_ = false;
  check_deadline_ = kNoWatchdogDeadline;
  FXL_LOG(INFO) << "Start VM watchdog monitoring for vmid " << vmid_
                << ", waiting for first heartbeat to arm timeout";
  SetPowerState(PowerState::RUNNING);
  SignalWorkerThread();
  return ZX_OK;
}

void Watchdog::Heartbeat() {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!initialized_) {
    FXL_LOG(ERROR) << "Ignore VM watchdog heartbeat before init";
    return;
  }

  const bool first_heartbeat = !monitoring_active_;
  SetPowerState(PowerState::RUNNING);
  const zx::time now = options_.now();
  last_heartbeat_at_ = now;
  ++heartbeat_count_;
  if (!monitoring_started_) {
    // Backstop old call sites that have not called StartMonitoring(). A guest
    // that never sends any heartbeat must not be treated as an armed timeout.
    monitoring_started_ = true;
  }
  if (!monitoring_active_) {
    StartMonitoringLocked(now);
    if (first_heartbeat) {
      FXL_LOG(INFO) << "First VM watchdog heartbeat armed timeout for vmid "
                    << vmid_ << " deadline=" << check_deadline_.get();
    }
  } else {
    UpdateDeadlineLocked(now);
  }
  SignalWorkerThread();
}

void Watchdog::OnSuspend() {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!initialized_) {
    FXL_LOG(ERROR) << "Ignore VM watchdog suspend before init";
    return;
  }

  FXL_LOG(INFO) << "event=vm_watchdog_suspend vmid=" << vmid_;
  SetPowerState(PowerState::SUSPENDED);
  SignalWorkerThread();
}

void Watchdog::OnResume() {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!initialized_) {
    FXL_LOG(ERROR) << "Ignore VM watchdog resume before init";
    return;
  }

  FXL_LOG(INFO) << "event=vm_watchdog_resume vmid=" << vmid_;
  SetPowerState(PowerState::RUNNING);
  if (monitoring_active_) {
    UpdateDeadlineLocked(options_.now());
  }
  SignalWorkerThread();
}

void Watchdog::StartMonitoringLocked(zx::time now) {
  monitoring_started_ = true;
  monitoring_active_ = true;
  last_heartbeat_at_ = now;
  UpdateDeadlineLocked(now);
}

void Watchdog::DumpState(uint32_t reason) {
  DumpStateCallback callback;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    callback = dump_state_callback_;
  }
  if (callback) {
    callback(reason);
  }
}

void Watchdog::SignalWorkerThread() {
  if (!wake_event_.is_valid()) {
    return;
  }

  const zx_status_t status = wake_event_.signal(0, kWatchdogWakeSignal);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to signal watchdog wake event for vmid " << vmid_
                   << ": " << status;
  }
}

bool Watchdog::ShouldStopThreadLocked() const {
  return stop_thread_;
}

void Watchdog::UpdateDeadlineLocked(zx::time now) {
  check_deadline_ = now + options_.check_interval;
}

void Watchdog::ProcessExpiredDeadline(int32_t vmid, zx::time deadline) {
  DumpStateCallback dump_callback;
  StopCallback stop_callback;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!monitoring_active_) {
      FXL_LOG(INFO) << "Ignore VM watchdog timeout for vmid " << vmid_
                    << " because monitoring is inactive."
                    << " now=" << options_.now().get()
                    << " last_heartbeat=" << last_heartbeat_at_.get()
                    << " heartbeat_count=" << heartbeat_count_;
      return;
    }
    if (check_deadline_ != deadline) {
      FXL_LOG(INFO) << "Ignore stale VM watchdog timeout for vmid " << vmid_
                    << " after deadline refresh."
                    << " expired=" << deadline.get()
                    << " current=" << check_deadline_.get()
                    << " last_heartbeat=" << last_heartbeat_at_.get()
                    << " heartbeat_count=" << heartbeat_count_;
      return;
    }

    const zx::time now = options_.now();
    if (now < deadline) {
      FXL_LOG(INFO) << "Ignore VM watchdog timeout for vmid " << vmid_
                    << " before deadline."
                    << " now=" << now.get() << " deadline=" << deadline.get()
                    << " last_heartbeat=" << last_heartbeat_at_.get()
                    << " heartbeat_count=" << heartbeat_count_;
      return;
    }

    if (ShouldSkipTimeoutCheckLocked(vmid)) {
      FXL_LOG(INFO) << "Skip VM watchdog timeout for vmid " << vmid_
                    << " while guest is suspended.";
      UpdateDeadlineLocked(now);
      return;
    }

    const zx::time heartbeat_deadline =
        last_heartbeat_at_ + options_.check_interval;
    const zx::time hard_deadline =
        heartbeat_deadline + options_.wakeup_delay_tolerance;
    // The grace window is bounded by the last observed heartbeat. It may defer
    // the first soft deadline caused by host scheduling delay, but it must not
    // keep extending the watchdog window without a new guest heartbeat.
    if (deadline < hard_deadline && now <= hard_deadline) {
      FXL_LOG(INFO) << "Defer VM watchdog timeout for vmid " << vmid_
                    << " inside bounded grace window."
                    << " deadline=" << deadline.get()
                    << " now=" << now.get()
                    << " hard_deadline=" << hard_deadline.get()
                    << " tolerance(ns)="
                    << options_.wakeup_delay_tolerance.get()
                    << " last_heartbeat=" << last_heartbeat_at_.get()
                    << " heartbeat_count=" << heartbeat_count_;
      check_deadline_ = hard_deadline;
      return;
    }

    dump_callback = dump_state_callback_;
    stop_callback = stop_callback_;
    monitoring_active_ = false;
    check_deadline_ = kNoWatchdogDeadline;
  }

  FXL_LOG(ERROR) << "event=vm_watchdog_timeout vmid=" << vmid
                 << " deadline=" << deadline.get();
  if (dump_callback) {
    dump_callback(1);
  }
  if (stop_callback) {
    stop_callback();
    FXL_LOG(WARNING) << "event=vm_watchdog_notify_controller vmid=" << vmid
                     << " stop_reason=watchdog_timeout";
  }
}

bool Watchdog::ShouldSkipTimeoutCheckLocked(int32_t vmid) {
  // TODO: Need to consider the scenario where HWT (Hardware Watchdog Trigger)
  // occurs during the period from the suspension of Yocto WDT thread to the
  // complete success of Yocto system suspension.
  if (GetPowerState() == PowerState::SUSPENDED) {
    return true;
  }

  int32_t pm_state = GUEST_PM_RUNNING;
  if (options_.get_pm_state(root_resource.get(), vmid, &pm_state) != ZX_OK) {
    return false;
  }

  if (pm_state == GUEST_PM_SUSPEND_TO_RAM ||
      pm_state == GUEST_PM_SUSPEND_TO_IDLE) {
    FXL_LOG(INFO) << "VM is in suspend state, skip watchdog check for vmid "
                  << vmid;
    return true;
  }

  return false;
}
}  // namespace machina
