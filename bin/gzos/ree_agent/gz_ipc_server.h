// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/gz_ipc_agent.h"
#include "garnet/bin/gzos/ree_agent/ta_service.h"

namespace ree_agent {

class GzIpcServer : public GzIpcAgent {
 public:
  GzIpcServer(zx::unowned_resource shm_rsc, zx::channel message_channel,
              size_t max_message_size, TaServices& service_provider)
      : GzIpcAgent(std::move(shm_rsc), std::move(message_channel),
                   max_message_size),
        ta_service_provider_(service_provider) {}
  GzIpcServer() = delete;

 private:
  zx_status_t HandleCtrlMessage(gz_ipc_msg_hdr* msg_hdr);

  zx_status_t HandleConnectRequest(uint32_t remote_addr,
                                   gz_ipc_ctrl_msg_hdr* ctrl_hdr);

  TaServices& ta_service_provider_;
};

}  // namespace ree_agent
