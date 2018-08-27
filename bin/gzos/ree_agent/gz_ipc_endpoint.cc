// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/gz_ipc_endpoint.h"
#include "garnet/bin/gzos/ree_agent/gz_ipc_agent.h"
#include "garnet/bin/gzos/ree_agent/gz_ipc_msg.h"

#include "lib/fsl/handles/object_info.h"

namespace ree_agent {

zx_status_t GzIpcEndpoint::Write(void* msg, size_t msg_len,
                                 zx_handle_t* handles, size_t num_handles) {
  return zx_channel_write(message_reader_.channel(), 0, msg, msg_len, handles,
                          num_handles);
}

zx_status_t GzIpcEndpoint::OnMessage(Message message) {
  auto endpoint_hdr = message.Alloc<gz_ipc_endpoint_msg_hdr>();
  endpoint_hdr->num_handles = 0;

  auto& handles = message.handles();
  for (uint32_t i = 0; i < handles.actual(); i++) {
    zx_handle_t handle = handles.data()[i];

    switch (fsl::GetType(handle)) {
      case ZX_OBJ_TYPE_CHANNEL: {
        zx::channel ch(handle);
        uint32_t local_addr;

        zx_status_t status = agent_->AllocEndpoint(
            std::move(ch), kInvalidEndpointAddress, &local_addr);
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to allocate endpoint: " << status;
          return status;
        }

        endpoint_hdr->handles[i].type = HandleType::CHANNEL;
        endpoint_hdr->handles[i].channel.remote = local_addr;
      } break;

      default:
        return ZX_ERR_BAD_HANDLE;
    }

    endpoint_hdr->num_handles++;
  }

  auto msg_hdr = message.Alloc<gz_ipc_msg_hdr>();
  msg_hdr->src = local_addr_;
  msg_hdr->dst = remote_addr_;
  msg_hdr->reserved = 0;
  msg_hdr->flags = 0;
  msg_hdr->len = message.actual() - sizeof(gz_ipc_msg_hdr);

  return agent_->Write(message.data(), message.actual());
}

}  // namespace ree_agent
