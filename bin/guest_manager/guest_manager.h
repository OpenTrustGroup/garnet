// SPDX-License-Identifier: BSD-3-Clause

#include <fuchsia/cpp/virtualization.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/zx/time.h>

#include <functional>

#include "garnet/bin/guest_manager/proto/config.pb.h"
#include "garnet/bin/guest_manager/guest_manager_reporter.h"
#include "garnet/lib/machina/vm_id.h"
#include "garnet/lib/machina/watchdog_policy.h"
#include "garnet/public/lib/app/cpp/application_context.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include <zircon/vmid.h>
#include "lib/nebula/base/macros.h"
#include "lib/nebula/base/thread_annotations.h"

constexpr char kVmmPackagePath[] = "file://sos";

using virtualization::GuestController;
using virtualization::GuestError;
using virtualization::GuestLifecyclePtr;
using virtualization::GuestManager;
using virtualization::GuestStatus;

inline const char* GuestStatusToString(GuestStatus status) {
  switch (status) {
    case GuestStatus::NOT_STARTED:
      return "not_started";
    case GuestStatus::STARTING:
      return "starting";
    case GuestStatus::RUNNING:
      return "running";
    case GuestStatus::STOPPING:
      return "stopping";
    case GuestStatus::STOPPED:
      return "stopped";
    case GuestStatus::VMM_UNEXPECTED_TERMINATION:
      return "vmm_unexpected_termination";
  }
  return "unknown";
}

inline const char* GuestErrorToString(GuestError error) {
  switch (error) {
    case GuestError::OK:
      return "ok";
    case GuestError::NOT_FOUND:
      return "not_found";
    case GuestError::ALREADY_RUNNING:
      return "already_running";
    case GuestError::REBOOT_REQUIRED:
      return "reboot_required";
    case GuestError::INITIALIZATION_FAILED:
      return "initialization_failed";
    case GuestError::START_VCPU_FAILED:
      return "start_vcpu_failed";
    case GuestError::OUT_OF_MEMORY:
      return "out_of_memory";
    case GuestError::FORCE_STOPPED:
      return "force_stopped";
    case GuestError::SHUTDOWN:
      return "shutdown";
    case GuestError::INTERNAL_ERROR:
      return "internal_error";
  }
  return "unknown";
}

class GuestManagerImpl;

class Guest {
 public:
  static constexpr zx::duration kManagementProbeInterval = zx::sec(15);
  static constexpr zx::duration kManagementProbeRetryInterval = zx::sec(1);
  static constexpr zx::duration kManagementProbeTimeout = zx::sec(5);
  static constexpr zx::duration kWatchdogStartupGrace = zx::sec(60);
  static constexpr zx::duration kWatchdogRecoveryGrace = zx::sec(90);
  static constexpr uint32_t kManagementProbeFailureThreshold = 2;

  Guest(component::ApplicationContext* application_context,
        async_t* async,
        guest_manager::Config cfg,
        machina::WatchdogPolicy watchdog_policy,
        std::function<void()> state_change_callback = nullptr,
        std::function<void()> platform_reset_callback = nullptr)
      : app_ctx_(application_context),
        async_(async),
        state_(GuestStatus::NOT_STARTED),
        cfg_(std::move(cfg)),
        watchdog_policy_(watchdog_policy),
        state_change_callback_(std::move(state_change_callback)),
        platform_reset_callback_(std::move(platform_reset_callback)) {
    watchdog_obligation_active_ = ShouldActivateWatchdogAtStartup();
    if (watchdog_obligation_active_) {
      ArmWatchdogStartupGrace(DelayedLaunchGrace());
    }
  }

