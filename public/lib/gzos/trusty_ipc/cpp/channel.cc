// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include "lib/gzos/trusty_ipc/cpp/channel.h"

namespace trusty_ipc {

zx_status_t TipcChannelImpl::Init(uint32_t num_items, size_t item_size) {
  fbl::AutoLock lock(&lock_);
  FXL_DCHECK(!initialized_);

  for (uint32_t i = 0; i < num_items; i++) {
    auto item = fbl::make_unique<MessageItem>(i);
    if (!item) {
      return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = item->InitNew(item_size);
    if (status != ZX_OK) {
      return status;
    }
    free_list_.push_back(fbl::move(item));
  }
  initialized_ = true;

  return ZX_OK;
}

zx_status_t TipcChannelImpl::PopulatePeerSharedItemsLocked() {
  fidl::VectorPtr<SharedMessageItem> shared_items;
  bool ret = peer_->RequestSharedMessageItems(&shared_items);
  if (!ret) {
    UnBindLocked();
    return ZX_ERR_INTERNAL;
  }

  if (!shared_items) {
    return ZX_ERR_NO_MEMORY;
  }

  peer_shared_items_.resize(shared_items->size());
  for (auto& shared_item : *shared_items) {
    uint32_t msg_id = shared_item.msg_id;
    uint32_t size = shared_item.size;
    auto item = fbl::make_unique<MessageItem>(msg_id);
    if (!item) {
      return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = item->InitFromVmo(fbl::move(shared_item.vmo), size);
    if (status != ZX_OK) {
      return status;
    }

    peer_shared_items_[msg_id] = fbl::move(item);
  }

  return ZX_OK;
}

void TipcChannelImpl::Hup() {
  fbl::AutoLock lock(&lock_);
  ready_ = false;

  // Notify user that peer is going to close channel
  SignalEvent(TipcEvent::HUP);

  if (hup_callback_) {
    hup_callback_();
  }
}

void TipcChannelImpl::RequestSharedMessageItems(
    RequestSharedMessageItemsCallback callback) {
  fbl::AutoLock lock(&lock_);
  FXL_CHECK(initialized_);

  fidl::VectorPtr<SharedMessageItem> shared_items;

  for (const auto& item : free_list_) {
    SharedMessageItem shared_item;

    shared_item.vmo = item.GetDuplicateVmo();
    shared_item.size = item.size();
    shared_item.msg_id = item.msg_id();
    shared_items.push_back(std::move(shared_item));
  }

  callback(std::move(shared_items));
}

void TipcChannelImpl::GetFreeMessageItem(GetFreeMessageItemCallback callback) {
  fbl::AutoLock lock(&lock_);

  if (free_list_.is_empty()) {
    callback(ZX_ERR_UNAVAILABLE, 0);
    no_free_item_ = true;
    return;
  }

  auto item = free_list_.pop_front();
  uint32_t msg_id = item->msg_id();
  outgoing_list_.push_back(fbl::move(item));

  callback(ZX_OK, msg_id);
}

void TipcChannelImpl::NotifyMessageItemIsFilled(uint32_t msg_id,
                                                uint64_t filled_size) {
  fbl::AutoLock lock(&lock_);

  auto iter = outgoing_list_.find_if(
      [&msg_id](const MessageItem& item) { return item.msg_id() == msg_id; });
  FXL_CHECK(iter != outgoing_list_.end());

  auto item = outgoing_list_.erase(iter);
  item->update_filled_size(filled_size);
  filled_list_.push_back(fbl::move(item));

  SignalEvent(TipcEvent::MSG);

  if (message_callback_) {
    message_callback_();
  }
}

void TipcChannelImpl::Bind(fidl::InterfaceHandle<TipcChannel> handle) {
  peer_.Bind(std::move(handle));
}

void TipcChannelImpl::UnBind() {
  fbl::AutoLock lock(&lock_);
  UnBindLocked();
}

void TipcChannelImpl::UnBindLocked() {
  ready_ = false;

  // The reference count held by callbacks should be released
  ready_callback_ = nullptr;
  hup_callback_ = nullptr;
  message_callback_ = nullptr;
  close_callback_ = nullptr;

  // Notify peer that channel is going to be shutdown
  if (peer_.is_bound()) {
    peer_->Hup();
    peer_.Unbind();
  }
}

void TipcChannelImpl::Close() {
  fbl::AutoLock lock(&lock_);
  if (close_callback_) {
    close_callback_();
  }

  UnBindLocked();
  TipcObject::Close();
}

void TipcChannelImpl::NotifyReady() {
  bool ret = peer_->Ready();
  if (!ret) {
    FXL_LOG(ERROR) << "failed to notify peer ready";
    return;
  }

  fbl::AutoLock lock(&lock_);
  ready_ = true;
}

zx_status_t TipcChannelImpl::CheckMessageParameter(ipc_msg_t* msg) {
  if (!msg) {
    return ZX_ERR_INTERNAL;
  }

  if (msg->num_handles && !msg->handles) {
    return ZX_ERR_INTERNAL;
  }

  if (msg->num_iov) {
    if (!msg->iov) {
      return ZX_ERR_INTERNAL;
    }

    for (uint32_t i = 0; i < msg->num_iov; i++) {
      if (!msg->iov[i].base) {
        return ZX_ERR_INTERNAL;
      }
    }
  }

  return ZX_OK;
}

zx_status_t TipcChannelImpl::SendMessage(void* buf, size_t buf_size) {
  FXL_DCHECK(buf);

  iovec_t iov[1] = {{buf, buf_size}};
  ipc_msg_t msg = {1, iov, 0, NULL};

  size_t actual_send = buf_size;
  zx_status_t status = SendMessage(&msg, actual_send);
  if (status != ZX_OK) {
    return status;
  }

  if (actual_send != buf_size) {
    FXL_LOG(ERROR) << "no available buffer for sending full message";
    return ZX_ERR_NO_RESOURCES;
  }

  return ZX_OK;
}

zx_status_t TipcChannelImpl::SendMessage(ipc_msg_t* msg, size_t& actual_send) {
  FXL_DCHECK(msg);
  fbl::AutoLock lock(&lock_);

  uint32_t msg_id;
  zx_status_t status;

  if (!is_bound()) {
    return ZX_ERR_PEER_CLOSED;
  }

  if (!ready_) {
    return ZX_ERR_SHOULD_WAIT;
  }

  if (!peer_shared_items_ready_) {
    zx_status_t err = PopulatePeerSharedItemsLocked();
    if (err != ZX_OK) {
      FXL_LOG(ERROR) << "failed to populate peer shared items " << err;
      return err;
    }
    peer_shared_items_ready_ = true;
  }

  status = CheckMessageParameter(msg);
  if (status != ZX_OK) {
    return status;
  }

  bool ret = peer_->GetFreeMessageItem(&status, &msg_id);
  if (!ret) {
    UnBindLocked();
    return ZX_ERR_INTERNAL;
  }

  if (status != ZX_OK) {
    return status;
  }

  auto item = peer_shared_items_[msg_id].get();

  char* buffer_ptr = static_cast<char*>(item->PtrFromOffset(0));
  size_t buffer_size = item->size();

  actual_send = 0;
  for (uint32_t i = 0; i < msg->num_iov; i++) {
    iovec* iov = &msg->iov[i];
    FXL_DCHECK(iov);
    size_t iov_len = (iov->len < buffer_size) ? iov->len : buffer_size;
    memcpy(buffer_ptr, iov->base, iov_len);
    buffer_ptr += iov_len;
    buffer_size -= iov_len;
    actual_send += iov_len;

    if (buffer_size == 0) {
      break;
    }
  }

  ret = peer_->NotifyMessageItemIsFilled(msg_id, actual_send);
  if (!ret) {
    UnBindLocked();
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t TipcChannelImpl::GetMessage(uint32_t* msg_id, size_t* len) {
  FXL_DCHECK(msg_id);
  FXL_DCHECK(len);

  fbl::AutoLock lock(&lock_);

  auto item = filled_list_.pop_front();
  if (item == nullptr) {
    return ZX_ERR_SHOULD_WAIT;
  }

  if (filled_list_.is_empty()) {
    ClearEvent(TipcEvent::MSG);
  }

  *msg_id = item->msg_id();
  *len = item->filled_size();
  read_list_.push_back(fbl::move(item));

  return ZX_OK;
}

zx_status_t TipcChannelImpl::ReadMessage(uint32_t msg_id, uint32_t offset,
                                         void* buf, size_t* buf_size) {
  FXL_DCHECK(buf);
  FXL_DCHECK(buf_size);

  iovec_t iov[1] = {{buf, *buf_size}};
  ipc_msg_t msg = {1, iov, 0, NULL};

  size_t actual_read = 0;
  zx_status_t status = ReadMessage(msg_id, offset, &msg, actual_read);

  if (status == ZX_OK) {
    *buf_size = actual_read;
  }

  return status;
}

zx_status_t TipcChannelImpl::ReadMessage(uint32_t msg_id, uint32_t offset,
                                         ipc_msg_t* msg, size_t& actual_read) {
  FXL_DCHECK(msg);

  fbl::AutoLock lock(&lock_);

  auto it = read_list_.find_if(
      [&msg_id](const MessageItem& item) { return msg_id == item.msg_id(); });

  if (it == read_list_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t filled_size = it->filled_size();
  if (offset > filled_size) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = CheckMessageParameter(msg);
  if (status != ZX_OK) {
    return status;
  }

  char* buffer_ptr = static_cast<char*>(it->PtrFromOffset(offset));
  size_t buffer_size = filled_size - offset;

  actual_read = 0;
  for (uint32_t i = 0; i < msg->num_iov; i++) {
    iovec_t* iov = &msg->iov[i];
    FXL_DCHECK(iov);

    size_t iov_len = (iov->len < buffer_size) ? iov->len : buffer_size;
    memcpy(iov->base, buffer_ptr, iov_len);
    buffer_ptr += iov_len;
    buffer_size -= iov_len;
    actual_read += iov_len;

    if (buffer_size == 0) {
      break;
    }
  }

  return ZX_OK;
}

zx_status_t TipcChannelImpl::PutMessage(uint32_t msg_id) {
  fbl::AutoLock lock(&lock_);

  auto it = read_list_.find_if(
      [&msg_id](const MessageItem& item) { return msg_id == item.msg_id(); });

  if (it == read_list_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto item = read_list_.erase(it);
  free_list_.push_back(fbl::move(item));

  if (no_free_item_) {
    async::PostTask(async_get_default(), [this] {
      fbl::AutoLock lock(&lock_);
      no_free_item_ = false;

      bool ret = peer_->NotifyFreeItemAvailable();
      if (!ret) {
        FXL_LOG(ERROR) << "failed to notify peer we have free item available";
        UnBindLocked();
      }
    });
  }

  return ZX_OK;
}

void TipcChannelImpl::NotifyFreeItemAvailable() {
  // Notify user that peer has free item available
  SignalEvent(TipcEvent::SEND_UNBLOCKED);
}

void TipcChannelImpl::Ready() {
  fbl::AutoLock lock(&lock_);
  ready_ = true;

  if (ready_callback_) {
    ready_callback_();
  }
}

uint32_t TipcChannelImpl::ReadEvent() {
  auto event_state = tipc_event_state();

  if (event_state & TipcEvent::READY) {
    ClearEvent(TipcEvent::READY);
  }

  if (event_state & TipcEvent::SEND_UNBLOCKED) {
    ClearEvent(TipcEvent::SEND_UNBLOCKED);
  }

  return event_state;
}

}  // namespace trusty_ipc
