// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include "lib/gzos/trusty_ipc/cpp/channel.h"

namespace trusty_ipc {

zx_status_t TipcChannelImpl::Init(uint32_t num_items, size_t item_size) {
  fbl::AutoLock lock(&msg_list_lock_);

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
    return ZX_ERR_PEER_CLOSED;
  }

  if (!shared_items) {
    return ZX_ERR_NO_MEMORY;
  }

  peer_shared_items_.resize(shared_items->size());
  for (auto& shared_item : *shared_items) {
    uint32_t msg_id = shared_item.msg_id;
    auto item = fbl::make_unique<MessageItem>(msg_id);
    if (!item) {
      return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = item->InitFromVmo(fbl::move(shared_item.vmo));
    if (status != ZX_OK) {
      return status;
    }

    peer_shared_items_[msg_id] = fbl::move(item);
  }

  return ZX_OK;
}

void TipcChannelImpl::Close() {
  fbl::AutoLock lock(&ready_lock_);
  ready_ = false;

  // Notify user that peer is going to shutdown channel
  SignalEvent(TipcEvent::HUP);

  if (close_callback_) {
    close_callback_();
  }
}

void TipcChannelImpl::RequestSharedMessageItems(
    RequestSharedMessageItemsCallback callback) {
  fbl::AutoLock lock(&msg_list_lock_);
  FXL_CHECK(initialized_);

  fidl::VectorPtr<SharedMessageItem> shared_items;

  for (const auto& item : free_list_) {
    SharedMessageItem shared_item;

    shared_item.vmo = item.GetDuplicateVmo();
    shared_item.msg_id = item.msg_id();
    shared_items.push_back(std::move(shared_item));
  }

  callback(std::move(shared_items));
}

void TipcChannelImpl::GetFreeMessageItem(GetFreeMessageItemCallback callback) {
  fbl::AutoLock lock(&msg_list_lock_);

  if (free_list_.is_empty()) {
    callback(ZX_ERR_NO_MEMORY, 0);
    return;
  }

  auto item = free_list_.pop_front();
  uint32_t msg_id = item->msg_id();
  outgoing_list_.push_back(fbl::move(item));

  callback(ZX_OK, msg_id);
}

void TipcChannelImpl::NotifyMessageItemIsFilled(
    uint32_t msg_id, uint64_t filled_size,
    NotifyMessageItemIsFilledCallback callback) {
  fbl::AutoLock lock(&msg_list_lock_);

  auto iter = outgoing_list_.find_if(
      [&msg_id](const MessageItem& item) { return item.msg_id() == msg_id; });
  if (iter == outgoing_list_.end()) {
    callback(ZX_ERR_NOT_FOUND);
    return;
  }

  auto item = outgoing_list_.erase(iter);
  item->update_filled_size(filled_size);
  filled_list_.push_back(fbl::move(item));

  SignalEvent(TipcEvent::MSG);

  if (message_in_callback_) {
    message_in_callback_();
  }

  callback(ZX_OK);
}

void TipcChannelImpl::Shutdown() {
  fbl::AutoLock lock(&ready_lock_);
  ready_ = false;

  // Notify peer that channel is going to be shutdown
  if (peer_.is_bound()) {
    peer_->Close();
    peer_.Unbind();
  }

  // The reference count held by callbacks should be released
  SetReadyCallback(nullptr);
  SetCloseCallback(nullptr);
  SetMessageInCallback(nullptr);

  TipcObject::Shutdown();
}

void TipcChannelImpl::NotifyReady() {
  bool ret = peer_->Ready();
  if (!ret) {
    FXL_LOG(ERROR) << "failed to notify peer ready";
    return;
  }

  fbl::AutoLock lock(&ready_lock_);
  ready_ = true;
}

zx_status_t TipcChannelImpl::SendMessage(void* msg, size_t msg_size) {
  FXL_DCHECK(msg);
  fbl::AutoLock lock(&msg_list_lock_);

  uint32_t msg_id;
  zx_status_t status;

  if (!is_bound()) {
    SignalEvent(TipcEvent::HUP);
    return ZX_ERR_PEER_CLOSED;
  }

  if (!is_ready()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  {
    fbl::AutoLock lock(&request_shared_items_lock_);
    if (!peer_shared_items_ready_) {
      zx_status_t err = PopulatePeerSharedItemsLocked();
      if (err != ZX_OK) {
        FXL_LOG(ERROR) << "failed to populate peer shared items " << err;
        return err;
      }
      peer_shared_items_ready_ = true;
    }
  }

  bool ret = peer_->GetFreeMessageItem(&status, &msg_id);
  if (!ret) {
    SignalEvent(TipcEvent::HUP);
    return ZX_ERR_PEER_CLOSED;
  }

  if (status != ZX_OK) {
    return status;
  }

  auto item = peer_shared_items_[msg_id].get();
  if (msg_size > item->size()) {
    return ZX_ERR_NO_MEMORY;
  }

  void* buffer_ptr = item->PtrFromOffset(0);
  memcpy(buffer_ptr, msg, msg_size);

  ret = peer_->NotifyMessageItemIsFilled(msg_id, msg_size, &status);
  if (!ret) {
    SignalEvent(TipcEvent::HUP);
    return ZX_ERR_PEER_CLOSED;
  }

  return status;
}

zx_status_t TipcChannelImpl::GetMessage(uint32_t* msg_id, size_t* len) {
  FXL_DCHECK(msg_id);
  FXL_DCHECK(len);

  fbl::AutoLock lock(&msg_list_lock_);

  auto item = filled_list_.pop_front();
  if (item == nullptr) {
    FXL_DLOG(INFO) << "no message item found";
    ClearEvent(TipcEvent::MSG);
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

  fbl::AutoLock lock(&msg_list_lock_);

  auto it = read_list_.find_if(
      [&msg_id](const MessageItem& item) { return msg_id == item.msg_id(); });

  if (it == read_list_.end()) {
    return ZX_ERR_NOT_FOUND;
  }

  size_t filled_size = it->filled_size();
  if (offset > filled_size) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  size_t bytes_to_copy = filled_size - offset;
  if (*buf_size < bytes_to_copy) {
    return ZX_ERR_NO_MEMORY;
  }

  memcpy(buf, it->PtrFromOffset(offset), bytes_to_copy);
  *buf_size = bytes_to_copy;

  return ZX_OK;
}

zx_status_t TipcChannelImpl::PutMessage(uint32_t msg_id) {
  fbl::AutoLock lock(&msg_list_lock_);

  auto it = read_list_.find_if(
      [&msg_id](const MessageItem& item) { return msg_id == item.msg_id(); });

  if (it == read_list_.end()) {
    return ZX_ERR_NOT_FOUND;
  }

  auto item = read_list_.erase(it);
  free_list_.push_back(fbl::move(item));

  return ZX_OK;
}

void TipcChannelImpl::Ready() {
  fbl::AutoLock lock(&ready_lock_);
  ready_ = true;

  if (ready_callback_) {
    ready_callback_();
  }
}

}  // namespace trusty_ipc
