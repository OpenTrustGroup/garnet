// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/cpp/auto_wait.h>
#include <zx/channel.h>

#include "garnet/drivers/bluetooth/lib/common/bt_snoop_logger.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"

namespace btsnoop {

class Sniffer final {
 public:
  Sniffer(const std::string& hci_dev_path, const std::string& log_file_path);
  ~Sniffer();

  // Starts the packet sniffing loop. Returns false if there is an error while
  // setting up the snoop file and device snoop channel.
  bool Start();

 private:
  // async::AutoWait handler
  async_wait_result_t OnHandleReady(async_t* async,
                                    zx_status_t status,
                                    const zx_packet_signal_t* signal);

  std::string hci_dev_path_;
  std::string log_file_path_;

  fxl::UniqueFD hci_dev_;
  zx::channel snoop_channel_;
  ::btlib::common::BTSnoopLogger logger_;

  std::unique_ptr<async::AutoWait> wait_;
  fsl::MessageLoop message_loop_;

  // For now we only sniff command and event packets so make the buffer large
  // enough to fit the largest command packet plus 1-byte for the snoop flags.
  uint8_t buffer_[sizeof(::btlib::hci::CommandHeader) +
                  ::btlib::hci::kMaxCommandPacketPayloadSize + 1];

  FXL_DISALLOW_COPY_AND_ASSIGN(Sniffer);
};

}  // namespace btsnoop
