// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>

#include "lib/gzos/trusty_ipc/cpp/channel.h"
#include "lib/gzos/trusty_ipc/cpp/object_manager.h"
#include "lib/gzos/trusty_ipc/cpp/port.h"

namespace trusty_ipc {

void TipcPortImpl::AddPendingRequest(fbl::RefPtr<TipcChannelImpl> channel) {
  fbl::AutoLock lock(&mutex_);
  pending_requests_.push_back(channel);
}

fbl::RefPtr<TipcChannelImpl> TipcPortImpl::GetPendingRequest() {
  fbl::AutoLock lock(&mutex_);
  return pending_requests_.pop_front();
};

bool TipcPortImpl::HasPendingRequests() {
  fbl::AutoLock lock(&mutex_);
  return !pending_requests_.is_empty();
}

void TipcPortImpl::Connect(fidl::InterfaceHandle<TipcChannel> peer_handle,
                           ConnectCallback callback) {
  fbl::RefPtr<TipcChannelImpl> channel;
  zx_status_t err = TipcChannelImpl::Create(num_items_, item_size_, &channel);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create channel object " << err;
    callback(err, nullptr);
  }

  channel->BindPeerInterfaceHandle(std::move(peer_handle));
  AddPendingRequest(channel);

  SignalEvent(TipcEvent::READY);

  auto local_handle = channel->GetInterfaceHandle();
  callback(ZX_OK, std::move(local_handle));
}

void TipcPortImpl::GetInfo(GetInfoCallback callback) {
  callback(num_items_, item_size_);
}

zx_status_t TipcPortImpl::Accept(fbl::RefPtr<TipcChannelImpl>* channel_out) {
  FXL_DCHECK(channel_out);
  if (!HasPendingRequests()) {
    return ZX_ERR_SHOULD_WAIT;
  }
  auto channel = GetPendingRequest();

  zx_status_t err = TipcObjectManager::Instance()->InstallObject(channel);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to install channel object: " << err;
    return err;
  }
  channel->NotifyReady();

  *channel_out = fbl::move(channel);
  return ZX_OK;
}

}  // namespace trusty_ipc
