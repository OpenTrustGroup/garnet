// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/fxl/random/uuid.h"
#include "lib/gzos/trusty_ipc/cpp/channel.h"
#include "lib/gzos/trusty_ipc/cpp/object_manager.h"
#include "lib/gzos/trusty_ipc/cpp/port.h"

namespace trusty_ipc {

void TipcPortImpl::AddPendingRequest(fbl::RefPtr<TipcChannelImpl> channel) {
  fbl::AutoLock lock(&mutex_);
  pending_requests_.push_back(fbl::move(channel));
}

void TipcPortImpl::RemoveFromPendingRequest(fbl::RefPtr<TipcChannelImpl> ch) {
  fbl::AutoLock lock(&mutex_);

  pending_requests_.erase_if(
      [ch](const TipcChannelImpl& ref) { return (ch.get() == &ref); });
}

fbl::RefPtr<TipcChannelImpl> TipcPortImpl::GetPendingRequest() {
  fbl::AutoLock lock(&mutex_);
  return pending_requests_.pop_front();
};

bool TipcPortImpl::HasPendingRequests() {
  fbl::AutoLock lock(&mutex_);
  return !pending_requests_.is_empty();
}

void TipcPortImpl::Connect(fidl::InterfaceHandle<gzos::trusty::ipc::TipcChannel> peer_handle,
                           fidl::StringPtr uuid, ConnectCallback callback) {
  fbl::RefPtr<TipcChannelImpl> channel;
  channel = fbl::MakeRefCounted<TipcChannelImpl>();
  if (!channel) {
    FXL_LOG(ERROR) << "Failed to create channel object";
    callback(ZX_ERR_NO_MEMORY, nullptr);
    return;
  }

  if (uuid.is_null()) {
    // check access for non-secure client
    if (!(flags_ & IPC_PORT_ALLOW_NS_CONNECT)) {
      callback(ZX_ERR_ACCESS_DENIED, nullptr);
      return;
    }
  } else {
    // check access for secure client
    if (!(flags_ & IPC_PORT_ALLOW_TA_CONNECT)) {
      callback(ZX_ERR_ACCESS_DENIED, nullptr);
      return;
    }

    if (!fxl::IsValidUUID(uuid.get())) {
      callback(ZX_ERR_INVALID_ARGS, nullptr);
      return;
    }

    void* cookie = new std::string(uuid.get());
    if (!cookie) {
      callback(ZX_ERR_NO_MEMORY, nullptr);
      return;
    }
    channel->set_cookie(cookie);
  }

  auto handle_hup = [this, channel] {
    channel->UnBind();
  };

  channel->SetHupCallback([handle_hup] {
    async::PostTask(async_get_default_dispatcher(), [handle_hup] { handle_hup(); });
  });

  zx_status_t status = channel->Init(num_items_, item_size_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to init channel " << status;
    callback(status, nullptr);
    return;
  }
  channel->Bind(std::move(peer_handle));

  auto local_handle = channel->GetInterfaceHandle();
  AddPendingRequest(fbl::move(channel));
  SignalEvent(TipcEvent::READY);
  callback(ZX_OK, std::move(local_handle));
}

void TipcPortImpl::GetInfo(GetInfoCallback callback) {
  callback(num_items_, item_size_);
}

zx_status_t TipcPortImpl::Accept(std::string* uuid_out,
                                 fbl::RefPtr<TipcChannelImpl>* channel_out) {
  FXL_DCHECK(channel_out);
  auto channel = GetPendingRequest();
  if (channel == nullptr) {
    return ZX_ERR_SHOULD_WAIT;
  }
  auto close_channel = fbl::MakeAutoCall([&channel]() { channel->Close(); });

  if (!HasPendingRequests()) {
    ClearEvent(TipcEvent::READY);
  }

  void* cookie = channel->cookie();
  if (cookie) {
    auto uuid = reinterpret_cast<std::string*>(cookie);
    FXL_DCHECK(fxl::IsValidUUID(*uuid));

    if (uuid_out) {
      uuid_out->assign(uuid->c_str());
    }
    delete uuid;
  }

  if (!channel->IsBound()) {
    return ZX_ERR_PEER_CLOSED;
  }

  zx_status_t err = TipcObjectManager::Instance()->InstallObject(channel);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to install channel object: " << err;
    return err;
  }

  // remove channel hup callback and user should take care of
  // channel HUP event by itself
  channel->set_cookie(nullptr);
  channel->SetHupCallback(nullptr);
  channel->NotifyReady();

  *channel_out = fbl::move(channel);
  close_channel.cancel();
  return ZX_OK;
}

void TipcPortImpl::Close() {
  fbl::AutoLock lock(&mutex_);
  bindings_.CloseAll();

  while (auto channel = pending_requests_.pop_back()) {
    channel->Close();
  }

  TipcObject::Close();
}

zx_status_t PortConnectFacade::Connect(std::string path, fidl::StringPtr uuid) {
  FXL_DCHECK(port_service_connector_);

  gzos::trusty::ipc::TipcPortSyncPtr port_client;
  zx_status_t status = port_service_connector_(port_client, path);

  // We can simply return ZX_OK if the user wants to wait for port
  // it's user's responsibility to re-connect the port when it is ready
  if (status == ZX_ERR_SHOULD_WAIT) {
    return ZX_OK;
  }

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to connect to port service, status=" << status;
    return status;
  }

  uint32_t num_items;
  uint64_t item_size;
  zx_status_t ret = port_client->GetInfo(&num_items, &item_size);
  if (ret != ZX_OK) {
    FXL_LOG(ERROR) << "bind to a non-existed port service";
    return ZX_ERR_NOT_FOUND;
  }

  status = channel_->Init(num_items, item_size);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to init channel, status=" << status;
    return status;
  }

  fidl::InterfaceHandle<gzos::trusty::ipc::TipcChannel> peer_handle;
  auto local_handle = channel_->GetInterfaceHandle();
  ret = port_client->Connect(std::move(local_handle), uuid, &status,
                             &peer_handle);
  if (ret != ZX_OK) {
    FXL_LOG(ERROR) << "failed to call port->Connect(), ret=" << ret;
    return ret;
  }

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to do port->Connect(), status=" << status;
    return status;
  }

  channel_->Bind(std::move(peer_handle));
  return ZX_OK;
}

}  // namespace trusty_ipc