  void Launch(GuestManager::LaunchCallback callback) {
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=launch_request state="
                  << GuestStatusToString(state_)
                  << " lifecycle_bound=" << lifecycle_.is_bound();
    CheckWatchdogHealth();
    if (platform_reset_requested_) {
      callback(GuestError::REBOOT_REQUIRED);
      return;
    }
    if (is_guest_started()) {
      FXL_LOG(WARNING) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                       << " event=launch_rejected reason=already_started state="
                       << GuestStatusToString(state_);
      callback(GuestError::ALREADY_RUNNING);
      return;
    }

    // if VMM app is not started, start it
    if (!lifecycle_.is_bound()) {
      component::ApplicationLaunchInfo launch_info;

      zx::channel h1, h2;
      FXL_CHECK(ZX_OK == zx::channel::create(0, &h1, &h2));
      lifecycle_.Bind(std::move(h1));

      lifecycle_.set_error_handler([this] { HandleLifecycleClosed(); });

      launch_info.url = kVmmPackagePath;
      launch_info.ta_session_request = std::move(h2);

      int log_level = cfg_.log_level();
      if (log_level < 0) {
        std::string log_level_arg = fxl::StringPrintf("--verbose=%d", -log_level);
        launch_info.arguments.push_back(log_level_arg);
      } else {
        std::string log_level_arg = fxl::StringPrintf("--quite=%d", log_level);
        launch_info.arguments.push_back(log_level_arg);
      }

      FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                    << " event=launch_component url=" << kVmmPackagePath
                    << " log_level=" << cfg_.log_level();
      app_ctx_->launcher()->CreateApplication(
          std::move(launch_info), app_controller_.NewRequest(async_));
    }

    manual_stop_requested_ = false;
    watchdog_obligation_active_ = true;
    if (!watchdog_recovery_active_) {
      ArmWatchdogStartupGrace(zx::msec(0));
    }
    state_ = GuestStatus::STARTING;
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=state_transition state="
                  << GuestStatusToString(state_)
                  << " watchdog_obligation=" << watchdog_obligation_active_;
    NotifyStateChange();
    virtualization::Config cfg;
    cfg.default_cfg = cfg_.default_cfg();
    cfg.bootloader = cfg_.bootloader();
    cfg.memory = cfg_.memory();
    cfg.memory_path = cfg_.memory_path();
    cfg.vmid = cfg_.vmid();
    cfg.log_level = cfg_.log_level();
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=lifecycle_create_request default_cfg="
                  << cfg_.default_cfg() << " bootloader=" << cfg_.bootloader();
    lifecycle_->Create(std::move(cfg), [this, callback = std::move(callback)](
                                           GuestError status) {
      this->HandleCreateResult(status, std::move(callback));
    });
  }

  void ScheduleAutoLaunch() {
    if (!is_auto_start_enabled() || platform_reset_requested_) {
      return;
    }

    const uint64_t generation = ++launch_generation_;
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=auto_launch_scheduled generation=" << generation
                  << " delayed_launch_ms=" << cfg_.delayed_launch_ms();
    if (cfg_.delayed_launch_ms() > 0) {
      async::PostDelayedTask(
          async_, [this, generation]() { LaunchIfCurrent(generation); },
          zx::msec(cfg_.delayed_launch_ms()));
      return;
    }

    LaunchIfCurrent(generation);
  }

  void LaunchIfCurrent(uint64_t generation) {
    if (generation != launch_generation_) {
      FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                    << " event=auto_launch_skipped generation=" << generation
                    << " current_generation=" << launch_generation_;
      return;
    }
    Launch([](GuestError) {});
  }

  void ForceShutdown(GuestManager::ForceShutdownCallback callback) {
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=force_shutdown_request state="
                  << GuestStatusToString(state_)
                  << " lifecycle_bound=" << lifecycle_.is_bound();
    CheckWatchdogHealth();
    if (platform_reset_requested_) {
      callback();
      return;
    }
    pending_force_shutdowns_.push_back(std::move(callback));
    MarkManualStopRequested();

    if (!lifecycle_.is_bound() || !is_guest_started()) {
      FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                    << " event=force_shutdown_complete reason=guest_not_running";
      // VMM component isn't running.
      NotifyStateChange();
      CompletePendingForceShutdowns();
      return;
    }

    if (state_ == GuestStatus::STOPPING) {
      FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                    << " event=force_shutdown_deduplicated state=stopping";
      NotifyStateChange();
      return;
    }

    state_ = GuestStatus::STOPPING;
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=state_transition state="
                  << GuestStatusToString(state_)
                  << " stop_reason=manual_stop";
    NotifyStateChange();
    lifecycle_->Stop([]() {});
  }

  void Connect(fidl::InterfaceRequest<virtualization::GuestController> request,
               GuestManager::ConnectCallback callback) {
    if (!lifecycle_.is_bound() || !is_guest_started()) {
      // VMM component isn't running.
      callback(GuestError::NOT_FOUND);
      return;
    }

    lifecycle_->Bind(std::move(request),
                     [callback = std::move(callback)](GuestError status) {
                       callback(status);
                     });
  }

