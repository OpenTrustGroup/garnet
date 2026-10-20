// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <stdint.h>
#include <sys/types.h>
#include <memory>
#include <thread>

#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/ipc_channel.h"
#include "lib/fxl/logging.h"

namespace machina {

class Guest;
class IpcClient : public IpcRequestHandler, public IpcEnumerator {
 public:
  IpcClient(Guest* guest);
  void Init(uint16_t vmid);

  fidl::InterfaceRequest<IpcService> NewRequest() { return svc_.NewRequest(); }

 private:
  // |IpcEnumerator|
  virtual void OnNewIpcChannel(uint8_t chan_id,
                               zx::vmo tx_vmo,
                               zx::vmo rx_vmo) override;

  IpcServiceSyncPtr svc_;
  fidl::Binding<IpcEnumerator> enumerator_;
  async::Loop loop_;
};

}  // namespace machina