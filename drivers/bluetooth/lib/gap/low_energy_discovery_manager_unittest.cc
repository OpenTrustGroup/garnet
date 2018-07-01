// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/low_energy_discovery_manager.h"

#include <unordered_set>
#include <vector>

#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_device.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace gap {
namespace {

using ::btlib::testing::FakeController;
using ::btlib::testing::FakeDevice;

using TestingBase = ::btlib::testing::FakeControllerTest<FakeController>;

const common::DeviceAddress kAddress0(common::DeviceAddress::Type::kLEPublic,
                                      "00:00:00:00:00:01");
const common::DeviceAddress kAddress1(common::DeviceAddress::Type::kLERandom,
                                      "00:00:00:00:00:02");
const common::DeviceAddress kAddress2(common::DeviceAddress::Type::kLEPublic,
                                      "00:00:00:00:00:03");
const common::DeviceAddress kAddress3(common::DeviceAddress::Type::kLEPublic,
                                      "00:00:00:00:00:04");

constexpr int64_t kTestScanPeriodMs = 10000;

class LowEnergyDiscoveryManagerTest : public TestingBase {
 public:
  LowEnergyDiscoveryManagerTest() = default;
  ~LowEnergyDiscoveryManagerTest() override = default;

  void SetUp() override {
    TestingBase::SetUp();

    scan_enabled_ = false;

    FakeController::Settings settings;
    settings.ApplyLegacyLEConfig();
    test_device()->set_settings(settings);

    discovery_manager_ = std::make_unique<LowEnergyDiscoveryManager>(
        Mode::kLegacy, transport(), &device_cache_);
    test_device()->SetScanStateCallback(
        std::bind(&LowEnergyDiscoveryManagerTest::OnScanStateChanged, this,
                  std::placeholders::_1),
        dispatcher());
    test_device()->StartCmdChannel(test_cmd_chan());
    test_device()->StartAclChannel(test_acl_chan());
  }

  void TearDown() override {
    discovery_manager_ = nullptr;
    test_device()->Stop();
    TestingBase::TearDown();
  }

 protected:
  LowEnergyDiscoveryManager* discovery_manager() const {
    return discovery_manager_.get();
  }

  // Returns the last reported scan state of the FakeController.
  bool scan_enabled() const { return scan_enabled_; }

  // The scan states that the FakeController has transitioned through.
  const std::vector<bool> scan_states() const { return scan_states_; }

  // Sets a callback that will run when the scan state transitions |count|
  // times.
  void set_scan_state_handler(size_t count, fit::closure callback) {
    scan_state_callbacks_[count] = std::move(callback);
  }

  // Called by FakeController when the scan state changes.
  void OnScanStateChanged(bool enabled) {
    FXL_VLOG(1) << "test: FakeController scan state: "
                << (enabled ? "enabled" : "disabled");
    scan_enabled_ = enabled;
    scan_states_.push_back(enabled);

    auto iter = scan_state_callbacks_.find(scan_states_.size());
    if (iter != scan_state_callbacks_.end()) {
      iter->second();
    }
  }

