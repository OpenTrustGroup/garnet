// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/low_energy_connection_manager.h"

#include <memory>
#include <vector>

#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"
#include "garnet/drivers/bluetooth/lib/gatt/fake_layer.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_connector.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_layer.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_device.h"

#include "gtest/gtest.h"

#include "lib/fxl/macros.h"

namespace btlib {
namespace gap {
namespace {

using ::btlib::testing::FakeController;
using ::btlib::testing::FakeDevice;

using TestingBase = ::btlib::testing::FakeControllerTest<FakeController>;

const common::DeviceAddress kAddress0(common::DeviceAddress::Type::kLEPublic,
                                      "00:00:00:00:00:01");
const common::DeviceAddress kAddress1(common::DeviceAddress::Type::kLEPublic,
                                      "00:00:00:00:00:02");
const common::DeviceAddress kAddress2(common::DeviceAddress::Type::kBREDR,
                                      "00:00:00:00:00:03");

class LowEnergyConnectionManagerTest : public TestingBase {
 public:
  LowEnergyConnectionManagerTest() = default;
  ~LowEnergyConnectionManagerTest() override = default;

 protected:
  void SetUp() override {
    TestingBase::SetUp();
    TestingBase::InitializeACLDataChannel(
        hci::DataBufferInfo(),
        hci::DataBufferInfo(hci::kMaxACLPayloadSize, 10));

    FakeController::Settings settings;
    settings.ApplyLegacyLEConfig();
    test_device()->set_settings(settings);

    dev_cache_ = std::make_unique<RemoteDeviceCache>();
    l2cap_ = l2cap::testing::FakeLayer::Create();
    l2cap_->Initialize();

    // TODO(armansito): Pass a fake connector here.
    connector_ = std::make_unique<hci::LowEnergyConnector>(
        transport(), dispatcher(),
        [this](auto link) { OnIncomingConnection(std::move(link)); });

    conn_mgr_ = std::make_unique<LowEnergyConnectionManager>(
        transport(), connector_.get(), dev_cache_.get(), l2cap_,
        gatt::testing::FakeLayer::Create());

    test_device()->SetConnectionStateCallback(
        fbl::BindMember(
            this, &LowEnergyConnectionManagerTest::OnConnectionStateChanged),
        dispatcher());

    test_device()->StartCmdChannel(test_cmd_chan());
    test_device()->StartAclChannel(test_acl_chan());
  }

  void TearDown() override {
    if (conn_mgr_)
      conn_mgr_ = nullptr;
    dev_cache_ = nullptr;

    l2cap_->ShutDown();
    l2cap_ = nullptr;

    TestingBase::TearDown();
  }

  // Deletes |conn_mgr_|.
  void DeleteConnMgr() { conn_mgr_ = nullptr; }

  RemoteDeviceCache* dev_cache() const { return dev_cache_.get(); }
  LowEnergyConnectionManager* conn_mgr() const { return conn_mgr_.get(); }
  l2cap::testing::FakeLayer* fake_l2cap() const { return l2cap_.get(); }

  // Addresses of currently connected fake devices.
  using DeviceList = std::unordered_set<common::DeviceAddress>;
  const DeviceList& connected_devices() const { return connected_devices_; }

  // Addresses of devices with a canceled connection attempt.
  const DeviceList& canceled_devices() const { return canceled_devices_; }

  hci::ConnectionPtr MoveLastRemoteInitiated() {
    return std::move(last_remote_initiated_);
  }

 private:
  // Called by |connector_| when a new remote initiated connection is received.
  void OnIncomingConnection(hci::ConnectionPtr link) {
    last_remote_initiated_ = std::move(link);
  }

