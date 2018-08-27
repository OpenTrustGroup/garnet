// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>

#include "garnet/bin/gzos/ree_agent/gz_ipc_agent.h"

namespace ree_agent {

zx_status_t GzIpcAgent::SendMessageToPeer(uint32_t local, uint32_t remote,
                                          void* data, size_t data_len) {
  size_t msg_size = sizeof(gz_ipc_msg_hdr) + data_len;
  fbl::unique_ptr<char> buf(new char[msg_size]);

  if (buf == nullptr)
    return ZX_ERR_NO_MEMORY;

  auto hdr = reinterpret_cast<gz_ipc_msg_hdr*>(buf.get());
  hdr->src = local;
  hdr->dst = remote;
  hdr->reserved = 0;
  hdr->len = data_len;
  hdr->flags = 0;
  memcpy(hdr->data, data, data_len);

  return Write(buf.get(), msg_size);
}

zx_status_t GzIpcAgent::AllocEndpoint(zx::channel connector,
                                      uint32_t remote_addr,
                                      uint32_t* local_addr_out) {
  fbl::AutoLock lock(&lock_);
  return AllocEndpointLocked(std::move(connector), remote_addr, local_addr_out);
}

zx_status_t GzIpcAgent::AllocEndpointLocked(zx::channel connector,
                                            uint32_t remote_addr,
                                            uint32_t* local_addr_out) {
  zx_status_t status;
  uint32_t local_addr = 0;

  auto send_reply_msg =
      fbl::MakeAutoCall([this, &status, &remote_addr, &local_addr]() {
        uint32_t err = static_cast<uint32_t>(status);
        conn_rsp_msg res{
            {CtrlMessageType::CONNECT_RESPONSE, sizeof(gz_ipc_conn_rsp_body)},
            {remote_addr, err, local_addr}};
        FXL_CHECK(SendMessageToPeer(kCtrlEndpointAddress, remote_addr, &res,
                                    sizeof(res)) == ZX_OK);
      });

  if (remote_addr == kInvalidEndpointAddress) {
    send_reply_msg.cancel();
  }

  status = id_allocator_.Alloc(&local_addr);
  if (status != ZX_OK) {
    return status;
  }
  auto free_local_addr = fbl::MakeAutoCall(
      [this, &local_addr]() { id_allocator_.Free(local_addr); });

  auto endpoint = fbl::make_unique<GzIpcEndpoint>(this, local_addr, remote_addr,
                                                  std::move(connector));
  if (!endpoint) {
    FXL_LOG(ERROR) << "failed to allocate endpoint, status=" << status;
    return ZX_ERR_NO_MEMORY;
  }

  endpoint->reader().set_error_handler(
      [this, local_addr] { FreeEndpoint(local_addr); });
  endpoint_table_.emplace(local_addr, std::move(endpoint));

  free_local_addr.cancel();
  if (local_addr_out) {
    *local_addr_out = local_addr;
  }
  return ZX_OK;
}

void GzIpcAgent::FreeEndpoint(uint32_t local_addr) {
  fbl::AutoLock lock(&lock_);

  auto it = endpoint_table_.find(local_addr);
  if (it != endpoint_table_.end()) {
    auto& endpoint = it->second;
    if (endpoint->remote_addr() != kInvalidEndpointAddress) {
      disc_req_msg disc_req{
          {CtrlMessageType::DISCONNECT_REQUEST, sizeof(gz_ipc_disc_req_body)},
          {endpoint->remote_addr()}};

      FXL_CHECK(SendMessageToPeer(local_addr, kCtrlEndpointAddress, &disc_req,
                                  sizeof(disc_req)) == ZX_OK);
    }
    endpoint_table_.erase(it);
  }

  id_allocator_.Free(local_addr);
}

zx_status_t GzIpcAgent::HandleConnectResponseLocked(
    fbl::unique_ptr<GzIpcEndpoint>& endpoint, void* msg, size_t msg_len) {
  if (msg_len != sizeof(conn_rsp_msg)) {
    FXL_LOG(ERROR) << "Invalid conn rsp msg";
    return ZX_ERR_INTERNAL;
  }

  auto conn_rsp = reinterpret_cast<conn_rsp_msg*>(msg);
  if (conn_rsp->hdr.type != CtrlMessageType::CONNECT_RESPONSE) {
    FXL_LOG(ERROR) << "Invalid conn rsp msg";
    return ZX_ERR_INTERNAL;
  }

  if (conn_rsp->hdr.body_len != sizeof(gz_ipc_conn_rsp_body)) {
    FXL_LOG(ERROR) << "Invalid body length";
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status = conn_rsp->body.status;
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Connect request failed, status=" << status;
    return status;
  }

  endpoint->set_remote_addr(conn_rsp->body.target);
  return ZX_OK;
}

zx_status_t GzIpcAgent::HandleEndpointMessage(gz_ipc_msg_hdr* msg_hdr) {
  fbl::AutoLock lock(&lock_);

  auto it = endpoint_table_.find(msg_hdr->dst);
  if (it != endpoint_table_.end()) {
    auto& endpoint = it->second;

    if (endpoint->remote_addr() == kInvalidEndpointAddress) {
      zx_status_t status =
          HandleConnectResponseLocked(endpoint, msg_hdr->data, msg_hdr->len);
      if (status != ZX_OK) {
        endpoint_table_.erase(msg_hdr->dst);
        id_allocator_.Free(msg_hdr->dst);
      }
      return ZX_OK;

    } else if (endpoint->remote_addr() == msg_hdr->src) {
      return DispatchEndpointMessageLocked(endpoint, msg_hdr);
    }
  }

  FXL_LOG(ERROR) << "Endpoint addr " << msg_hdr->src
                 << " not found, msg dropped";
  return ZX_OK;
}

zx_status_t GzIpcAgent::DispatchEndpointMessageLocked(
    fbl::unique_ptr<GzIpcEndpoint>& endpoint, gz_ipc_msg_hdr* msg_hdr) {
  auto hdr = reinterpret_cast<gz_ipc_endpoint_msg_hdr*>(msg_hdr->data);
  size_t num_handles = 0;
  zx_handle_t handles[kDefaultHandleCapacity];

  for (uint32_t i = 0; i < hdr->num_handles; i++) {
    switch (hdr->handles[i].type) {
      case HandleType::CHANNEL: {
        zx::channel ch0, ch1;
        auto channel_info = hdr->handles[i].channel;

        zx_status_t status = zx::channel::create(0, &ch0, &ch1);
        if (status != ZX_OK) {
          return status;
        }

        status = AllocEndpointLocked(std::move(ch0), channel_info.remote);
        if (status != ZX_OK) {
          return status;
        }

        handles[i] = ch1.release();
      } break;

      case HandleType::VMO:
        FXL_CHECK(false) << "Not implemented";

      default:
        FXL_CHECK(false) << "Bad handle";
    }

    num_handles++;
  }

  auto payload = hdr + 1;
  auto payload_len = msg_hdr->len - sizeof(gz_ipc_endpoint_msg_hdr);
  return endpoint->Write(payload, payload_len, handles, num_handles);
}

zx_status_t GzIpcAgent::HandleDisconnectRequest(gz_ipc_ctrl_msg_hdr* ctrl_hdr) {
  FXL_CHECK(ctrl_hdr);

  if (ctrl_hdr->body_len != sizeof(gz_ipc_disc_req_body)) {
    FXL_LOG(ERROR) << "Invalid disc req msg";
    return ZX_ERR_INTERNAL;
  }

  fbl::AutoLock lock(&lock_);
  auto body = reinterpret_cast<gz_ipc_disc_req_body*>(ctrl_hdr + 1);

  endpoint_table_.erase(body->target);
  id_allocator_.Free(body->target);
  return ZX_OK;
}

zx_status_t GzIpcAgent::OnMessage(Message msg) {
  void* buf = msg.data();
  size_t msg_size = msg.actual();

  if (msg_size < sizeof(gz_ipc_msg_hdr)) {
    FXL_LOG(ERROR) << "Invalid msg";
    return ZX_ERR_INTERNAL;
  }

  auto msg_hdr = reinterpret_cast<gz_ipc_msg_hdr*>(buf);

  if (msg_hdr->dst == kCtrlEndpointAddress) {
    return HandleCtrlMessage(msg_hdr);
  }

  return HandleEndpointMessage(msg_hdr);
}

zx_status_t GzIpcAgent::Start() { return message_reader_.Start(); };

zx_status_t GzIpcAgent::Stop() {
  message_reader_.Stop();
  return ZX_OK;
};

}  // namespace ree_agent