  // Registers the following fake devices with the FakeController:
  //
  // Device 0:
  //   - Connectable, not scannable;
  //   - General discoverable;
  //   - UUIDs: 0x180d, 0x180f;
  //   - has name: "Device 0"
  //
  // Device 1:
  //   - Connectable, not scannable;
  //   - Limited discoverable;
  //   - UUIDs: 0x180d;
  //   - has name: "Device 1"
  //
  // Device 2:
  //   - Not connectable, not scannable;
  //   - General discoverable;
  //   - UUIDs: none;
  //   - has name: "Device 2"
  //
  // Device 3:
  //   - Not discoverable;
  void AddFakeDevices() {
    // Device 0
    const auto kAdvData0 = common::CreateStaticByteBuffer(
        // Flags
        0x02, 0x01, 0x02,

        // Complete 16-bit service UUIDs
        0x05, 0x03, 0x0d, 0x18, 0x0f, 0x18,

        // Complete local name
        0x09, 0x09, 'D', 'e', 'v', 'i', 'c', 'e', ' ', '0');
    auto fake_device = std::make_unique<FakeDevice>(kAddress0, true, true);
    fake_device->SetAdvertisingData(kAdvData0);
    test_device()->AddDevice(std::move(fake_device));

    // Device 1
    const auto kAdvData1 = common::CreateStaticByteBuffer(
        // Flags
        0x02, 0x01, 0x01,

        // Complete 16-bit service UUIDs
        0x03, 0x03, 0x0d, 0x18);
    fake_device = std::make_unique<FakeDevice>(kAddress1, true, true);
    fake_device->SetAdvertisingData(kAdvData1);
    test_device()->AddDevice(std::move(fake_device));

    // Device 2
    const auto kAdvData2 = common::CreateStaticByteBuffer(
        // Flags
        0x02, 0x01, 0x02,

        // Complete local name
        0x09, 0x09, 'D', 'e', 'v', 'i', 'c', 'e', ' ', '2');
    fake_device = std::make_unique<FakeDevice>(kAddress2, false, false);
    fake_device->SetAdvertisingData(kAdvData2);
    test_device()->AddDevice(std::move(fake_device));

    // Device 3
    const auto kAdvData3 = common::CreateStaticByteBuffer(
        // Flags
        0x02, 0x01, 0x00,

        // Complete local name
        0x09, 0x09, 'D', 'e', 'v', 'i', 'c', 'e', ' ', '3');
    fake_device = std::make_unique<FakeDevice>(kAddress3, false, false);
    fake_device->SetAdvertisingData(kAdvData3);
    test_device()->AddDevice(std::move(fake_device));
  }

  // Creates and returns a discovery session.
  std::unique_ptr<LowEnergyDiscoverySession> StartDiscoverySession() {
    std::unique_ptr<LowEnergyDiscoverySession> session;
    discovery_manager()->StartDiscovery([&](auto cb_session) {
      FXL_DCHECK(cb_session);
      session = std::move(cb_session);
    });

    RunLoopUntilIdle();
    FXL_DCHECK(session);
    return session;
  }

 private:
  RemoteDeviceCache device_cache_;
  std::unique_ptr<LowEnergyDiscoveryManager> discovery_manager_;

  bool scan_enabled_;
  std::vector<bool> scan_states_;
  std::unordered_map<size_t, fit::closure> scan_state_callbacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyDiscoveryManagerTest);
};

using GAP_LowEnergyDiscoveryManagerTest = LowEnergyDiscoveryManagerTest;