  // Called by FakeController on connection events.
  void OnConnectionStateChanged(const common::DeviceAddress& address,
                                bool connected,
                                bool canceled) {
    FXL_VLOG(3) << "OnConnectionStateChanged: " << address << " is "
                << (connected ? "connected " : "")
                << (canceled ? "canceled " : "");
    if (canceled) {
      canceled_devices_.insert(address);
    } else if (connected) {
      FXL_DCHECK(connected_devices_.find(address) == connected_devices_.end());
      connected_devices_.insert(address);
    } else {
      FXL_DCHECK(connected_devices_.find(address) != connected_devices_.end());
      connected_devices_.erase(address);
    }
  }

  fbl::RefPtr<l2cap::testing::FakeLayer> l2cap_;

  std::unique_ptr<RemoteDeviceCache> dev_cache_;
  std::unique_ptr<hci::LowEnergyConnector> connector_;
  std::unique_ptr<LowEnergyConnectionManager> conn_mgr_;

  // The most recent remote-initiated connection reported by |connector_|.
  hci::ConnectionPtr last_remote_initiated_;

  DeviceList connected_devices_;
  DeviceList canceled_devices_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyConnectionManagerTest);
};

using GAP_LowEnergyConnectionManagerTest = LowEnergyConnectionManagerTest;

TEST_F(GAP_LowEnergyConnectionManagerTest, ConnectUnknownDevice) {
  EXPECT_FALSE(conn_mgr()->Connect("nope", {}));
}

TEST_F(GAP_LowEnergyConnectionManagerTest, ConnectClassicDevice) {
  auto* dev = dev_cache()->NewDevice(kAddress2, true);
  EXPECT_FALSE(conn_mgr()->Connect(dev->identifier(), {}));
}

TEST_F(GAP_LowEnergyConnectionManagerTest, ConnectNonConnectableDevice) {
  auto* dev = dev_cache()->NewDevice(kAddress0, false);
  EXPECT_FALSE(conn_mgr()->Connect(dev->identifier(), {}));
}

// An error is received via the HCI Command cb_status event
TEST_F(GAP_LowEnergyConnectionManagerTest, ConnectSingleDeviceErrorStatus) {
  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  fake_dev->set_connect_status(
      hci::StatusCode::kConnectionFailedToBeEstablished);
  test_device()->AddLEDevice(std::move(fake_dev));

  EXPECT_EQ(RemoteDevice::ConnectionState::kNotConnected,
            dev->connection_state());

  hci::Status status;
  auto callback = [&status](auto cb_status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    status = cb_status;
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));
  EXPECT_EQ(RemoteDevice::ConnectionState::kInitializing,
            dev->connection_state());

  RunUntilIdle();

  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(hci::StatusCode::kConnectionFailedToBeEstablished,
            status.protocol_error());
  EXPECT_EQ(RemoteDevice::ConnectionState::kNotConnected,
            dev->connection_state());
}

// LE Connection Complete event reports error
TEST_F(GAP_LowEnergyConnectionManagerTest, ConnectSingleDeviceFailure) {
  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  fake_dev->set_connect_response(
      hci::StatusCode::kConnectionFailedToBeEstablished);
  test_device()->AddLEDevice(std::move(fake_dev));

  hci::Status status;
  auto callback = [&status](auto cb_status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    status = cb_status;
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));
  EXPECT_EQ(RemoteDevice::ConnectionState::kInitializing,
            dev->connection_state());

  RunUntilIdle();

  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(hci::StatusCode::kConnectionFailedToBeEstablished,
            status.protocol_error());
  EXPECT_EQ(RemoteDevice::ConnectionState::kNotConnected,
            dev->connection_state());
}

