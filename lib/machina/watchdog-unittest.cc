// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/lib/machina/watchdog.h"

#include <atomic>
#include <mutex>

#include "gtest/gtest.h"

namespace machina {

class WatchdogTestPeer {
 public:
  static void Arm(Watchdog* watchdog,
                  int32_t vmid,
                  zx::time last_heartbeat,
                  zx::time deadline) {
    std::lock_guard<std::mutex> lock(watchdog->mutex_);
    watchdog->vmid_ = vmid;
    watchdog->initialized_ = true;
    watchdog->monitoring_started_ = true;
    watchdog->monitoring_active_ = true;
    watchdog->last_heartbeat_at_ = last_heartbeat;
    watchdog->check_deadline_ = deadline;
    watchdog->SetPowerState(Watchdog::PowerState::RUNNING);
  }

  static void Expire(Watchdog* watchdog, int32_t vmid, zx::time deadline) {
    watchdog->ProcessExpiredDeadline(vmid, deadline);
  }
};

namespace {

bool WaitForCount(const std::atomic<uint32_t>& value,
                  uint32_t expected,
                  zx::duration timeout) {
  const zx::time deadline = zx::deadline_after(timeout);
  while (zx::clock::get(ZX_CLOCK_MONOTONIC) < deadline) {
    if (value.load() >= expected) {
      return true;
    }
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }
  return value.load() >= expected;
}

class WatchdogTest : public ::testing::Test {
 protected:
  WatchdogTest() {
    options_.get_root_resource = [](zx::resource*) { return ZX_OK; };
    options_.now = []() { return zx::clock::get(ZX_CLOCK_MONOTONIC); };
    options_.get_pm_state = [](zx_handle_t, int32_t, int32_t* pm_state) {
      *pm_state = 1;
      return ZX_OK;
    };
    options_.check_interval = zx::msec(20);
    options_.wakeup_delay_tolerance = zx::msec(5);
  }

  Watchdog::Options options_;
};

TEST_F(WatchdogTest, StartMonitoringWaitsForFirstHeartbeat) {
  Watchdog watchdog(options_);
  std::atomic<uint32_t> dumps{0};
  std::atomic<uint32_t> stops{0};
  watchdog.RegisterDumpStateCallback([&dumps](uint32_t) { ++dumps; });
  watchdog.RegisterStopCallback([&stops]() { ++stops; });

  ASSERT_EQ(ZX_OK, watchdog.Init(1));
  ASSERT_EQ(ZX_OK, watchdog.StartMonitoring());

  zx::nanosleep(zx::deadline_after(zx::msec(10)));

  EXPECT_EQ(0u, dumps.load());
  EXPECT_EQ(0u, stops.load());
}

TEST_F(WatchdogTest, HeartbeatArmsTimeoutCallback) {
  Watchdog watchdog(options_);
  std::atomic<uint32_t> dumps{0};
  std::atomic<uint32_t> stops{0};
  watchdog.RegisterDumpStateCallback([&dumps](uint32_t) { ++dumps; });
  watchdog.RegisterStopCallback([&stops]() { ++stops; });

  ASSERT_EQ(ZX_OK, watchdog.Init(1));
  ASSERT_EQ(ZX_OK, watchdog.StartMonitoring());
  watchdog.Heartbeat();

  EXPECT_TRUE(WaitForCount(dumps, 1, zx::msec(80)));
  EXPECT_TRUE(WaitForCount(stops, 1, zx::msec(80)));
}

TEST_F(WatchdogTest, SuspendSkipsExpiredDeadline) {
  Watchdog watchdog(options_);
  std::atomic<uint32_t> dumps{0};
  std::atomic<uint32_t> stops{0};
  watchdog.RegisterDumpStateCallback([&dumps](uint32_t) { ++dumps; });
  watchdog.RegisterStopCallback([&stops]() { ++stops; });

  ASSERT_EQ(ZX_OK, watchdog.Init(1));
  ASSERT_EQ(ZX_OK, watchdog.StartMonitoring());
  watchdog.Heartbeat();
  watchdog.OnSuspend();

  zx::nanosleep(zx::deadline_after(zx::msec(40)));

  EXPECT_EQ(0u, dumps.load());
  EXPECT_EQ(0u, stops.load());
}

TEST_F(WatchdogTest, ResumeRefreshesDeadline) {
  Watchdog watchdog(options_);
  std::atomic<uint32_t> dumps{0};
  std::atomic<uint32_t> stops{0};
  watchdog.RegisterDumpStateCallback([&dumps](uint32_t) { ++dumps; });
  watchdog.RegisterStopCallback([&stops]() { ++stops; });

  ASSERT_EQ(ZX_OK, watchdog.Init(1));
  ASSERT_EQ(ZX_OK, watchdog.StartMonitoring());
  watchdog.Heartbeat();
  zx::nanosleep(zx::deadline_after(zx::msec(10)));
  watchdog.OnResume();
  zx::nanosleep(zx::deadline_after(zx::msec(15)));

  EXPECT_EQ(0u, dumps.load());
  EXPECT_EQ(0u, stops.load());
}

TEST_F(WatchdogTest, StaleDeadlineIsIgnored) {
  zx::time now = zx::time(1060);
  options_.now = [&now]() { return now; };
  Watchdog watchdog(options_);
  std::atomic<uint32_t> dumps{0};
  std::atomic<uint32_t> stops{0};
  watchdog.RegisterDumpStateCallback([&dumps](uint32_t) { ++dumps; });
  watchdog.RegisterStopCallback([&stops]() { ++stops; });

  WatchdogTestPeer::Arm(&watchdog, 1, zx::time(1000), zx::time(1050));
  WatchdogTestPeer::Expire(&watchdog, 1, zx::time(1020));

  EXPECT_EQ(0u, dumps.load());
  EXPECT_EQ(0u, stops.load());
}

TEST_F(WatchdogTest, BoundedGraceDefersOnlyOnce) {
  options_.check_interval = zx::nsec(20);
  options_.wakeup_delay_tolerance = zx::nsec(5);
  zx::time now = zx::time(1021);
  options_.now = [&now]() { return now; };
  Watchdog watchdog(options_);
  std::atomic<uint32_t> dumps{0};
  std::atomic<uint32_t> stops{0};
  watchdog.RegisterDumpStateCallback([&dumps](uint32_t) { ++dumps; });
  watchdog.RegisterStopCallback([&stops]() { ++stops; });

  WatchdogTestPeer::Arm(&watchdog, 1, zx::time(1000), zx::time(1020));
  WatchdogTestPeer::Expire(&watchdog, 1, zx::time(1020));
  EXPECT_EQ(0u, dumps.load());
  EXPECT_EQ(0u, stops.load());

  now = zx::time(1026);
  WatchdogTestPeer::Expire(&watchdog, 1, zx::time(1025));
  EXPECT_EQ(1u, dumps.load());
  EXPECT_EQ(1u, stops.load());
}

}  // namespace
}  // namespace machina
