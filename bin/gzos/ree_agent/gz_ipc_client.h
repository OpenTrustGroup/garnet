// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/gz_ipc_agent.h"

namespace ree_agent {

class GzIpcClient : public GzIpcAgent {
 public:
  GzIpcClient(zx::channel message_channel, size_t max_message_size)
      : GzIpcAgent(std::move(message_channel), max_message_size) {}

  zx_status_t Connect(std::string service_name, zx::channel channel);

 private:
  zx_status_t HandleCtrlMessage(gz_ipc_msg_hdr* msg_hdr) override;
};

}  // namespace ree_agent