TEST_F(GAP_LowEnergyConnectionManagerTest, ConnectSingleDeviceTimeout) {
  constexpr int64_t kTestRequestTimeoutMs = 20000;

  auto* dev = dev_cache()->NewDevice(kAddress0, true);

  // We add no fake devices to cause the request to time out.

  hci::Status status;
  auto callback = [&status](auto cb_status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    status = cb_status;
  };

  conn_mgr()->set_request_timeout_for_testing(kTestRequestTimeoutMs);
  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));
  EXPECT_EQ(RemoteDevice::ConnectionState::kInitializing,
            dev->connection_state());

  // Make sure the first HCI transaction completes before advancing the fake
  // clock.
  RunUntilIdle();

  AdvanceTimeBy(zx::msec(kTestRequestTimeoutMs));
  RunUntilIdle();

  EXPECT_FALSE(status);
  EXPECT_EQ(common::HostError::kTimedOut, status.error()) << status.ToString();
  EXPECT_EQ(RemoteDevice::ConnectionState::kNotConnected,
            dev->connection_state());
}

// Successful connection to single device
TEST_F(GAP_LowEnergyConnectionManagerTest, ConnectSingleDevice) {
  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  EXPECT_TRUE(dev->temporary());

  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  test_device()->AddLEDevice(std::move(fake_dev));

  // Initialize as error to verify that |callback| assigns success.
  hci::Status status(common::HostError::kFailed);
  LowEnergyConnectionRefPtr conn_ref;
  auto callback = [&status, &conn_ref](auto cb_status, auto cb_conn_ref) {
    EXPECT_TRUE(cb_conn_ref);
    status = cb_status;
    conn_ref = std::move(cb_conn_ref);
    EXPECT_TRUE(conn_ref->active());
  };

  EXPECT_TRUE(connected_devices().empty());
  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));
  EXPECT_EQ(RemoteDevice::ConnectionState::kInitializing,
            dev->connection_state());

  RunUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));

  ASSERT_TRUE(conn_ref);
  EXPECT_TRUE(conn_ref->active());
  EXPECT_EQ(dev->identifier(), conn_ref->device_identifier());
  EXPECT_FALSE(dev->temporary());
  EXPECT_EQ(RemoteDevice::ConnectionState::kConnected, dev->connection_state());
}

TEST_F(GAP_LowEnergyConnectionManagerTest, ReleaseRef) {
  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  test_device()->AddLEDevice(std::move(fake_dev));

  // Initialize as error to verify that |callback| assigns success.
  hci::Status status(common::HostError::kFailed);
  LowEnergyConnectionRefPtr conn_ref;
  auto callback = [&status, &conn_ref](auto cb_status, auto cb_conn_ref) {
    EXPECT_TRUE(cb_conn_ref);
    status = cb_status;
    conn_ref = std::move(cb_conn_ref);
    EXPECT_TRUE(conn_ref->active());
  };

  EXPECT_TRUE(connected_devices().empty());
  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));

  RunUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(RemoteDevice::ConnectionState::kConnected, dev->connection_state());

  ASSERT_TRUE(conn_ref);
  conn_ref = nullptr;

  RunUntilIdle();

  EXPECT_TRUE(connected_devices().empty());
  EXPECT_EQ(RemoteDevice::ConnectionState::kNotConnected,
            dev->connection_state());
}

TEST_F(GAP_LowEnergyConnectionManagerTest,
       OneDeviceTwoPendingRequestsBothFail) {
  constexpr int kRequestCount = 2;

  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  fake_dev->set_connect_response(
      hci::StatusCode::kConnectionFailedToBeEstablished);
  test_device()->AddLEDevice(std::move(fake_dev));

  hci::Status statuses[kRequestCount];

  int cb_count = 0;
  auto callback = [&statuses, &cb_count](auto cb_status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    statuses[cb_count++] = cb_status;
  };

  for (int i = 0; i < kRequestCount; ++i) {
    EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback))
        << "request count: " << i + 1;
  }

  RunUntilIdle();

  ASSERT_EQ(kRequestCount, cb_count);
  for (int i = 0; i < kRequestCount; ++i) {
    EXPECT_TRUE(statuses[i].is_protocol_error());
    EXPECT_EQ(hci::StatusCode::kConnectionFailedToBeEstablished,
              statuses[i].protocol_error())
        << "request count: " << i + 1;
  }
}

