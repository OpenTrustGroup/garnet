// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_scanner.h"

#include <zircon/assert.h>

#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"

namespace btlib {
namespace hci {

// Default implementations do nothing.

void LowEnergyScanner::Delegate::OnDeviceFound(
    const LowEnergyScanResult& result,
    const common::ByteBuffer& data) {}

LowEnergyScanResult::LowEnergyScanResult()
    : connectable(false), rssi(hci::kRSSIInvalid) {}

LowEnergyScanResult::LowEnergyScanResult(const common::DeviceAddress& address,
                                         bool connectable,
                                         int8_t rssi)
    : address(address), connectable(connectable), rssi(rssi) {}

LowEnergyScanner::LowEnergyScanner(Delegate* delegate,
                                   fxl::RefPtr<Transport> hci,
                                   async_dispatcher_t* dispatcher)
    : state_(State::kIdle),
      delegate_(delegate),
      dispatcher_(dispatcher),
      transport_(hci) {
  ZX_DEBUG_ASSERT(delegate_);
  ZX_DEBUG_ASSERT(transport_);
  ZX_DEBUG_ASSERT(dispatcher_);

  hci_cmd_runner_ =
      std::make_unique<SequentialCommandRunner>(dispatcher_, transport_);
}

}  // namespace hci
}  // namespace btlib
