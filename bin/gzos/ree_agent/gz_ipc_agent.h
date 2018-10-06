// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc_service.h>
#include <zx/eventpair.h>
#include <zx/resource.h>
#include <zx/vmo.h>
#include <map>

#include "garnet/bin/gzos/ree_agent/gz_ipc_endpoint.h"
#include "garnet/bin/gzos/ree_agent/gz_ipc_msg.h"
#include "garnet/bin/gzos/ree_agent/ree_agent.h"

#include "lib/gzos/utils/cpp/id_alloc.h"

namespace ree_agent {

static constexpr uint64_t INVALID_SHM_ID = UINT64_MAX;

class SharedMemoryRecord {
 public:
  static uint64_t GetShmId(zx_handle_t handle) {
    zx_info_vmo_t vmo_info;
    FXL_CHECK(zx_object_get_info(handle, ZX_INFO_VMO, &vmo_info,
                                 sizeof(vmo_info), NULL, NULL) == ZX_OK);

    uint64_t id;
    if (sscanf(vmo_info.name, "ns_shm:%lx", &id) == 1) {
      return id;
    } else {
      return INVALID_SHM_ID;
    }
  };

  SharedMemoryRecord() = delete;

  SharedMemoryRecord(uintptr_t base_phys, size_t size, zx::eventpair event,
                     zx::vmo vmo = zx::vmo());
  ~SharedMemoryRecord();

  uintptr_t base_phys() { return base_phys_; }
  uintptr_t size() { return size_; }

  zx::vmo& vmo() { return vmo_; }
  void set_vmo(zx::vmo vmo) { vmo_ = std::move(vmo); }
  void reset_vmo() { vmo_.reset(); }

  void set_release_handler(fit::closure handler) {
    handler_ = std::move(handler);
  }

 private:
  void OnVmoDestroyed(async_dispatcher_t* async, async::WaitBase* wait,
                      zx_status_t status, const zx_packet_signal_t* signal) {
    if (handler_) {
      handler_();
    }
  }

  uintptr_t base_phys_;
  size_t size_;
  zx::vmo vmo_;
  zx::eventpair event_;

  fit::closure handler_;

  async::WaitMethod<SharedMemoryRecord, &SharedMemoryRecord::OnVmoDestroyed>
      wait_{this};
};

class GzIpcAgent : public Agent, public MessageHandler {
 public:
  friend class GzIpcEndpoint;
  GzIpcAgent() = delete;
  GzIpcAgent(zx::unowned_resource shm_rsc, zx::channel message_channel,
             size_t max_message_size);

  // Overriden from |Agent|
  zx_status_t Start() override;
  zx_status_t Stop() override;

  SharedMemoryRecord* LookupSharedMemoryRecord(uint64_t id);

 protected:
  zx_status_t HandleDisconnectRequest(gz_ipc_ctrl_msg_hdr* ctrl_hdr);

  zx_status_t CreateEndpointAndSendReply(zx::channel& ch, uint32_t remote_addr);

  zx_status_t AllocEndpoint(zx::channel connector, uint32_t remote_addr,
                            uint32_t* local_addr_out);

  void FreeEndpoint(uint32_t local_addr);

  void InstallSharedMemoryRecord(uint64_t id,
                                 fbl::unique_ptr<SharedMemoryRecord> rec);

  void RemoveSharedMemoryRecord(uint64_t id);

  zx_status_t SendMessageToPeer(uint32_t local, uint32_t remote, void* data,
                                size_t data_len);

  zx::unowned_resource shm_rsc_;

 private:
  // Both client and server agent should implement its own ctrl message handler
  virtual zx_status_t HandleCtrlMessage(gz_ipc_msg_hdr* msg_hdr) = 0;

  void ServeEndpointLocked(uint32_t remote_addr)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  zx_status_t CreateEndpointAndSendReplyLocked(zx::channel& ch,
                                               uint32_t remote_addr)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  zx_status_t HandleConnectResponseLocked(
      fbl::unique_ptr<GzIpcEndpoint>& endpoint, void* msg, size_t msg_len)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  zx_status_t DispatchEndpointMessageLocked(
      fbl::unique_ptr<GzIpcEndpoint>& endpoint, gz_ipc_msg_hdr* msg_hdr)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  zx_status_t AllocEndpointLocked(zx::channel connector, uint32_t remote_addr,
                                  uint32_t* local_addr_out)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  void InstallSharedMemoryRecordLocked(uint64_t id,
                                       fbl::unique_ptr<SharedMemoryRecord> rec)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  zx_status_t HandleEndpointMessage(gz_ipc_msg_hdr* msg_hdr);

  void SendFreeVmoMessage(uint64_t id);

  void SendConnectResponseMessage(zx_status_t status, uint32_t local_addr,
                                  uint32_t remote_addr);

  // Overriden from |MessageHandler|
  zx_status_t OnMessage(Message msg) override;

  fbl::Mutex lock_;
  gzos_utils::IdAllocator<kMaxEndpointNumber> id_allocator_
      FXL_GUARDED_BY(lock_);
  std::map<uint32_t, fbl::unique_ptr<GzIpcEndpoint>> endpoint_table_
      FXL_GUARDED_BY(lock_);

  std::map<uint64_t, fbl::unique_ptr<SharedMemoryRecord>> shm_rec_table_
      FXL_GUARDED_BY(lock_);
};

}  // namespace ree_agent