TEST_F(GAP_LowEnergyConnectionManagerTest, OneDeviceManyPendingRequests) {
  constexpr size_t kRequestCount = 50;

  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  test_device()->AddLEDevice(std::move(fake_dev));

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto callback = [&conn_refs](auto cb_status, auto conn_ref) {
    EXPECT_TRUE(conn_ref);
    EXPECT_TRUE(cb_status);
    conn_refs.emplace_back(std::move(conn_ref));
  };

  for (size_t i = 0; i < kRequestCount; ++i) {
    EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback))
        << "request count: " << i + 1;
  }

  RunUntilIdle();

  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));

  EXPECT_EQ(kRequestCount, conn_refs.size());
  for (size_t i = 0; i < kRequestCount; ++i) {
    ASSERT_TRUE(conn_refs[i]);
    EXPECT_TRUE(conn_refs[i]->active());
    EXPECT_EQ(dev->identifier(), conn_refs[i]->device_identifier());
  }

  // Release one reference. The rest should be active.
  conn_refs[0] = nullptr;
  for (size_t i = 1; i < kRequestCount; ++i)
    EXPECT_TRUE(conn_refs[i]->active());

  // Release all but one reference.
  for (size_t i = 1; i < kRequestCount - 1; ++i)
    conn_refs[i] = nullptr;
  EXPECT_TRUE(conn_refs[kRequestCount - 1]->active());

  // Drop the last reference.
  conn_refs[kRequestCount - 1] = nullptr;

  RunUntilIdle();

  EXPECT_TRUE(connected_devices().empty());
}

TEST_F(GAP_LowEnergyConnectionManagerTest, AddRefAfterConnection) {
  constexpr size_t kRefCount = 50;

  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  test_device()->AddLEDevice(std::move(fake_dev));

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto callback = [&conn_refs](auto cb_status, auto conn_ref) {
    EXPECT_TRUE(conn_ref);
    EXPECT_TRUE(cb_status);
    conn_refs.emplace_back(std::move(conn_ref));
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));

  RunUntilIdle();

  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));
  EXPECT_EQ(1u, conn_refs.size());

  // Add new references.
  for (size_t i = 1; i < kRefCount; ++i) {
    EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback))
        << "request count: " << i + 1;
    RunUntilIdle();
  }

  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));
  EXPECT_EQ(kRefCount, conn_refs.size());

  // Disconnect.
  conn_refs.clear();

  RunUntilIdle();

  EXPECT_TRUE(connected_devices().empty());
}

TEST_F(GAP_LowEnergyConnectionManagerTest, PendingRequestsOnTwoDevices) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, true);
  auto* dev1 = dev_cache()->NewDevice(kAddress1, true);

  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress1));

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto callback = [&conn_refs](auto cb_status, auto conn_ref) {
    EXPECT_TRUE(conn_ref);
    EXPECT_TRUE(cb_status);
    conn_refs.emplace_back(std::move(conn_ref));
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), callback));
  EXPECT_TRUE(conn_mgr()->Connect(dev1->identifier(), callback));

  RunUntilIdle();

  EXPECT_EQ(2u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));
  EXPECT_EQ(1u, connected_devices().count(kAddress1));

  ASSERT_EQ(2u, conn_refs.size());
  ASSERT_TRUE(conn_refs[0]);
  ASSERT_TRUE(conn_refs[1]);
  EXPECT_EQ(dev0->identifier(), conn_refs[0]->device_identifier());
  EXPECT_EQ(dev1->identifier(), conn_refs[1]->device_identifier());

  // |dev1| should disconnect first.
  conn_refs[1] = nullptr;

  RunUntilIdle();

  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));

  conn_refs.clear();

  RunUntilIdle();
  EXPECT_TRUE(connected_devices().empty());
}

