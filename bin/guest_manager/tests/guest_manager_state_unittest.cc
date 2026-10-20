// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest_manager/guest_manager.h"

#include <vector>

#include "gtest/gtest.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/fidl/cpp/binding.h"

namespace {

guest_manager::Config MakeConfig(bool auto_start,
                                 int32_t delayed_launch_ms = 0) {
  guest_manager::Config cfg;
  cfg.set_vmid(1);
  cfg.set_name("test");
  cfg.set_auto_start(auto_start);
  cfg.set_delayed_launch_ms(delayed_launch_ms);
  return cfg;
}

std::unique_ptr<Guest> MakeGuest(async_t* async,
                                 guest_manager::Config cfg,
                                 machina::WatchdogPolicy watchdog_policy =
                                     machina::WatchdogPolicy(true),
                                 std::function<void()> state_change = nullptr,
                                 std::function<void()> platform_reset = nullptr) {
  return std::make_unique<Guest>(nullptr, async, std::move(cfg), watchdog_policy,
                                  std::move(state_change),
                                  std::move(platform_reset));
}

guest_manager::Config MakeSosConfig(bool auto_start) {
  auto cfg = MakeConfig(auto_start);
  cfg.set_vmid(ZX_USER_VMID_SOS);
  return cfg;
}

class TestGuestLifecycle : public virtualization::GuestLifecycle {
 public:
  explicit TestGuestLifecycle(async_t* async) : async_(async), binding_(this) {}

  zx_status_t Bind(zx::channel channel) {
    return binding_.Bind(
        fidl::InterfaceRequest<virtualization::GuestLifecycle>(
            std::move(channel)),
        async_);
  }

  void Create(virtualization::Config, CreateCallback callback) override {
    ++create_count_;
    if (defer_create) {
      create_callback_ = std::move(callback);
      return;
    }
    callback(create_result);
  }

  void Bind(fidl::InterfaceRequest<virtualization::GuestController>,
            BindCallback callback) override {
    if (respond_to_probe) {
      callback(probe_result);
    }
  }

  void Run(RunCallback callback) override {
    ++run_count_;
    run_callback_ = std::move(callback);
  }

  void Stop(StopCallback callback) override {
    // Keep Stop pending; ForceShutdown completion is driven by guest teardown.
    (void)callback;
    ++stop_count_;
  }

  size_t stop_count() const { return stop_count_; }
  size_t create_count() const { return create_count_; }
  size_t run_count() const { return run_count_; }
  void CompleteCreate(GuestError status) {
    auto callback = std::move(create_callback_);
    callback(status);
  }
  void CompleteRun(GuestError status) {
    auto callback = std::move(run_callback_);
    callback(status);
  }
  void Close() { binding_.Unbind(); }

  GuestError create_result = GuestError::OK;
  GuestError probe_result = GuestError::OK;
  bool defer_create = false;
  bool respond_to_probe = true;

 private:
  async_t* const async_;
  fidl::Binding<virtualization::GuestLifecycle> binding_;
  size_t stop_count_ = 0;
  size_t create_count_ = 0;
  size_t run_count_ = 0;
  CreateCallback create_callback_;
  RunCallback run_callback_;
};

class TestApplicationController : public component::ApplicationController {
 public:
  explicit TestApplicationController(Guest* guest, async_t* async)
      : binding_(this, guest->app_controller_.NewRequest(async), async) {}

  void Kill() override { ++kill_count_; }
  void Detach() override {}
  void Wait(WaitCallback) override {}
  void WaitLaunchResponse(WaitLaunchResponseCallback callback) override {
    callback(ZX_OK);
  }
  void KillAndWait(KillAndWaitCallback callback) override {
    Kill();
    callback();
  }
  size_t kill_count() const { return kill_count_; }

