// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>

#include "garnet/bin/gzos/ree_agent/ta_service.h"
#include "garnet/bin/gzos/ree_agent/tipc_agent.h"

namespace ree_agent {

struct conn_rsp_msg {
  struct tipc_ctrl_msg_hdr  ctrl_msg;
  struct tipc_conn_rsp_body body;
};

static IdAllocator<kTipcAddrMaxNum> id_allocator_;

static inline uint32_t slot_to_addr(uint32_t slot) {
  return kTipcAddrBase + slot;
}

TipcAgent::TipcAgent(uint32_t id, zx::channel ch, size_t max_msg_size,
                     TaServices& service_provider)
    : ReeAgent(id, std::move(ch), max_msg_size),
      ta_service_provider_(service_provider) {}

TipcAgent::~TipcAgent() {}

zx_status_t TipcAgent::AllocateEndpointSlot(uint32_t src_addr,
                                            fbl::RefPtr<TipcChannelImpl> ch,
                                            uint32_t* dst_addr) {
  FXL_DCHECK(dst_addr);

  fbl::AutoLock lock(&ep_table_lock_);

  uint32_t slot;
  zx_status_t err = id_allocator_.Alloc(&slot);
  if (err != ZX_OK) {
    return err;
  }

  ep_table_[slot].src_addr = src_addr;
  ep_table_[slot].channel = ch;

  void* cookie = ch->cookie();
  cookie = &ep_table_[slot];

  *dst_addr = slot_to_addr(slot);
  return ZX_OK;
}

zx_status_t TipcAgent::SendMessage(uint32_t local, uint32_t remote, void* data,
                                   size_t data_len) {
  size_t msg_size = sizeof(tipc_hdr) + data_len;
  fbl::unique_ptr<char> buf(new char[msg_size]);

  if (buf == nullptr)
    return ZX_ERR_NO_MEMORY;

  auto hdr = reinterpret_cast<tipc_hdr*>(buf.get());
  hdr->src = local;
  hdr->dst = remote;
  hdr->reserved = 0;
  hdr->len = data_len;
  hdr->flags = 0;
  memcpy(hdr->data, data, data_len);

  return WriteMessage(buf.get(), msg_size);
}

zx_status_t TipcAgent::Start() {
  tipc_ctrl_msg_hdr ctrl_msg{CtrlMessage::GO_ONLINE, 0};

  zx_status_t status = SendMessage(kTipcCtrlAddress, kTipcCtrlAddress,
                                   &ctrl_msg, sizeof(ctrl_msg));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to send tipc go online message, status="
                   << status;
    return status;
  }

  return ReeAgent::Start();
}

zx_status_t TipcAgent::Stop() {
  zx_status_t status = ReeAgent::Stop();
  if (status == ZX_ERR_BAD_STATE) {
    // tipc agent ignore error if agent is already stopped.
    return ZX_OK;
  }

  // TODO(james): disconnect all Tipc channels

  return status;
}

zx_status_t TipcAgent::HandleMessage(void* buf, size_t size) {
  FXL_DCHECK(buf);

  if (size < sizeof(tipc_hdr)) {
    FXL_LOG(ERROR) << "message buffer size too small, size=" << size;
    return ZX_ERR_INVALID_ARGS;
  }

  auto hdr = reinterpret_cast<tipc_hdr*>(buf);

  if (size != (sizeof(tipc_hdr) + hdr->len)) {
    FXL_LOG(ERROR) << "invalid message length, length=" << size;
    return ZX_ERR_INVALID_ARGS;
  }

  return (hdr->dst == kTipcCtrlAddress) ?
      HandleCtrlMessage(hdr) : HandleTipcMessage(hdr);
}

zx_status_t TipcAgent::HandleCtrlMessage(tipc_hdr* hdr) {
  FXL_DCHECK(hdr);

  auto ctrl_msg = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr->data);
  auto msg_body = reinterpret_cast<void*>(ctrl_msg + 1);

  switch (ctrl_msg->type) {
    case CONNECT_REQUEST:
      if (ctrl_msg->body_len != sizeof(tipc_conn_req_body)) {
        break;
      }
      return HandleConnectRequest(hdr->src, msg_body);

    case DISCONNECT_REQUEST:
      if (ctrl_msg->body_len != sizeof(tipc_conn_rsp_body)) {
        break;
      }
      return HandleDisconnectRequest(hdr->src, msg_body);

    default:
      break;
  }

  FXL_LOG(ERROR) << "invalid ctrl message, type=" << ctrl_msg->type
                 << ", body_len=" << ctrl_msg->body_len;
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t TipcAgent::HandleConnectRequest(uint32_t src_addr, void* req) {
  FXL_DCHECK(req);

  auto conn_req = reinterpret_cast<tipc_conn_req_body*>(req);
  uint32_t dst_addr = 0;

  auto send_err_conn_resp = fbl::MakeAutoCall([&](){
    zx_status_t st;
    uint32_t err = static_cast<uint32_t>(ZX_ERR_NO_RESOURCES);
    conn_rsp_msg res{ {CONNECT_RESPONSE, sizeof(tipc_conn_rsp_body)},
                      {src_addr, err, dst_addr, 0, 0} };
    st = SendMessage(kTipcCtrlAddress, kTipcCtrlAddress, &res, sizeof(res));
    if (st != ZX_OK) {
      FXL_LOG(ERROR) << "failed to send connect response, status=" << st;
    }
  });

  TipcPortSyncPtr port_client;
  uint32_t num_items;
  uint64_t item_size;
  std::string port_name(conn_req->name);

  ta_service_provider_.ConnectToService(port_client.NewRequest().TakeChannel(),
                                        port_name);

  bool ret = port_client->GetInfo(&num_items, &item_size);
  if (!ret) {
    FXL_LOG(ERROR) << "internal error on calling port->GetInfo()";
    return ZX_ERR_INTERNAL;
  }

  fbl::RefPtr<TipcChannelImpl> channel;
  fidl::InterfaceHandle<TipcChannel> peer_handle;

  zx_status_t status = TipcChannelImpl::Create(num_items, item_size, &channel);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create tipc channel, status=" << status;
    return status;
  }

  auto local_handle = channel->GetInterfaceHandle();
  ret = port_client->Connect(std::move(local_handle), &status, &peer_handle);
  if (!ret) {
    FXL_LOG(ERROR) << "internal error on calling port->Connect()";
    return ZX_ERR_INTERNAL;
  }

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to do port->Connect(), status=" << status;
    return status;
  }

  status = AllocateEndpointSlot(src_addr, channel, &dst_addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to allocate endpoint slot, status=" << status;
    return status;
  }

  channel->SetReadyCallback([&]() {
    zx_status_t st;
    conn_rsp_msg res{ {CONNECT_RESPONSE, sizeof(tipc_conn_rsp_body)},
                      {src_addr, ZX_OK, dst_addr, kTipcChanMaxBufSize, 1} };
    st = SendMessage(kTipcCtrlAddress, kTipcCtrlAddress, &res, sizeof(res));
    if (st != ZX_OK) {
      FXL_LOG(ERROR) << "failed to send connect response, status=" << status;
    }
  });

  channel->BindPeerInterfaceHandle(std::move(peer_handle));
  send_err_conn_resp.cancel();
  return ZX_OK;
}

zx_status_t TipcAgent::HandleDisconnectRequest(uint32_t src_addr, void* req) {
  FXL_DCHECK(req);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TipcAgent::HandleTipcMessage(tipc_hdr* hdr) {
  FXL_DCHECK(hdr);
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace ree_agent