TEST_F(GAP_LowEnergyConnectionManagerTest,
       PendingRequestsOnTwoDevicesOneFails) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, true);
  auto* dev1 = dev_cache()->NewDevice(kAddress1, true);

  auto fake_dev0 = std::make_unique<FakeDevice>(kAddress0);
  fake_dev0->set_connect_response(
      hci::StatusCode::kConnectionFailedToBeEstablished);
  test_device()->AddLEDevice(std::move(fake_dev0));
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress1));

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto callback = [&conn_refs](auto, auto conn_ref) {
    conn_refs.emplace_back(std::move(conn_ref));
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), callback));
  EXPECT_TRUE(conn_mgr()->Connect(dev1->identifier(), callback));

  RunUntilIdle();

  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress1));

  ASSERT_EQ(2u, conn_refs.size());
  EXPECT_FALSE(conn_refs[0]);
  ASSERT_TRUE(conn_refs[1]);
  EXPECT_EQ(dev1->identifier(), conn_refs[1]->device_identifier());

  // Both connections should disconnect.
  conn_refs.clear();

  RunUntilIdle();
  EXPECT_TRUE(connected_devices().empty());
}

TEST_F(GAP_LowEnergyConnectionManagerTest, Destructor) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, true);
  auto* dev1 = dev_cache()->NewDevice(kAddress1, true);

  // Connecting to this device will succeed.
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  // Connecting to this device will remain pending.
  auto pending_dev = std::make_unique<FakeDevice>(kAddress1);
  pending_dev->set_force_pending_connect(true);
  test_device()->AddLEDevice(std::move(pending_dev));

  // Below we create one connection and one pending request to have at the time
  // of destruction.

  LowEnergyConnectionRefPtr conn_ref;
  auto success_cb = [&conn_ref, this](auto status, auto cb_conn_ref) {
    EXPECT_TRUE(cb_conn_ref);
    EXPECT_TRUE(status);

    conn_ref = std::move(cb_conn_ref);
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), success_cb));
  RunUntilIdle();

  ASSERT_TRUE(conn_ref);
  bool conn_closed = false;
  conn_ref->set_closed_callback([&conn_closed] { conn_closed = true; });

  bool error_cb_called = false;
  auto error_cb = [&error_cb_called](auto status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    EXPECT_EQ(common::HostError::kFailed, status.error());
    error_cb_called = true;
  };

  // This will send an HCI command to the fake controller. We delete the
  // connection manager before a connection event gets received which should
  // cancel the connection.
  EXPECT_TRUE(conn_mgr()->Connect(dev1->identifier(), error_cb));
  DeleteConnMgr();

  RunUntilIdle();

  EXPECT_TRUE(error_cb_called);
  EXPECT_TRUE(conn_closed);
  EXPECT_EQ(1u, canceled_devices().size());
  EXPECT_EQ(1u, canceled_devices().count(kAddress1));
}

TEST_F(GAP_LowEnergyConnectionManagerTest, DisconnectError) {
  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  // This should fail as |dev0| is not connected.
  EXPECT_FALSE(conn_mgr()->Disconnect(dev->identifier()));
}

TEST_F(GAP_LowEnergyConnectionManagerTest, Disconnect) {
  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  int closed_count = 0;
  auto closed_cb = [&closed_count] { closed_count++; };

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto success_cb = [&conn_refs, &closed_cb, this](auto status, auto conn_ref) {
    EXPECT_TRUE(status);
    ASSERT_TRUE(conn_ref);
    conn_ref->set_closed_callback(closed_cb);
    conn_refs.push_back(std::move(conn_ref));
  };

  // Issue two connection refs.
  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), success_cb));
  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), success_cb));

  RunUntilIdle();

  ASSERT_EQ(2u, conn_refs.size());

  EXPECT_TRUE(conn_mgr()->Disconnect(dev->identifier()));

  RunUntilIdle();

  EXPECT_EQ(2, closed_count);
  EXPECT_TRUE(connected_devices().empty());
  EXPECT_TRUE(canceled_devices().empty());
}