 private:
  fidl::Binding<component::ApplicationController> binding_;
  size_t kill_count_ = 0;
};

void BindLifecycle(Guest* guest,
                   TestGuestLifecycle* lifecycle,
                   async_t* async) {
  zx::channel client;
  zx::channel server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &client, &server));
  ASSERT_EQ(ZX_OK, guest->lifecycle_.Bind(std::move(client), async));
  guest->lifecycle_.set_error_handler([guest] { guest->HandleLifecycleClosed(); });
  ASSERT_EQ(ZX_OK, lifecycle->Bind(std::move(server)));
}

bool AreGuestsHealthy(const std::vector<Guest*>& guests) {
  for (const auto& guest : guests) {
    if (!guest->is_healthy_for_watchdog()) {
      return false;
    }
  }
  return true;
}

class GuestWatchdogStateTest : public ::testing::Test {
 protected:
  GuestWatchdogStateTest() : loop_(&kAsyncLoopConfigMakeDefault) {}

  async_t* async() { return loop_.async(); }

  async::Loop loop_;
};

TEST_F(GuestWatchdogStateTest,
       AutoStartGuestHasStartupWatchdogGraceByDefault) {
  auto guest = MakeGuest(async(), MakeConfig(true));

  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());

  guest->watchdog_grace_deadline_ = async::Now(async()) - zx::sec(1);
  EXPECT_FALSE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       ManualGuestDoesNotArmBeforeLaunch) {
  auto guest = MakeGuest(async(), MakeConfig(false));

  EXPECT_FALSE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       WatchdogObligationStaysActiveAcrossRelaunchPending) {
  auto guest = MakeGuest(async(), MakeConfig(false));

  guest->watchdog_obligation_active_ = true;
  guest->state_ = GuestStatus::RUNNING;
  guest->HandleGuestStopped(GuestError::INTERNAL_ERROR);

  EXPECT_EQ(GuestStatus::STOPPED, guest->state());
  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());

  guest->watchdog_grace_deadline_ = async::Now(async()) - zx::sec(1);
  EXPECT_FALSE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       ManualStopCancelsRelaunchPendingWatchdogObligation) {
  auto guest = MakeGuest(async(), MakeConfig(false));

  guest->watchdog_obligation_active_ = true;
  guest->state_ = GuestStatus::RUNNING;
  guest->HandleGuestStopped(GuestError::INTERNAL_ERROR);
  const auto pending_relaunch_generation = guest->supervision_generation_;

  bool callback_called = false;
  guest->ForceShutdown([&callback_called] { callback_called = true; });

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(GuestStatus::STOPPED, guest->state());
  EXPECT_FALSE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
  EXPECT_EQ(zx::time(0), guest->watchdog_grace_deadline_);
  EXPECT_NE(pending_relaunch_generation, guest->supervision_generation_);
}

TEST_F(GuestWatchdogStateTest,
       ManualStopSuppressesWatchdogObligation) {
  auto guest = MakeGuest(async(), MakeConfig(true));

  ASSERT_TRUE(guest->has_watchdog_obligation());
  guest->state_ = GuestStatus::STOPPING;
  guest->manual_stop_requested_ = true;
  guest->watchdog_obligation_active_ = false;
  guest->ClearWatchdogGrace();
  guest->HandleGuestStopped(GuestError::SHUTDOWN);

  EXPECT_EQ(GuestStatus::STOPPED, guest->state());
  EXPECT_FALSE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       AutoStartGuestWithLaunchDelayIsHealthyDuringStartupGrace) {
  auto guest = MakeGuest(async(), MakeConfig(true, 2000));

  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       ManualStopCancelsPendingDelayedLaunchWatchdogObligation) {
  auto guest = MakeGuest(async(), MakeConfig(true, 2000));

  guest->ScheduleAutoLaunch();
  const auto pending_launch_generation = guest->launch_generation_;

  bool callback_called = false;
  guest->ForceShutdown([&callback_called] { callback_called = true; });

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(GuestStatus::NOT_STARTED, guest->state());
  EXPECT_FALSE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
  EXPECT_EQ(zx::time(0), guest->watchdog_grace_deadline_);
  EXPECT_NE(pending_launch_generation, guest->launch_generation_);

  guest->LaunchIfCurrent(pending_launch_generation);
  EXPECT_EQ(GuestStatus::NOT_STARTED, guest->state());
  EXPECT_FALSE(guest->has_watchdog_obligation());
}

