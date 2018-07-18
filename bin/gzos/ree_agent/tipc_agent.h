// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
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

// TipcEndpointTable is not thread-safe. User should have a lock
// to guarantee the atomicity of the table operations.
class TipcEndpointTable {
 public:
  TipcEndpointTable() = default;

  zx_status_t AllocateSlot(uint32_t src_addr,
                           fbl::RefPtr<TipcChannelImpl> channel,
                           uint32_t* dst_addr);
  TipcEndpoint* LookupByAddr(uint32_t dst_addr);
  TipcEndpoint* FindInUseSlot(uint32_t& start_slot);
  void FreeSlotByAddr(uint32_t dst_addr);

  uint32_t to_addr(uint32_t slot_id) { return kTipcAddrBase + slot_id; }
  uint32_t to_slot_id(uint32_t addr) { return addr - kTipcAddrBase; }

 private:
  void FreeSlotInternal(uint32_t slot_id);

  IdAllocator<kTipcAddrMaxNum> id_allocator_;
  TipcEndpoint table_[kTipcAddrMaxNum];

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcEndpointTable);
};

class TipcAgent : public ReeAgent {
 public:
  TipcAgent(uint32_t id, zx::channel ch, size_t max_msg_size,
            TaServices& service_provider, TipcEndpointTable* ep_table);
  ~TipcAgent();

  zx_status_t Start() override;
  zx_status_t Stop() override;
  zx_status_t HandleMessage(void* buf, size_t size) override;

 private:
  zx_status_t SendMessageToRee(uint32_t local, uint32_t remote, void* data,
                               size_t data_len);
  void CloseTipcChannelLocked(TipcEndpoint* ep, uint32_t dst_addr)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  zx_status_t HandleCtrlMessage(tipc_hdr* hdr);
  zx_status_t HandleConnectRequest(uint32_t src, void* req);
  zx_status_t HandleDisconnectRequest(uint32_t src, void* req);

  zx_status_t HandleTipcMessage(tipc_hdr* hdr);

  TaServices& ta_service_provider_;

  fbl::Mutex lock_;
  fbl::unique_ptr<TipcEndpointTable> ep_table_ FXL_GUARDED_BY(lock_);
  fbl::unique_ptr<char> write_buffer_ FXL_GUARDED_BY(lock_);
};

}  // namespace ree_agent