// Tests when a link is lost without explicitly disconnecting
TEST_F(GAP_LowEnergyConnectionManagerTest, DisconnectEvent) {
  auto* dev = dev_cache()->NewDevice(kAddress0, true);

  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  int closed_count = 0;
  auto closed_cb = [&closed_count, this] {
    closed_count++;
  };

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto success_cb = [&conn_refs, &closed_cb, this](auto status, auto conn_ref) {
    EXPECT_TRUE(status);
    ASSERT_TRUE(conn_ref);
    conn_ref->set_closed_callback(closed_cb);
    conn_refs.push_back(std::move(conn_ref));
  };

  // Issue two connection refs.
  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), success_cb));
  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), success_cb));

  RunUntilIdle();

  ASSERT_EQ(2u, conn_refs.size());

  // This makes FakeController send us HCI Disconnection Complete events.
  test_device()->Disconnect(kAddress0);

  RunUntilIdle();

  EXPECT_EQ(2, closed_count);
}

TEST_F(GAP_LowEnergyConnectionManagerTest, DisconnectWhileRefPending) {
  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  LowEnergyConnectionRefPtr conn_ref;
  auto success_cb = [&conn_ref, this](auto status, auto cb_conn_ref) {
    EXPECT_TRUE(status);
    ASSERT_TRUE(cb_conn_ref);
    EXPECT_TRUE(cb_conn_ref->active());

    conn_ref = std::move(cb_conn_ref);
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), success_cb));
  RunUntilIdle();
  ASSERT_TRUE(conn_ref);

  auto ref_cb = [](auto status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    EXPECT_FALSE(status);
    EXPECT_EQ(common::HostError::kFailed, status.error());
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), ref_cb));

  // This should invalidate the ref that was bound to |ref_cb|.
  EXPECT_TRUE(conn_mgr()->Disconnect(dev->identifier()));

  RunUntilIdle();
}

// This tests that a connection reference callback returns nullptr if a HCI
// Disconnection Complete event is received for the corresponding ACL link
// BEFORE the callback gets run.
TEST_F(GAP_LowEnergyConnectionManagerTest, DisconnectEventWhileRefPending) {
  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  LowEnergyConnectionRefPtr conn_ref;
  auto success_cb = [&conn_ref, this](auto status, auto cb_conn_ref) {
    ASSERT_TRUE(cb_conn_ref);
    ASSERT_TRUE(status);
    EXPECT_TRUE(cb_conn_ref->active());

    conn_ref = std::move(cb_conn_ref);
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), success_cb));
  RunUntilIdle();
  ASSERT_TRUE(conn_ref);

  // Request a new reference. Disconnect the link before the reference is
  // received.
  auto ref_cb = [](auto status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    EXPECT_FALSE(status);
    EXPECT_EQ(common::HostError::kFailed, status.error());
  };

  auto disconn_cb = [this, ref_cb, dev](auto) {
    // The link is gone but conn_mgr() hasn't updated the connection state yet.
    // The request to connect will attempt to add a new reference which will be
    // invalidated before |ref_cb| gets called.
    EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), ref_cb));
  };
  conn_mgr()->SetDisconnectCallbackForTesting(disconn_cb);

  test_device()->Disconnect(kAddress0);
  RunUntilIdle();
}