TEST_F(GuestWatchdogStateTest,
       RepeatedForceShutdownWhileStoppingQueuesCallbacksUntilLifecycleClose) {
  auto guest = MakeGuest(async(), MakeConfig(false));
  TestGuestLifecycle lifecycle(async());
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));

  guest->state_ = GuestStatus::RUNNING;

  bool first_callback_called = false;
  bool second_callback_called = false;
  guest->ForceShutdown(
      [&first_callback_called] { first_callback_called = true; });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());

  EXPECT_EQ(GuestStatus::STOPPING, guest->state());
  EXPECT_EQ(1u, lifecycle.stop_count());
  EXPECT_FALSE(first_callback_called);

  guest->ForceShutdown(
      [&second_callback_called] { second_callback_called = true; });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());

  EXPECT_EQ(GuestStatus::STOPPING, guest->state());
  EXPECT_EQ(1u, lifecycle.stop_count());
  EXPECT_FALSE(first_callback_called);
  EXPECT_FALSE(second_callback_called);

  guest->HandleLifecycleClosed();

  EXPECT_EQ(GuestStatus::STOPPED, guest->state());
  EXPECT_TRUE(first_callback_called);
  EXPECT_TRUE(second_callback_called);
}

TEST_F(GuestWatchdogStateTest,
       MultiGuestHealthOnlyFailsAfterActiveObligationGraceExpires) {
  auto required = MakeGuest(async(), MakeConfig(true));
  auto idle_manual = MakeGuest(async(), MakeConfig(false));

  EXPECT_TRUE(AreGuestsHealthy({required.get(), idle_manual.get()}));

  required->watchdog_grace_deadline_ = async::Now(async()) - zx::sec(1);
  EXPECT_FALSE(AreGuestsHealthy({required.get(), idle_manual.get()}));
}

TEST_F(GuestWatchdogStateTest,
       NoSinkCreateFailureLatchesBeforeCallbacksAndBlocksLaterLaunches) {
  size_t reset_count = 0;
  Guest* guest_ptr = nullptr;
  auto guest = MakeGuest(
      async(), MakeSosConfig(true), machina::WatchdogPolicy(false), nullptr,
      [&] {
        ++reset_count;
        EXPECT_TRUE(guest_ptr->is_waiting_for_platform_reset());
        EXPECT_TRUE(guest_ptr->has_watchdog_obligation());
        EXPECT_FALSE(guest_ptr->is_healthy_for_watchdog());
        guest_ptr->Launch([](GuestError error) {
          EXPECT_EQ(GuestError::REBOOT_REQUIRED, error);
        });
      });
  guest_ptr = guest.get();
  TestGuestLifecycle lifecycle(async());
  lifecycle.create_result = GuestError::INITIALIZATION_FAILED;
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));
  bool launch_replied = false;
  guest->Launch([&](GuestError error) {
    launch_replied = true;
    EXPECT_EQ(GuestError::INITIALIZATION_FAILED, error);
    EXPECT_EQ(1u, reset_count);
  });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  ASSERT_TRUE(launch_replied);
  EXPECT_EQ(1u, lifecycle.create_count());
  EXPECT_EQ(0u, lifecycle.run_count());
  EXPECT_EQ(GuestStatus::STOPPED, guest->state());

  guest->ScheduleAutoLaunch();
  guest->LaunchIfCurrent(guest->launch_generation_);
  guest->ScheduleRelaunch(GuestError::REBOOT_REQUIRED);
  guest->RequestStackRebuildForSilentHang();
  guest->HandleGuestStopped(GuestError::INTERNAL_ERROR);
  bool shutdown_replied = false;
  guest->ForceShutdown([&] { shutdown_replied = true; });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_TRUE(shutdown_replied);
  EXPECT_EQ(1u, reset_count);
  EXPECT_EQ(1u, lifecycle.create_count());
  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_FALSE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       NoSinkStuckCreateExpiresAndLateSuccessCannotRunGuest) {
  size_t reset_count = 0;
  auto guest = MakeGuest(async(), MakeSosConfig(true),
                          machina::WatchdogPolicy(false), nullptr,
                          [&] { ++reset_count; });
  TestGuestLifecycle lifecycle(async());
  lifecycle.defer_create = true;
  TestApplicationController controller(guest.get(), async());
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));
  bool launch_replied = false;
  guest->Launch([&](GuestError error) {
    launch_replied = true;
    EXPECT_EQ(GuestError::REBOOT_REQUIRED, error);
  });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  ASSERT_EQ(GuestStatus::STARTING, guest->state());
  ASSERT_FALSE(launch_replied);
  ASSERT_TRUE(guest->CheckWatchdogHealth());

  guest->watchdog_grace_deadline_ = async::Now(async()) - zx::sec(1);
  EXPECT_FALSE(guest->CheckWatchdogHealth());
  EXPECT_EQ(1u, reset_count);
  lifecycle.CompleteCreate(GuestError::OK);
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_TRUE(launch_replied);
  EXPECT_EQ(0u, lifecycle.run_count());
  EXPECT_EQ(0u, controller.kill_count());
  EXPECT_EQ(GuestStatus::STARTING, guest->state());
  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_FALSE(guest->is_healthy_for_watchdog());
}