TEST_F(GAP_LowEnergyDiscoveryManagerTest, StartDiscoveryAndStop) {
  std::unique_ptr<LowEnergyDiscoverySession> session;
  discovery_manager()->StartDiscovery([this, &session](auto cb_session) {
    session = std::move(cb_session);
  });

  RunLoopUntilIdle();

  // The test fixture will be notified of the change in scan state before we
  // receive the session.
  EXPECT_TRUE(scan_enabled());
  RunLoopUntilIdle();

  ASSERT_TRUE(session);
  EXPECT_TRUE(session->active());

  session->Stop();
  EXPECT_FALSE(session->active());

  RunLoopUntilIdle();
  EXPECT_FALSE(scan_enabled());
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, StartDiscoveryAndStopByDeleting) {
  // Start discovery but don't acquire ownership of the received session. This
  // should immediately terminate the session.
  std::unique_ptr<LowEnergyDiscoverySession> session;
  discovery_manager()->StartDiscovery([this, &session](auto cb_session) {
    session = std::move(cb_session);
  });

  RunLoopUntilIdle();

  // The test fixture will be notified of the change in scan state before we
  // receive the session.
  EXPECT_TRUE(scan_enabled());
  RunLoopUntilIdle();

  ASSERT_TRUE(session);
  EXPECT_TRUE(session->active());

  session = nullptr;

  RunLoopUntilIdle();
  EXPECT_FALSE(scan_enabled());
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, StartDiscoveryAndStopInCallback) {
  // Start discovery but don't acquire ownership of the received session. This
  // should terminate the session when |session| goes out of scope.
  discovery_manager()->StartDiscovery([](auto session) {});

  RunLoopUntilIdle();
  ASSERT_EQ(2u, scan_states().size());
  EXPECT_TRUE(scan_states()[0]);
  EXPECT_FALSE(scan_states()[1]);
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, StartDiscoveryFailure) {
  test_device()->SetDefaultResponseStatus(hci::kLESetScanEnable,
                                          hci::StatusCode::kCommandDisallowed);

  // |session| should contain nullptr.
  discovery_manager()->StartDiscovery(
      [](auto session) { EXPECT_FALSE(session); });

  RunLoopUntilIdle();
  EXPECT_FALSE(scan_enabled());
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, StartDiscoveryWhileScanning) {
  std::vector<std::unique_ptr<LowEnergyDiscoverySession>> sessions;

  constexpr size_t kExpectedSessionCount = 5;
  size_t cb_count = 0u;
  auto cb = [this, &cb_count, &sessions](auto session) {
    sessions.push_back(std::move(session));
    cb_count++;
  };

  discovery_manager()->StartDiscovery(cb);

  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());
  EXPECT_EQ(1u, sessions.size());

  // Add the rest of the sessions. These are expected to succeed immediately but
  // the callbacks should be called asynchronously.
  for (size_t i = 1u; i < kExpectedSessionCount; i++) {
    discovery_manager()->StartDiscovery(cb);
  }

  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());
  EXPECT_EQ(kExpectedSessionCount, sessions.size());

  // Remove one session from the list. Scan should continue.
  sessions.pop_back();
  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());

  // Remove all but one session from the list. Scan should continue.
  sessions.erase(sessions.begin() + 1, sessions.end());
  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());
  EXPECT_EQ(1u, sessions.size());

  // Remove the last session.
  sessions.clear();
  RunLoopUntilIdle();
  EXPECT_FALSE(scan_enabled());
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, StartDiscoveryWhilePendingStart) {
  std::vector<std::unique_ptr<LowEnergyDiscoverySession>> sessions;

  constexpr size_t kExpectedSessionCount = 5;
  size_t cb_count = 0u;
  auto cb = [this, &cb_count, &sessions](auto session) {
    sessions.push_back(std::move(session));
    cb_count++;
  };

  for (size_t i = 0u; i < kExpectedSessionCount; i++) {
    discovery_manager()->StartDiscovery(cb);
  }

  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());
  EXPECT_EQ(kExpectedSessionCount, sessions.size());

  // Remove all sessions. This should stop the scan.
  sessions.clear();
  RunLoopUntilIdle();
  EXPECT_FALSE(scan_enabled());
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest,
       StartDiscoveryWhilePendingStartAndStopInCallback) {
  constexpr size_t kExpectedSessionCount = 5;
  size_t cb_count = 0u;
  std::unique_ptr<LowEnergyDiscoverySession> session;
  auto cb = [this, &cb_count, &session](auto cb_session) {
    cb_count++;
    if (cb_count == kExpectedSessionCount) {
      // Hold on to only the last session object. The rest should get deleted
      // within the callback.
      session = std::move(cb_session);
    }
  };

  for (size_t i = 0u; i < kExpectedSessionCount; i++) {
    discovery_manager()->StartDiscovery(cb);
  }

  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());
  EXPECT_TRUE(session);

  RunLoopUntilIdle();
  EXPECT_EQ(kExpectedSessionCount, cb_count);
  EXPECT_TRUE(scan_enabled());

  // Deleting the only remaning session should stop the scan.
  session = nullptr;
  RunLoopUntilIdle();
  EXPECT_FALSE(scan_enabled());
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, StartDiscoveryWhilePendingStop) {
  std::unique_ptr<LowEnergyDiscoverySession> session;

  discovery_manager()->StartDiscovery([this, &session](auto cb_session) {
    session = std::move(cb_session);
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());
  EXPECT_TRUE(session);

  // Stop the session. This should issue a request to stop the ongoing scan but
  // the request will remain pending until we run the message loop.
  session = nullptr;

  // Request a new session. The discovery manager should restart the scan after
  // the ongoing one stops.
  discovery_manager()->StartDiscovery([this, &session](auto cb_session) {
    session = std::move(cb_session);
  });

  // Discovery should stop and start again.
  RunLoopUntilIdle();
  ASSERT_EQ(3u, scan_states().size());
  EXPECT_TRUE(scan_states()[0]);
  EXPECT_FALSE(scan_states()[1]);
  EXPECT_TRUE(scan_states()[2]);
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, StartDiscoveryFailureManyPending) {
  test_device()->SetDefaultResponseStatus(hci::kLESetScanEnable,
                                          hci::StatusCode::kCommandDisallowed);

  constexpr size_t kExpectedSessionCount = 5;
  size_t cb_count = 0u;
  auto cb = [this, &cb_count](auto session) {
    // |session| should contain nullptr as the request will fail.
    EXPECT_FALSE(session);
    cb_count++;
  };

  for (size_t i = 0u; i < kExpectedSessionCount; i++) {
    discovery_manager()->StartDiscovery(cb);
  }

  RunLoopUntilIdle();
  EXPECT_FALSE(scan_enabled());
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, ScanPeriodRestart) {
  constexpr size_t kNumScanStates = 3;

  discovery_manager()->set_scan_period(kTestScanPeriodMs);

  std::unique_ptr<LowEnergyDiscoverySession> session;
  discovery_manager()->StartDiscovery(
      [&session](auto cb_session) { session = std::move(cb_session); });

  // We should observe the scan state become enabled -> disabled -> enabled.
  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());

  // End the scan period.
  AdvanceTimeBy(zx::msec(kTestScanPeriodMs));
  RunLoopUntilIdle();
  ASSERT_EQ(kNumScanStates, scan_states().size());
  EXPECT_TRUE(scan_states()[0]);
  EXPECT_FALSE(scan_states()[1]);
  EXPECT_TRUE(scan_states()[2]);
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, ScanPeriodRestartFailure) {
  constexpr size_t kNumScanStates = 2;

  discovery_manager()->set_scan_period(kTestScanPeriodMs);

  std::unique_ptr<LowEnergyDiscoverySession> session;
  bool session_error = false;
  discovery_manager()->StartDiscovery([&](auto cb_session) {
    session = std::move(cb_session);
    session->set_error_callback(
        [&session_error, this] { session_error = true; });
  });

  // The controller will fail to restart scanning after scanning stops at the
  // end of the period. The scan state will transition twice (-> enabled ->
  // disabled).
  set_scan_state_handler(kNumScanStates, [this] {
    test_device()->SetDefaultResponseStatus(
        hci::kLESetScanEnable, hci::StatusCode::kCommandDisallowed);
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());

  // End the scan period. The scan should not restart.
  AdvanceTimeBy(zx::msec(kTestScanPeriodMs));
  RunLoopUntilIdle();

  ASSERT_EQ(kNumScanStates, scan_states().size());
  EXPECT_TRUE(scan_states()[0]);
  EXPECT_FALSE(scan_states()[1]);
  EXPECT_TRUE(session_error);
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, ScanPeriodRestartRemoveSession) {
  constexpr size_t kNumScanStates = 4;

  discovery_manager()->set_scan_period(kTestScanPeriodMs);

  std::unique_ptr<LowEnergyDiscoverySession> session;
  discovery_manager()->StartDiscovery(
      [&session](auto cb_session) { session = std::move(cb_session); });

  // We should observe 3 scan state transitions (-> enabled -> disabled ->
  // enabled).
  set_scan_state_handler(kNumScanStates - 1, [this, &session] {
    ASSERT_TRUE(session);
    EXPECT_TRUE(scan_enabled());

    // At this point the fake controller has updated its state but the discovery
    // manager has not processed the restarted scan. We should be able to remove
    // the current session and the state should ultimately become disabled.
    session->Stop();
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());

  // End the scan period.
  AdvanceTimeBy(zx::msec(kTestScanPeriodMs));
  RunLoopUntilIdle();

  ASSERT_EQ(kNumScanStates, scan_states().size());
  EXPECT_TRUE(scan_states()[0]);
  EXPECT_FALSE(scan_states()[1]);
  EXPECT_TRUE(scan_states()[2]);
  EXPECT_FALSE(scan_states()[3]);
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest,
       ScanPeriodRemoveSessionDuringRestart) {
  constexpr size_t kNumScanStates = 2;

  // Set a very short scan period for the sake of the test.
  discovery_manager()->set_scan_period(kTestScanPeriodMs);

  std::unique_ptr<LowEnergyDiscoverySession> session;
  discovery_manager()->StartDiscovery(
      [&session](auto cb_session) { session = std::move(cb_session); });

  // The controller will fail to restart scanning after scanning stops at the
  // end of the period. The scan state will transition twice (-> enabled ->
  // disabled).
  set_scan_state_handler(kNumScanStates, [this, &session] {
    ASSERT_TRUE(session);
    EXPECT_FALSE(scan_enabled());

    // Stop the session before the discovery manager processes the event. It
    // should detect this and discontinue the scan.
    session->Stop();
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());

  // End the scan period.
  AdvanceTimeBy(zx::msec(kTestScanPeriodMs));
  RunLoopUntilIdle();

  ASSERT_EQ(kNumScanStates, scan_states().size());
  EXPECT_TRUE(scan_states()[0]);
  EXPECT_FALSE(scan_states()[1]);
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest,
       ScanPeriodRestartRemoveAndAddSession) {
  constexpr size_t kNumScanPeriodRestartStates = 3;
  constexpr size_t kTotalNumStates = 5;

  // Set a very short scan period for the sake of the test.
  discovery_manager()->set_scan_period(kTestScanPeriodMs);

  std::unique_ptr<LowEnergyDiscoverySession> session;
  auto cb = [&session](auto cb_session) { session = std::move(cb_session); };
  discovery_manager()->StartDiscovery(cb);

  // We should observe 3 scan state transitions (-> enabled -> disabled ->
  // enabled).
  set_scan_state_handler(kNumScanPeriodRestartStates, [this, &session, cb] {
    ASSERT_TRUE(session);
    EXPECT_TRUE(scan_enabled());

    // At this point the fake controller has updated its state but the discovery
    // manager has not processed the restarted scan. We should be able to remove
    // the current session and create a new one and the state should update
    // accordingly.
    session->Stop();
    discovery_manager()->StartDiscovery(cb);
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(scan_enabled());

  // End the scan period.
  AdvanceTimeBy(zx::msec(kTestScanPeriodMs));
  RunLoopUntilIdle();

  // Scan should have been disabled and re-enabled.
  ASSERT_EQ(kTotalNumStates, scan_states().size());
  EXPECT_TRUE(scan_states()[0]);
  EXPECT_FALSE(scan_states()[1]);
  EXPECT_TRUE(scan_states()[2]);
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest, StartDiscoveryWithFilters) {
  AddFakeDevices();

  std::vector<std::unique_ptr<LowEnergyDiscoverySession>> sessions;

  // Set a short scan period so that we that we process events for multiple scan
  // periods during the test.
  discovery_manager()->set_scan_period(200);

  // Session 0 is interested in performing general discovery.
  std::unordered_set<common::DeviceAddress> devices_session0;
  LowEnergyDiscoverySession::DeviceFoundCallback result_cb =
      [&devices_session0](const auto& device) {
        devices_session0.insert(device.address());
      };
  sessions.push_back(StartDiscoverySession());
  sessions[0]->filter()->SetGeneralDiscoveryFlags();
  sessions[0]->SetResultCallback(std::move(result_cb));

  // Session 1 is interested in performing limited discovery.
  std::unordered_set<common::DeviceAddress> devices_session1;
  result_cb = [&devices_session1](const auto& device) {
    devices_session1.insert(device.address());
  };
  sessions.push_back(StartDiscoverySession());
  sessions[1]->filter()->set_flags(
      static_cast<uint8_t>(AdvFlag::kLELimitedDiscoverableMode));
  sessions[1]->SetResultCallback(std::move(result_cb));

  // Session 2 is interested in devices with UUID 0x180d.
  std::unordered_set<common::DeviceAddress> devices_session2;
  result_cb = [&devices_session2](const auto& device) {
    devices_session2.insert(device.address());
  };
  sessions.push_back(StartDiscoverySession());

  uint16_t uuid = 0x180d;
  sessions[2]->filter()->set_service_uuids({common::UUID(uuid)});
  sessions[2]->SetResultCallback(std::move(result_cb));

  // Session 3 is interested in devices whose names contain "Device".
  std::unordered_set<common::DeviceAddress> devices_session3;
  result_cb = [&devices_session3](const auto& device) {
    devices_session3.insert(device.address());
  };
  sessions.push_back(StartDiscoverySession());
  sessions[3]->filter()->set_name_substring("Device");
  sessions[3]->SetResultCallback(std::move(result_cb));

  // Session 4 is interested in non-connectable devices.
  std::unordered_set<common::DeviceAddress> devices_session4;
  result_cb = [&devices_session4](const auto& device) {
    devices_session4.insert(device.address());
  };
  sessions.push_back(StartDiscoverySession());
  sessions[4]->filter()->set_connectable(false);
  sessions[4]->SetResultCallback(std::move(result_cb));

  RunLoopUntilIdle();

  EXPECT_EQ(5u, sessions.size());

#define EXPECT_CONTAINS(addr, dev_list) \
  EXPECT_TRUE(dev_list.find(addr) != dev_list.end())
  // At this point all sessions should have processed all devices at least once.

  // Session 0: Should have seen all devices except for device 3, which is
  // non-discoverable.
  EXPECT_EQ(3u, devices_session0.size());
  EXPECT_CONTAINS(kAddress0, devices_session0);
  EXPECT_CONTAINS(kAddress1, devices_session0);
  EXPECT_CONTAINS(kAddress2, devices_session0);

  // Session 1: Should have only seen device 1.
  EXPECT_EQ(1u, devices_session1.size());
  EXPECT_CONTAINS(kAddress1, devices_session1);

  // Session 2: Should have only seen devices 0 and 1
  EXPECT_EQ(2u, devices_session2.size());
  EXPECT_CONTAINS(kAddress0, devices_session2);
  EXPECT_CONTAINS(kAddress1, devices_session2);

  // Session 3: Should have only seen devices 0, 2, and 3
  EXPECT_EQ(3u, devices_session3.size());
  EXPECT_CONTAINS(kAddress0, devices_session3);
  EXPECT_CONTAINS(kAddress2, devices_session3);
  EXPECT_CONTAINS(kAddress3, devices_session3);

  // Session 4: Should have seen devices 2
  EXPECT_EQ(2u, devices_session4.size());
  EXPECT_CONTAINS(kAddress2, devices_session4);
  EXPECT_CONTAINS(kAddress3, devices_session4);

#undef EXPECT_CONTAINS
}

TEST_F(GAP_LowEnergyDiscoveryManagerTest,
       StartDiscoveryWithFiltersCachedDeviceNotifications) {
  AddFakeDevices();

  std::vector<std::unique_ptr<LowEnergyDiscoverySession>> sessions;

  // Set a long scan period to make sure that the FakeController sends
  // advertising reports only once.
  discovery_manager()->set_scan_period(20000);

  // Session 0 is interested in performing general discovery.
  std::unordered_set<common::DeviceAddress> devices_session0;
  LowEnergyDiscoverySession::DeviceFoundCallback result_cb =
      [this, &devices_session0](const auto& device) {
        devices_session0.insert(device.address());
      };
  sessions.push_back(StartDiscoverySession());
  sessions[0]->filter()->SetGeneralDiscoveryFlags();
  sessions[0]->SetResultCallback(std::move(result_cb));

  RunLoopUntilIdle();
  ASSERT_EQ(3u, devices_session0.size());

  // Session 1 is interested in performing limited discovery.
  std::unordered_set<common::DeviceAddress> devices_session1;
  result_cb = [&devices_session1](const auto& device) {
    devices_session1.insert(device.address());
  };
  sessions.push_back(StartDiscoverySession());
  sessions[1]->filter()->set_flags(
      static_cast<uint8_t>(AdvFlag::kLELimitedDiscoverableMode));
  sessions[1]->SetResultCallback(std::move(result_cb));

  // Session 2 is interested in devices with UUID 0x180d.
  std::unordered_set<common::DeviceAddress> devices_session2;
  result_cb = [&devices_session2](const auto& device) {
    devices_session2.insert(device.address());
  };
  sessions.push_back(StartDiscoverySession());

  uint16_t uuid = 0x180d;
  sessions[2]->filter()->set_service_uuids({common::UUID(uuid)});
  sessions[2]->SetResultCallback(std::move(result_cb));

  // Session 3 is interested in devices whose names contain "Device".
  std::unordered_set<common::DeviceAddress> devices_session3;
  result_cb = [&devices_session3](const auto& device) {
    devices_session3.insert(device.address());
  };
  sessions.push_back(StartDiscoverySession());
  sessions[3]->filter()->set_name_substring("Device");
  sessions[3]->SetResultCallback(std::move(result_cb));

  // Session 4 is interested in non-connectable devices.
  std::unordered_set<common::DeviceAddress> devices_session4;
  result_cb = [&devices_session4](const auto& device) {
    devices_session4.insert(device.address());
  };
  sessions.push_back(StartDiscoverySession());
  sessions[4]->filter()->set_connectable(false);
  sessions[4]->SetResultCallback(std::move(result_cb));

  EXPECT_EQ(5u, sessions.size());

#define EXPECT_CONTAINS(addr, dev_list) \
  EXPECT_TRUE(dev_list.find(addr) != dev_list.end())
  // At this point all sessions should have processed all devices at least once
  // without running the message loop; results for Sessions 1, 2, 3, and 4 should
  // have come from the cache.

  // Session 0: Should have seen all devices except for device 3, which is
  // non-discoverable.
  EXPECT_EQ(3u, devices_session0.size());
  EXPECT_CONTAINS(kAddress0, devices_session0);
  EXPECT_CONTAINS(kAddress1, devices_session0);
  EXPECT_CONTAINS(kAddress2, devices_session0);

  // Session 1: Should have only seen device 1.
  EXPECT_EQ(1u, devices_session1.size());
  EXPECT_CONTAINS(kAddress1, devices_session1);

  // Session 2: Should have only seen devices 0 and 1
  EXPECT_EQ(2u, devices_session2.size());
  EXPECT_CONTAINS(kAddress0, devices_session2);
  EXPECT_CONTAINS(kAddress1, devices_session2);

  // Session 3: Should have only seen devices 0, 2, and 3
  EXPECT_EQ(3u, devices_session3.size());
  EXPECT_CONTAINS(kAddress0, devices_session3);
  EXPECT_CONTAINS(kAddress2, devices_session3);
  EXPECT_CONTAINS(kAddress3, devices_session3);

  // Session 4: Should have seen devices 2 and 3
  EXPECT_EQ(2u, devices_session4.size());
  EXPECT_CONTAINS(kAddress2, devices_session4);
  EXPECT_CONTAINS(kAddress3, devices_session4);

#undef EXPECT_CONTAINS
}

}  // namespace
}  // namespace gap
}  // namespace btlib