  void HandleCreateResult(GuestError status,
                          GuestManager::LaunchCallback callback) {
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=lifecycle_create_result status="
                  << GuestErrorToString(status);
    CheckWatchdogHealth();
    if (platform_reset_requested_) {
      callback(GuestError::REBOOT_REQUIRED);
      return;
    }
    if (status != GuestError::OK) {
      if (!manual_stop_requested_) {
        RequestPlatformReset("guest_create_failed");
      }
      HandleGuestStopped(status);
    } else {
      state_ = GuestStatus::RUNNING;
      FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                    << " event=state_transition state="
                    << GuestStatusToString(state_);
      NotifyStateChange();
      ResetManagementSupervision();
      lifecycle_->Run(
          [this](GuestError status) { this->HandleGuestStopped(status); });
      ScheduleManagementProbe();
      OnGuestLaunched();
    }
    callback(status);
  }

  void HandleGuestStopped(GuestError status) {
    const bool was_stopping = state_ == GuestStatus::STOPPING;
    const bool restart_requested = relaunch_after_forced_teardown_;
    const bool manual_stop_requested = manual_stop_requested_;
    if (!manual_stop_requested &&
        (restart_requested ||
         (status != GuestError::FORCE_STOPPED &&
          status != GuestError::SHUTDOWN && status != GuestError::OK))) {
      RequestPlatformReset("guest_stopped_with_error");
    }
    relaunch_after_forced_teardown_ = false;
    manual_stop_requested_ = false;
    const bool should_relaunch =
        !platform_reset_requested_ && AllowsRelaunch() &&
        !manual_stop_requested &&
        (restart_requested ||
         (!was_stopping && ShouldRelaunchAfterStop(status, false)));
    ++supervision_generation_;
    ResetManagementSupervision();
    if (was_stopping && !restart_requested &&
        status != GuestError::FORCE_STOPPED &&
        status != GuestError::SHUTDOWN &&
        status != GuestError::OK) {
      FXL_LOG(ERROR) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                     << " event=stop_completed_with_error status="
                     << GuestErrorToString(status);
    }
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=guest_stopped status="
                  << GuestErrorToString(status)
                  << " was_stopping=" << was_stopping
                  << " restart_requested=" << restart_requested
                  << " manual_stop_requested=" << manual_stop_requested
                  << " relaunch=" << should_relaunch;
    watchdog_obligation_active_ = platform_reset_requested_ || should_relaunch;
    if (should_relaunch) {
      ArmWatchdogRecoveryGrace();
    } else {
      ClearWatchdogGrace();
    }
    state_ = GuestStatus::STOPPED;
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=state_transition state="
                  << GuestStatusToString(state_);
    NotifyStateChange();

    CompletePendingForceShutdowns();
    OnGuestStopped();

    if (should_relaunch) {
      ScheduleRelaunch(status);
    }
  }

  void HandleLifecycleClosed() {
    if (!is_guest_started()) {
      FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                    << " event=lifecycle_closed state="
                    << GuestStatusToString(state_)
                    << " reason=guest_not_started";
      lifecycle_.Unbind();
      return;
    }
    const bool was_stopping = state_ == GuestStatus::STOPPING;

    ++supervision_generation_;
    ResetManagementSupervision();

    const bool restart_requested = relaunch_after_forced_teardown_;
    const bool manual_stop_requested = manual_stop_requested_;
    relaunch_after_forced_teardown_ = false;
    manual_stop_requested_ = false;
    const bool unexpected_termination =
        !was_stopping && !restart_requested && !manual_stop_requested;
    if (unexpected_termination ||
        (restart_requested && !manual_stop_requested)) {
      RequestPlatformReset("unexpected_lifecycle_close");
    }
    const bool should_relaunch =
        !platform_reset_requested_ && AllowsRelaunch() &&
        !manual_stop_requested &&
        (restart_requested ||
         (ShouldAutoRelaunch() && unexpected_termination));

    FXL_LOG(WARNING) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                     << " event=lifecycle_closed was_stopping=" << was_stopping
                     << " restart_requested=" << restart_requested
                     << " manual_stop_requested=" << manual_stop_requested
                     << " unexpected_termination=" << unexpected_termination
                     << " relaunch=" << should_relaunch;

    watchdog_obligation_active_ = platform_reset_requested_ || should_relaunch;
    if (should_relaunch) {
      ArmWatchdogRecoveryGrace();
    } else {
      ClearWatchdogGrace();
    }
    state_ = unexpected_termination ? GuestStatus::VMM_UNEXPECTED_TERMINATION
                                    : GuestStatus::STOPPED;
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=state_transition state="
                  << GuestStatusToString(state_);
    NotifyStateChange();
    CompletePendingForceShutdowns();
    OnGuestStopped();

    if (should_relaunch) {
      ScheduleRelaunch(GuestError::INTERNAL_ERROR);
    }
  }