TEST_F(GuestWatchdogStateTest,
       NoSinkForcedTeardownCannotBypassResetPolicy) {
  size_t reset_count = 0;
  auto guest = MakeGuest(async(), MakeSosConfig(true),
                          machina::WatchdogPolicy(false), nullptr,
                          [&] { ++reset_count; });
  guest->state_ = GuestStatus::STOPPING;
  guest->relaunch_after_forced_teardown_ = true;
  guest->HandleGuestStopped(GuestError::FORCE_STOPPED);
  EXPECT_EQ(1u, reset_count);
  EXPECT_TRUE(guest->is_waiting_for_platform_reset());
  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_FALSE(guest->is_healthy_for_watchdog());
  guest->Launch([](GuestError error) {
    EXPECT_EQ(GuestError::REBOOT_REQUIRED, error);
  });
}

TEST_F(GuestWatchdogStateTest,
       NoSinkLateCreateReplyDetectsExpiredGraceBeforeHealthPoll) {
  size_t reset_count = 0;
  auto guest = MakeGuest(async(), MakeSosConfig(true),
                          machina::WatchdogPolicy(false), nullptr,
                          [&] { ++reset_count; });
  TestGuestLifecycle lifecycle(async());
  lifecycle.defer_create = true;
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));
  guest->Launch([](GuestError error) {
    EXPECT_EQ(GuestError::REBOOT_REQUIRED, error);
  });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  guest->watchdog_grace_deadline_ = async::Now(async()) - zx::sec(1);
  lifecycle.CompleteCreate(GuestError::OK);
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_EQ(1u, reset_count);
  EXPECT_EQ(0u, lifecycle.run_count());
  EXPECT_TRUE(guest->is_waiting_for_platform_reset());
}

TEST_F(GuestWatchdogStateTest,
       NoSinkCreateChannelCloseRequestsReset) {
  size_t reset_count = 0;
  auto guest = MakeGuest(async(), MakeSosConfig(true),
                          machina::WatchdogPolicy(false), nullptr,
                          [&] { ++reset_count; });
  TestGuestLifecycle lifecycle(async());
  lifecycle.defer_create = true;
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));
  guest->Launch([](GuestError) {});
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  ASSERT_EQ(GuestStatus::STARTING, guest->state());
  lifecycle.Close();
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_EQ(1u, reset_count);
  EXPECT_EQ(GuestStatus::VMM_UNEXPECTED_TERMINATION, guest->state());
  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_FALSE(guest->is_healthy_for_watchdog());
}

