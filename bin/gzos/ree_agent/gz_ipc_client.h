// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/mutex.h>
#include <zx/eventpair.h>

#include "garnet/bin/gzos/ree_agent/gz_ipc_agent.h"

#include "lib/fidl/cpp/interface_request.h"
#include "magma_util/simple_allocator.h"

namespace ree_agent {

class GzIpcClient : public GzIpcAgent {
 public:
  GzIpcClient(zx::unowned_resource shm_rsc, zx::channel message_channel,
              size_t max_message_size)
      : GzIpcAgent(std::move(shm_rsc), std::move(message_channel),
                   max_message_size) {
    zx_info_resource_t info;
    zx_status_t status = shm_rsc_->get_info(ZX_INFO_RESOURCE, &info,
                                            sizeof(info), nullptr, nullptr);
    FXL_CHECK(status == ZX_OK);

    alloc_ = magma::SimpleAllocator::Create(info.base, info.size);
    FXL_CHECK(alloc_);
  }

  template <typename INTERFACE>
  zx_status_t Connect(fidl::InterfaceRequest<INTERFACE> request) {
    std::string service_name = INTERFACE::Name_;
    return Connect(service_name, request.TakeChannel());
  }

  zx_status_t Connect(std::string service_name, zx::channel channel);

  zx_status_t AllocSharedMemory(size_t size, zx::vmo* vmo_out);

 private:
  zx_status_t HandleFreeVmo(gz_ipc_ctrl_msg_hdr* ctrl_hdr);

  zx_status_t HandleCtrlMessage(gz_ipc_msg_hdr* msg_hdr) override;

  fbl::Mutex alloc_lock_;
  std::unique_ptr<magma::SimpleAllocator> alloc_ FXL_GUARDED_BY(alloc_lock_);
};

}  // namespace ree_agent
