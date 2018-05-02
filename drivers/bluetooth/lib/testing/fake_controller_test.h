// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <lib/async/cpp/task.h>

#include "garnet/drivers/bluetooth/lib/hci/acl_data_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/acl_data_packet.h"
#include "garnet/drivers/bluetooth/lib/hci/device_wrapper.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "garnet/drivers/bluetooth/lib/testing/test_base.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace testing {

class FakeControllerBase;

// FakeControllerTest is a test harness intended for tests that rely on HCI
// transactions. It is templated on FakeControllerType which must derive from
// FakeControllerBase and must be able to send and receive HCI packets over
// Zircon channels, acting as the controller endpoint of HCI.
//
// The testing library provides two such types:
//
//   - TestController (test_controller.h): Routes HCI packets directly to the
//     test harness. It allows tests to setup expectations based on the receipt
//     of HCI packets.
//
//   - FakeController (fake_controller.h): Emulates a Bluetooth controller. This
//     can respond to HCI commands the way a real controller would (albeit in a
//     contrived fashion), emulate discovery and connection events, etc.
template <class FakeControllerType>
class FakeControllerTest : public TestBase {
 public:
  FakeControllerTest() = default;
  virtual ~FakeControllerTest() = default;

 protected:
  // TestBase overrides:
  void SetUp() override {
    transport_ = hci::Transport::Create(
        FakeControllerTest<FakeControllerType>::SetUpTestDevice());
    transport_->Initialize(dispatcher());
  }

  void TearDown() override {
    if (!transport_)
      return;

    if (transport_->IsInitialized()) {
      transport_->ShutDown();
    }

    RunUntilIdle();

    transport_ = nullptr;
    test_device_ = nullptr;
 }

  // Directly initializes the ACL data channel and wires up its data rx
  // callback. It is OK to override the data rx callback after this is called.
  bool InitializeACLDataChannel(const hci::DataBufferInfo& bredr_buffer_info,
                                const hci::DataBufferInfo& le_buffer_info) {
    if (!transport_->InitializeACLDataChannel(bredr_buffer_info,
                                              le_buffer_info)) {
      return false;
    }

    transport_->acl_data_channel()->SetDataRxHandler(
        std::bind(&FakeControllerTest<FakeControllerType>::OnDataReceived, this,
                  std::placeholders::_1),
        TestBase::dispatcher());

    return true;
  }

  // Sets a callback which will be invoked when we receive packets from the test
  // controller. |callback| will be posted on the test loop, thus no locking is
  // necessary within the callback.
  //
  // InitializeACLDataChannel() must be called once and its data rx handler must
  // not be overridden by tests for |callback| to work.
  void set_data_received_callback(
      const hci::ACLDataChannel::DataReceivedCallback& callback) {
    data_received_callback_ = callback;
  }

  fxl::RefPtr<hci::Transport> transport() const { return transport_; }
  hci::CommandChannel* cmd_channel() const {
    return transport_->command_channel();
  }
  hci::ACLDataChannel* acl_data_channel() const {
    return transport_->acl_data_channel();
  }

  // Deletes |test_device_| and resets the pointer.
  void DeleteTestDevice() { test_device_ = nullptr; }

  // Getters for internal fields frequently used by tests.
  FakeControllerType* test_device() const { return test_device_.get(); }
  zx::channel test_cmd_chan() { return std::move(cmd1_); }
  zx::channel test_acl_chan() { return std::move(acl1_); }

 private:
  // Channels to be moved to the tests
  zx::channel cmd1_;
  zx::channel acl1_;

  // Initializes |test_device_| and returns the DeviceWrapper endpoint which can
  // be passed to classes that are under test.
  std::unique_ptr<hci::DeviceWrapper> SetUpTestDevice() {
    zx::channel cmd0;
    zx::channel acl0;

    zx_status_t status = zx::channel::create(0, &cmd0, &cmd1_);
    FXL_DCHECK(ZX_OK == status);

    status = zx::channel::create(0, &acl0, &acl1_);
    FXL_DCHECK(ZX_OK == status);

    auto hci_dev = std::make_unique<hci::DummyDeviceWrapper>(std::move(cmd0),
                                                             std::move(acl0));
    test_device_ = std::make_unique<FakeControllerType>();

    return hci_dev;
  }

  void OnDataReceived(hci::ACLDataPacketPtr data_packet) {
    // Accessing |data_received_callback_| is racy but unlikely to cause issues
    // in unit tests. NOTE(armansito): Famous last words?
    if (!data_received_callback_)
      return;

    async::PostTask(TestBase::dispatcher(),
                    [this, packet = std::move(data_packet)]() mutable {
                      data_received_callback_(std::move(packet));
                    });
  }

  std::unique_ptr<FakeControllerType> test_device_;
  fxl::RefPtr<hci::Transport> transport_;
  hci::ACLDataChannel::DataReceivedCallback data_received_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeControllerTest);
  static_assert(
      std::is_base_of<FakeControllerBase, FakeControllerType>::value,
      "TestBase must be used with a derivative of FakeControllerBase");
};

}  // namespace testing
}  // namespace btlib