struct WatchdogProfile {
  bool sink;
  int32_t user_vmid;
};

class GuestWatchdogPolicyTest
    : public GuestWatchdogStateTest,
      public ::testing::WithParamInterface<WatchdogProfile> {
 protected:
  std::unique_ptr<Guest> MakeProfileGuest(bool auto_start = true) {
    auto cfg = MakeConfig(auto_start);
    cfg.set_vmid(GetParam().user_vmid);
    return MakeGuest(async(), std::move(cfg),
                       machina::WatchdogPolicy(GetParam().sink), nullptr,
                       [this] { ++reset_count_; });
  }

  bool ExpectsPlatformReset() const {
    return !GetParam().sink && GetParam().user_vmid == ZX_USER_VMID_SOS;
  }

  size_t reset_count_ = 0;
};

TEST_P(GuestWatchdogPolicyTest, RunFailurePreservesRequiredSupervision) {
  auto guest = MakeProfileGuest();
  TestGuestLifecycle lifecycle(async());
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));
  guest->Launch([](GuestError error) { EXPECT_EQ(GuestError::OK, error); });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  ASSERT_EQ(1u, lifecycle.run_count());
  lifecycle.CompleteRun(GuestError::START_VCPU_FAILED);
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_EQ(GuestStatus::STOPPED, guest->state());
  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_EQ(ExpectsPlatformReset(), guest->is_waiting_for_platform_reset());
  EXPECT_EQ(ExpectsPlatformReset() ? 1u : 0u, reset_count_);
  EXPECT_EQ(!ExpectsPlatformReset(), guest->is_healthy_for_watchdog());

  TestGuestLifecycle recovered_lifecycle(async());
  ASSERT_NO_FATAL_FAILURE(
      BindLifecycle(guest.get(), &recovered_lifecycle, async()));
  // Let the real recovery task run. No-sink SOS must never create again.
  EXPECT_EQ(ZX_ERR_TIMED_OUT,
            loop_.Run(async::Now(async()) + zx::msec(1100)));
  EXPECT_EQ(ExpectsPlatformReset() ? 0u : 1u,
            recovered_lifecycle.create_count());
  EXPECT_EQ(ExpectsPlatformReset() ? GuestStatus::STOPPED : GuestStatus::RUNNING,
            guest->state());
}

TEST_P(GuestWatchdogPolicyTest, ManuallyLaunchedGuestCrashUsesProductPolicy) {
  auto guest = MakeProfileGuest(false);
  TestGuestLifecycle lifecycle(async());
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));
  guest->Launch([](GuestError error) { EXPECT_EQ(GuestError::OK, error); });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  lifecycle.Close();
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_EQ(GuestStatus::VMM_UNEXPECTED_TERMINATION, guest->state());
  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_EQ(ExpectsPlatformReset(), guest->is_waiting_for_platform_reset());
  EXPECT_EQ(ExpectsPlatformReset() ? 1u : 0u, reset_count_);
  EXPECT_EQ(!ExpectsPlatformReset(), guest->is_healthy_for_watchdog());
}