  void ScheduleManagementProbe();
  void IssueManagementProbe(uint64_t generation);
  void OnManagementProbeResponse(uint64_t generation,
                                 uint64_t probe_id,
                                 GuestError status);
  void OnManagementProbeTimeout(uint64_t generation, uint64_t probe_id);
  void OnManagementProbeFailure(uint64_t generation,
                                uint64_t probe_id,
                                std::string reason);
  void RequestStackRebuildForSilentHang();
  void RequestPlatformReset(const char* reason);
  void ResetManagementSupervision();
  void NotifyStateChange() {
    if (state_change_callback_) {
      state_change_callback_();
    }
  }

  void MarkManualStopRequested() {
    if (platform_reset_requested_) {
      return;
    }
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=mark_manual_stop state="
                  << GuestStatusToString(state_);
    manual_stop_requested_ = true;
    relaunch_after_forced_teardown_ = false;
    watchdog_obligation_active_ = false;
    ++launch_generation_;
    ++supervision_generation_;
    ResetManagementSupervision();
    ClearWatchdogGrace();
  }

  bool ShouldRelaunchAfterStop(GuestError status,
                               bool restart_requested) const {
    if (platform_reset_requested_ || !AllowsRelaunch()) {
      return false;
    }
    if (restart_requested || status == GuestError::REBOOT_REQUIRED) {
      return true;
    }
    return status != GuestError::FORCE_STOPPED &&
           status != GuestError::SHUTDOWN &&
           status != GuestError::OK;
  }

  void ScheduleRelaunch(GuestError reason) {
    if (platform_reset_requested_ || !AllowsRelaunch()) {
      FXL_LOG(WARNING) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                       << " event=relaunch_blocked reason=no_sink_sos";
      return;
    }
    const uint64_t generation = supervision_generation_;
    FXL_LOG(WARNING) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                     << " event=relaunch_scheduled reason="
                     << GuestErrorToString(reason)
                     << " generation=" << generation
                     << " delay_ms="
                     << (kManagementProbeRetryInterval / zx::msec(1));
    async::PostDelayedTask(
        async_, [this, generation]() {
          if (generation != supervision_generation_ || is_guest_started()) {
            FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                          << " event=relaunch_skipped generation="
                          << generation
                          << " current_generation=" << supervision_generation_
                          << " already_started=" << is_guest_started();
            return;
          }
          Launch([name = cfg_.name(), vmid = cfg_.vmid()](GuestError status) {
            if (status != GuestError::OK) {
              FXL_LOG(ERROR) << "guest=" << name << " vmid=" << vmid
                             << " event=relaunch_failed status="
                             << GuestErrorToString(status);
            }
          });
        },
        kManagementProbeRetryInterval);
  }

  void OnGuestLaunched() {
    ClearWatchdogGrace();
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=guest_launched state="
                  << GuestStatusToString(state_);
  }

  void CompletePendingForceShutdowns() {
    for (auto& pending_force_shutdown : pending_force_shutdowns_) {
      auto callback = std::move(pending_force_shutdown);
      callback();
    }
    pending_force_shutdowns_.clear();
  }

  void OnGuestStopped() {
    FXL_LOG(INFO) << "guest=" << cfg_.name() << " vmid=" << cfg_.vmid()
                  << " event=guest_stopped_final state="
                  << GuestStatusToString(state_);
    lifecycle_.Unbind();
  }

  bool AllowsRelaunch() const {
    return watchdog_policy_.AllowsRelaunch(cfg_.vmid());
  }

  GuestStatus state() const { return state_; }
  guest_manager::Config cfg() const { return cfg_; }

  ~Guest() = default;

  bool is_guest_started() const {
    switch (state_) {
      case GuestStatus::STARTING:
      case GuestStatus::RUNNING:
      case GuestStatus::STOPPING:
        return true;
      default:
        return false;
    }
  }

