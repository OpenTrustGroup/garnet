// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/gzos/ree_agent/ta_service.h"
#include "garnet/bin/gzos/ree_agent/tipc_agent.h"

namespace ree_agent {

struct conn_rsp_msg {
  struct tipc_ctrl_msg_hdr ctrl_msg;
  struct tipc_conn_rsp_body body;
};

struct disc_req_msg {
  struct tipc_ctrl_msg_hdr ctrl_msg;
  struct tipc_disc_req_body body;
};

zx_status_t TipcEndpointTable::AllocateSlot(uint32_t src_addr,
                                            fbl::RefPtr<TipcChannelImpl> chan,
                                            uint32_t* dst_addr) {
  FXL_DCHECK(dst_addr);
  FXL_DCHECK(chan != nullptr);
  FXL_DCHECK(chan->cookie() == nullptr);

  uint32_t slot;
  zx_status_t err = id_allocator_.Alloc(&slot);
  if (err != ZX_OK) {
    return err;
  }

  table_[slot].src_addr = src_addr;
  table_[slot].channel = chan;

  chan->set_cookie(&table_[slot]);

  *dst_addr = to_addr(slot);
  return ZX_OK;
}

TipcEndpoint* TipcEndpointTable::LookupByAddr(uint32_t dst_addr) {
  uint32_t slot_id = to_slot_id(dst_addr);
  if (slot_id >= kTipcAddrMaxNum) {
    FXL_LOG(WARNING) << "Invalid slot_id: " << slot_id;
    return nullptr;
  }

  if (id_allocator_.InUse(slot_id)) {
    return &table_[slot_id];
  }
  return nullptr;
}

TipcEndpoint* TipcEndpointTable::FindInUseSlot(uint32_t& start_slot) {
  uint32_t i;
  for (i = start_slot; i < kTipcAddrMaxNum; i++) {
    if (id_allocator_.InUse(i)) {
      start_slot = i;
      return &table_[i];
    }
  }
  return nullptr;
}

void TipcEndpointTable::FreeSlotByAddr(uint32_t dst_addr) {
  FreeSlotInternal(to_slot_id(dst_addr));
}

void TipcEndpointTable::FreeSlotInternal(uint32_t slot_id) {
  if (slot_id >= kTipcAddrMaxNum) {
    FXL_LOG(WARNING) << "Invalid slot_id: " << slot_id;
    return;
  }

  if (id_allocator_.InUse(slot_id)) {
    table_[slot_id].src_addr = 0;
    table_[slot_id].channel->set_cookie(nullptr);
    table_[slot_id].channel.reset();

    id_allocator_.Free(slot_id);
  }
}

TipcAgent::TipcAgent(uint32_t id, zx::channel ch, size_t max_msg_size,
                     TaServices& service_provider, TipcEndpointTable* ep_table)
    : ReeAgent(id, std::move(ch), max_msg_size),
      ta_service_provider_(service_provider),
      ep_table_(ep_table) {}

TipcAgent::~TipcAgent() {}

zx_status_t TipcAgent::SendMessageToRee(uint32_t local, uint32_t remote,
                                        void* data, size_t data_len) {
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

void TipcAgent::ShutdownTipcChannelLocked(TipcEndpoint* ep, uint32_t dst_addr) {
  FXL_DCHECK(ep);

  zx_status_t st;
  disc_req_msg req{{DISCONNECT_REQUEST, sizeof(tipc_disc_req_body)},
                   {ep->src_addr}};
  st = SendMessageToRee(dst_addr, kTipcCtrlAddress, &req, sizeof(req));
  if (st != ZX_OK) {
    FXL_LOG(WARNING) << "failed to send disconnect request, status=" << st;
  }

  ep->channel->Shutdown();
  ep_table_->FreeSlotByAddr(dst_addr);
}

zx_status_t TipcAgent::Start() {
  tipc_ctrl_msg_hdr ctrl_msg{CtrlMessage::GO_ONLINE, 0};

  fbl::AutoLock lock(&lock_);
  write_buffer_.reset(new char[max_message_size()]);
  if (write_buffer_ == nullptr) {
    FXL_LOG(ERROR) << "Failed to allocate write buffer";
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = SendMessageToRee(kTipcCtrlAddress, kTipcCtrlAddress,
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

  fbl::AutoLock lock(&lock_);

  write_buffer_.reset();

  uint32_t slot_id = 0;
  TipcEndpoint* ep;
  while ((ep = ep_table_->FindInUseSlot(slot_id)) != nullptr) {
    ShutdownTipcChannelLocked(ep, ep_table_->to_addr(slot_id));
    // Find in-use slot again start from next slot_id
    slot_id++;
  }

  return ZX_OK;
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

  return (hdr->dst == kTipcCtrlAddress) ? HandleCtrlMessage(hdr)
                                        : HandleTipcMessage(hdr);
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
      if (ctrl_msg->body_len != sizeof(tipc_disc_req_body)) {
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
  auto send_err_conn_resp = fbl::MakeAutoCall([&]() {
    zx_status_t st;
    uint32_t err = static_cast<uint32_t>(ZX_ERR_NO_RESOURCES);
    conn_rsp_msg res{{CONNECT_RESPONSE, sizeof(tipc_conn_rsp_body)},
                     {src_addr, err, 0, 0, 0}};
    st =
        SendMessageToRee(kTipcCtrlAddress, kTipcCtrlAddress, &res, sizeof(res));
    if (st != ZX_OK) {
      FXL_LOG(ERROR) << "failed to send connect response, status=" << st;
    }
  });

  fbl::RefPtr<TipcChannelImpl> channel;
  channel = fbl::MakeRefCounted<TipcChannelImpl>();
  if (!channel) {
    FXL_LOG(ERROR) << "failed to create tipc channel";
    return ZX_ERR_NO_MEMORY;
  }

  uint32_t dst_addr;
  zx_status_t status;
  {
    fbl::AutoLock lock(&lock_);

    status = ep_table_->AllocateSlot(src_addr, channel, &dst_addr);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to allocate endpoint slot, status=" << status;
      return status;
    }
  }

  auto free_endpoint_slot = fbl::MakeAutoCall([this, &dst_addr]() {
    fbl::AutoLock lock(&lock_);
    ep_table_->FreeSlotByAddr(dst_addr);
  });

  channel->SetReadyCallback([this, src_addr, dst_addr] {
    zx_status_t st;
    conn_rsp_msg res{{CONNECT_RESPONSE, sizeof(tipc_conn_rsp_body)},
                     {src_addr, ZX_OK, dst_addr, kTipcChanMaxBufSize, 1}};
    st =
        SendMessageToRee(kTipcCtrlAddress, kTipcCtrlAddress, &res, sizeof(res));
    if (st != ZX_OK) {
      FXL_LOG(ERROR) << "failed to send connect response, status=" << st;
    }
  });

  auto handle_hup = [this, channel, src_addr, dst_addr] {
    channel->Shutdown();

    zx_status_t st;
    disc_req_msg req{{DISCONNECT_REQUEST, sizeof(tipc_disc_req_body)},
                     {src_addr}};
    st = SendMessageToRee(dst_addr, kTipcCtrlAddress, &req, sizeof(req));
    if (st != ZX_OK) {
      FXL_LOG(ERROR) << "failed to send disconnect request, status=" << st;
    }

    fbl::AutoLock lock(&lock_);
    ep_table_->FreeSlotByAddr(dst_addr);
  };

  channel->SetCloseCallback([handle_hup] {
    async::PostTask(async_get_default(), [handle_hup] { handle_hup(); });
  });

  auto handle_rx_msg = [this, channel, dst_addr] {
    fbl::AutoLock lock(&lock_);

    uint32_t msg_id;
    size_t msg_len;
    zx_status_t st = channel->GetMessage(&msg_id, &msg_len);
    if (st != ZX_OK) {
      FXL_LOG(ERROR) << "no message come in";
      return;
    }

    void* buf = static_cast<void*>(write_buffer_.get());
    size_t buf_size = max_message_size();
    st = channel->ReadMessage(msg_id, 0, buf, &buf_size);
    if (st != ZX_OK) {
      FXL_LOG(ERROR) << "failed to read message, status=" << st;
      return;
    }

    FXL_DCHECK(msg_len == buf_size);

    st = channel->PutMessage(msg_id);
    if (st != ZX_OK) {
      FXL_LOG(WARNING) << "failed to put message, status=" << st;
    }

    auto ep = static_cast<TipcEndpoint*>(channel->cookie());
    if (!ep) {
      FXL_LOG(ERROR) << "cannot get tipc endpoint from channel cookie";
      return;
    }

    st = SendMessageToRee(dst_addr, ep->src_addr, buf, buf_size);
    if (st != ZX_OK) {
      FXL_LOG(ERROR) << "failed to send message to ree, status=" << st;
    }
  };

  channel->SetMessageInCallback([handle_rx_msg] {
    async::PostTask(async_get_default(), [handle_rx_msg] { handle_rx_msg(); });
  });

  TipcPortSyncPtr port_client;
  std::string port_name(conn_req->name);
  ta_service_provider_.ConnectToService(port_client.NewRequest().TakeChannel(),
                                        port_name);

  uint32_t num_items;
  uint64_t item_size;
  bool ret = port_client->GetInfo(&num_items, &item_size);
  if (!ret) {
    FXL_LOG(ERROR) << "internal error on calling port->GetInfo()";
    return ZX_ERR_INTERNAL;
  }

  status = channel->Init(num_items, item_size);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to init channel, status=" << status;
    return status;
  }

  fidl::InterfaceHandle<TipcChannel> peer_handle;
  auto local_handle = channel->GetInterfaceHandle();
  ret = port_client->Connect(std::move(local_handle), nullptr, &status,
                             &peer_handle);
  if (!ret) {
    FXL_LOG(ERROR) << "internal error on calling port->Connect()";
    return ZX_ERR_INTERNAL;
  }

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to do port->Connect(), status=" << status;
    return status;
  }

  channel->BindPeerInterfaceHandle(std::move(peer_handle));

  send_err_conn_resp.cancel();
  free_endpoint_slot.cancel();
  return ZX_OK;
}

zx_status_t TipcAgent::HandleDisconnectRequest(uint32_t src_addr, void* req) {
  FXL_DCHECK(req);

  fbl::AutoLock lock(&lock_);

  auto disc_req = reinterpret_cast<tipc_disc_req_body*>(req);
  uint32_t dst_addr = disc_req->target;

  TipcEndpoint* ep = ep_table_->LookupByAddr(dst_addr);
  if (!ep) {
    FXL_LOG(ERROR) << "invalid target address, addr:" << dst_addr;
    return ZX_ERR_INVALID_ARGS;
  }

  ShutdownTipcChannelLocked(ep, dst_addr);
  return ZX_OK;
}

zx_status_t TipcAgent::HandleTipcMessage(tipc_hdr* hdr) {
  FXL_DCHECK(hdr);

  fbl::AutoLock lock(&lock_);

  TipcEndpoint* ep = ep_table_->LookupByAddr(hdr->dst);
  if (ep && (ep->src_addr == hdr->src)) {
    void* msg = reinterpret_cast<void*>(hdr->data);
    zx_status_t status = ep->channel->SendMessage(msg, hdr->len);
    if (status == ZX_ERR_PEER_CLOSED) {
      ShutdownTipcChannelLocked(ep, hdr->dst);
    }

    return status;
  }

  return ZX_ERR_NOT_FOUND;
}

}  // namespace ree_agent