TEST_P(GuestWatchdogPolicyTest, ProbeTimeoutThresholdResetsOrRebuilds) {
  auto guest = MakeProfileGuest();
  TestGuestLifecycle lifecycle(async());
  lifecycle.respond_to_probe = false;
  TestApplicationController controller(guest.get(), async());
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));
  guest->Launch([](GuestError error) { EXPECT_EQ(GuestError::OK, error); });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());

  for (uint32_t i = 0; i < Guest::kManagementProbeFailureThreshold; ++i) {
    guest->IssueManagementProbe(guest->supervision_generation_);
    ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
    guest->OnManagementProbeTimeout(guest->supervision_generation_,
                                     guest->next_probe_id_);
  }
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_EQ(ExpectsPlatformReset() ? 1u : 0u, reset_count_);
  EXPECT_EQ(ExpectsPlatformReset() ? 0u : 1u, controller.kill_count());
  EXPECT_EQ(ExpectsPlatformReset(), guest->is_waiting_for_platform_reset());
  EXPECT_EQ(ExpectsPlatformReset() ? GuestStatus::RUNNING : GuestStatus::STOPPING,
            guest->state());
  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->lifecycle_.is_bound());

  if (ExpectsPlatformReset()) {
    guest->RequestStackRebuildForSilentHang();
    guest->ForceShutdown([] {});
    guest->HandleCreateResult(GuestError::OK, [](GuestError error) {
      EXPECT_EQ(GuestError::REBOOT_REQUIRED, error);
    });
    ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
    EXPECT_EQ(0u, lifecycle.stop_count());
    EXPECT_EQ(0u, controller.kill_count());
    EXPECT_EQ(1u, reset_count_);
    EXPECT_FALSE(guest->is_healthy_for_watchdog());
    EXPECT_TRUE(guest->has_watchdog_obligation());
  } else {
    lifecycle.Close();
    ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
    EXPECT_EQ(GuestStatus::STOPPED, guest->state());
    EXPECT_TRUE(guest->has_watchdog_obligation());
    EXPECT_TRUE(guest->is_healthy_for_watchdog());
  }
}

TEST_P(GuestWatchdogPolicyTest, NonOkProbeThresholdResetsOrRebuilds) {
  auto guest = MakeProfileGuest();
  TestGuestLifecycle lifecycle(async());
  lifecycle.probe_result = GuestError::INTERNAL_ERROR;
  TestApplicationController controller(guest.get(), async());
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));
  guest->Launch([](GuestError error) { EXPECT_EQ(GuestError::OK, error); });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  guest->IssueManagementProbe(guest->supervision_generation_);
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_EQ(0u, reset_count_);
  EXPECT_EQ(0u, controller.kill_count());
  guest->IssueManagementProbe(guest->supervision_generation_);
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_EQ(ExpectsPlatformReset() ? 1u : 0u, reset_count_);
  EXPECT_EQ(ExpectsPlatformReset() ? 0u : 1u, controller.kill_count());
  EXPECT_TRUE(guest->has_watchdog_obligation());
}

TEST_P(GuestWatchdogPolicyTest, NormalManualStopAllowsLaterManualLaunch) {
  auto guest = MakeProfileGuest();
  TestGuestLifecycle lifecycle(async());
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest.get(), &lifecycle, async()));
  guest->Launch([](GuestError error) { EXPECT_EQ(GuestError::OK, error); });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  bool shutdown_replied = false;
  guest->ForceShutdown([&] { shutdown_replied = true; });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_EQ(1u, lifecycle.stop_count());
  EXPECT_FALSE(shutdown_replied);
  lifecycle.Close();
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_TRUE(shutdown_replied);
  EXPECT_FALSE(guest->has_watchdog_obligation());
  EXPECT_TRUE(guest->is_healthy_for_watchdog());
  EXPECT_FALSE(guest->is_waiting_for_platform_reset());
  EXPECT_EQ(0u, reset_count_);

  TestGuestLifecycle relaunched_lifecycle(async());
  ASSERT_NO_FATAL_FAILURE(
      BindLifecycle(guest.get(), &relaunched_lifecycle, async()));
  guest->Launch([](GuestError error) { EXPECT_EQ(GuestError::OK, error); });
  ASSERT_EQ(ZX_OK, loop_.RunUntilIdle());
  EXPECT_EQ(GuestStatus::RUNNING, guest->state());
  EXPECT_EQ(1u, relaunched_lifecycle.run_count());
  EXPECT_TRUE(guest->has_watchdog_obligation());
  EXPECT_EQ(0u, reset_count_);
}

INSTANTIATE_TEST_SUITE_P(
    ProductProfiles, GuestWatchdogPolicyTest,
    ::testing::Values(WatchdogProfile{true, ZX_USER_VMID_SOS},
                      WatchdogProfile{false, ZX_USER_VMID_SOS},
                      WatchdogProfile{false, ZX_USER_VMID_TBOX}));

}  // namespace