  bool is_auto_start_enabled() const { return cfg_.auto_start(); }
  int32_t delayed_launch_ms() const { return cfg_.delayed_launch_ms(); }
  bool has_watchdog_obligation() const { return watchdog_obligation_active_; }
  bool is_waiting_for_platform_reset() const {
    return platform_reset_requested_;
  }
  bool is_healthy_for_watchdog() const {
    return !platform_reset_requested_ &&
           (!watchdog_obligation_active_ || state_ == GuestStatus::RUNNING ||
            IsWithinWatchdogGrace());
  }
  bool CheckWatchdogHealth();

  bool ShouldAutoRelaunch() const {
    return cfg_.auto_start() || watchdog_obligation_active_;
  }

  bool ShouldActivateWatchdogAtStartup() const {
    return cfg_.auto_start();
  }

  zx::duration DelayedLaunchGrace() const {
    if (cfg_.delayed_launch_ms() <= 0) {
      return zx::msec(0);
    }
    return zx::msec(cfg_.delayed_launch_ms());
  }

  void ArmWatchdogStartupGrace(zx::duration launch_delay) {
    watchdog_grace_deadline_ =
        async::Now(async_) + launch_delay + kWatchdogStartupGrace;
  }

  void ArmWatchdogRecoveryGrace() {
    if (watchdog_recovery_active_) {
      return;
    }
    watchdog_recovery_active_ = true;
    watchdog_grace_deadline_ = async::Now(async_) + kWatchdogRecoveryGrace;
  }

  void ClearWatchdogGrace() {
    watchdog_recovery_active_ = false;
    watchdog_grace_deadline_ = zx::time(0);
  }

  bool IsWithinWatchdogGrace() const {
    return watchdog_grace_deadline_ != zx::time(0) &&
           async::Now(async_) <= watchdog_grace_deadline_;
  }

  component::ApplicationContext* const app_ctx_;
  async_t* async_;
  component::ApplicationControllerPtr app_controller_;
  GuestLifecyclePtr lifecycle_;
  std::vector<GuestManager::ForceShutdownCallback> pending_force_shutdowns_;
  GuestStatus state_;
  guest_manager::Config cfg_;
  zx::time active_probe_deadline_ = zx::time(0);
  uint64_t supervision_generation_ = 0;
  uint64_t next_probe_id_ = 0;
  uint32_t consecutive_probe_failures_ = 0;
  bool probe_pending_ = false;
  bool relaunch_after_forced_teardown_ = false;
  bool manual_stop_requested_ = false;
  bool watchdog_obligation_active_ = false;
  bool watchdog_recovery_active_ = false;
  const machina::WatchdogPolicy watchdog_policy_;
  bool platform_reset_requested_ = false;
  zx::time watchdog_grace_deadline_ = zx::time(0);
  uint64_t launch_generation_ = 0;
  std::function<void()> state_change_callback_;
  std::function<void()> platform_reset_callback_;
};

class GuestManagerImpl : public GuestManager {
 public:
  static std::unique_ptr<GuestManagerImpl> BuildFromFile(
      component::ApplicationContext* application_context,
      async_t* async,
      std::string path,
      machina::WatchdogPolicy watchdog_policy);

  GuestManagerImpl(component::ApplicationContext* app_context,
                   async_t* async,
                   machina::WatchdogPolicy watchdog_policy);

  void LaunchGuests();
  bool AreWatchdogObligationsHealthy();
  bool HasWatchdogObligation() const;
  void SyncWatchdogReporter();
  void RequestPlatformReset();

  // |virtualization::GuestManager|
  void Launch(int8_t vmid, LaunchCallback callback) override;
  void Connect(int8_t vmid,
               fidl::InterfaceRequest<GuestController> request,
               ConnectCallback) override;
  void ForceShutdown(int8_t vmid, ForceShutdownCallback callback) override;
  void Show(ShowCallback callback) override;

 private:
  friend class GuestManagerTestPeer;

  component::ApplicationContext* const application_context_;
  async_t* const async_ __UNUSED;

  fidl::BindingSet<GuestManager> bindings_;

  std::unordered_map<int8_t, std::unique_ptr<Guest>> guests_;
  GuestManagerReporter reporter_;
  const machina::WatchdogPolicy watchdog_policy_;

  static std::unique_ptr<GuestManagerImpl> Build(
      component::ApplicationContext* application_context,
      async_t* async,
      guest_manager::ManagerConfig cfg,
      machina::WatchdogPolicy watchdog_policy);
};
