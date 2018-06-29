// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>
#include <lib/async-loop/cpp/loop.h>

#include <ree_agent/cpp/fidl.h>

#include "garnet/bin/gzos/ree_agent/ree_agent.h"
#include "garnet/bin/gzos/ree_agent/ta_service.h"
#include "garnet/bin/gzos/ree_agent/tipc_msg.h"

#include "lib/gzos/trusty_ipc/cpp/channel.h"
#include "lib/gzos/trusty_ipc/cpp/id_alloc.h"
#include "lib/gzos/trusty_ipc/cpp/object.h"

using namespace trusty_ipc;

namespace ree_agent {

struct TipcEndpoint {
  uint32_t src_addr;
  fbl::RefPtr<TipcChannelImpl> channel;
};

class TipcAgent : public ReeAgent {
 public:
  TipcAgent(uint32_t id, zx::channel ch, size_t max_msg_size,
            TaServices& service_provider);
  ~TipcAgent();

  zx_status_t Start() override;
  zx_status_t Stop() override;
  zx_status_t HandleMessage(void* buf, size_t size) override;

 private:
  zx_status_t AllocateEndpointSlot(uint32_t src_addr,
                                   fbl::RefPtr<TipcChannelImpl> channel,
                                   uint32_t* dst_addr);
  zx_status_t SendMessage(uint32_t local, uint32_t remote, void* data,
                          size_t data_len);

  zx_status_t HandleCtrlMessage(tipc_hdr* hdr);
  zx_status_t HandleConnectRequest(uint32_t src, void* req);
  zx_status_t HandleDisconnectRequest(uint32_t src, void* req);

  zx_status_t HandleTipcMessage(tipc_hdr* hdr);

  fbl::Mutex ep_table_lock_;
  TipcEndpoint ep_table_[kTipcAddrMaxNum] FXL_GUARDED_BY(ep_table_lock_);
  TaServices& ta_service_provider_;
};

}  // namespace ree_agent