// Listener receives remote initiated connection ref.
TEST_F(GAP_LowEnergyConnectionManagerTest, RegisterRemoteInitiatedLink) {
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  // First create a fake incoming connection.
  test_device()->ConnectLowEnergy(kAddress0);

  RunUntilIdle();

  auto link = MoveLastRemoteInitiated();
  ASSERT_TRUE(link);

  LowEnergyConnectionRefPtr conn_ref =
      conn_mgr()->RegisterRemoteInitiatedLink(std::move(link));
  ASSERT_TRUE(conn_ref);
  EXPECT_TRUE(conn_ref->active());

  // A RemoteDevice should now exist in the cache.
  auto* dev = dev_cache()->FindDeviceByAddress(kAddress0);
  ASSERT_TRUE(dev);
  EXPECT_EQ(dev->identifier(), conn_ref->device_identifier());

  conn_ref = nullptr;

  RunUntilIdle();
  EXPECT_TRUE(connected_devices().empty());
}

// Tests that the master accepts the connection parameters that are sent from
// a fake slave and eventually applies them to the link.
TEST_F(GAP_LowEnergyConnectionManagerTest, L2CAPLEConnectionParameterUpdate) {
  // Set up a fake device and a connection over which to process the L2CAP
  // request.
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));
  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  ASSERT_TRUE(dev);

  LowEnergyConnectionRefPtr conn_ref;
  auto conn_cb = [&conn_ref](const auto& dev_id, auto cr) {
    conn_ref = std::move(cr);
  };
  ASSERT_TRUE(conn_mgr()->Connect(dev->identifier(), conn_cb));

  RunUntilIdle();
  ASSERT_TRUE(conn_ref);

  hci::LEPreferredConnectionParameters preferred(
      hci::kLEConnectionIntervalMin, hci::kLEConnectionIntervalMax,
      hci::kLEConnectionLatencyMax, hci::kLEConnectionSupervisionTimeoutMax);

  hci::LEConnectionParameters actual;
  bool fake_dev_cb_called = false;
  bool conn_params_cb_called = false;

  auto fake_dev_cb = [&actual, &fake_dev_cb_called](const auto& addr,
                                                           const auto& params) {
    fake_dev_cb_called = true;
    actual = params;
  };
  test_device()->SetLEConnectionParametersCallback(fake_dev_cb, dispatcher());

  auto conn_params_cb = [&conn_params_cb_called, &conn_ref](const auto& dev) {
    EXPECT_EQ(conn_ref->device_identifier(), dev.identifier());
    conn_params_cb_called = true;
  };
  conn_mgr()->SetConnectionParametersCallbackForTesting(conn_params_cb);

  fake_l2cap()->TriggerLEConnectionParameterUpdate(conn_ref->handle(),
                                                   preferred);

  RunUntilIdle();

  EXPECT_TRUE(fake_dev_cb_called);
  ASSERT_TRUE(conn_params_cb_called);

  EXPECT_EQ(preferred, *dev->le_preferred_connection_params());
  EXPECT_EQ(actual, *dev->le_connection_params());
}

TEST_F(GAP_LowEnergyConnectionManagerTest, L2CAPSignalLinkError) {
  // Set up a fake device and a connection over which to process the L2CAP
  // request.
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));
  auto* dev = dev_cache()->NewDevice(kAddress0, true);
  ASSERT_TRUE(dev);

  fbl::RefPtr<l2cap::Channel> att_chan;
  auto l2cap_chan_cb = [&att_chan](auto chan) { att_chan = chan; };
  fake_l2cap()->set_channel_callback(l2cap_chan_cb);

  LowEnergyConnectionRefPtr conn_ref;
  auto conn_cb = [&conn_ref](const auto& dev_id, auto cr) {
    conn_ref = std::move(cr);
  };
  ASSERT_TRUE(conn_mgr()->Connect(dev->identifier(), conn_cb));

  RunUntilIdle();
  ASSERT_TRUE(conn_ref);
  ASSERT_TRUE(att_chan);
  ASSERT_EQ(1u, connected_devices().size());

  // Signaling a link error through the channel should disconnect the link.
  att_chan->SignalLinkError();

  RunUntilIdle();
  EXPECT_TRUE(connected_devices().empty());
}

}  // namespace
}  // namespace gap
}  // namespace btlib
