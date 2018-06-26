// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include "lib/ree_agent/cpp/channel.h"

namespace ree_agent {

zx_status_t TipcChannelImpl::Create(uint32_t num_items, size_t item_size,
                                    fbl::RefPtr<TipcChannelImpl>* out) {
  auto chan = fbl::AdoptRef(new TipcChannelImpl());
  if (!chan) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::AutoLock lock(&chan->msg_list_lock_);
  for (uint32_t i = 0; i < num_items; i++) {
    auto item = fbl::make_unique<MessageItem>(i);
    if (!item) {
      return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = item->InitNew(item_size);
    if (status != ZX_OK) {
      return status;
    }
    chan->free_list_.push_back(fbl::move(item));
  }

  chan->num_items_ = num_items;
  *out = fbl::move(chan);
  return ZX_OK;
}

zx_status_t TipcChannelImpl::PopulatePeerSharedItemsLocked() {
  fidl::VectorPtr<SharedMessageItem> shared_items;
  peer_->RequestSharedMessageItems(&shared_items);

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

void TipcChannelImpl::Close() {}

void TipcChannelImpl::RequestSharedMessageItems(
    RequestSharedMessageItemsCallback callback) {
  fbl::AutoLock lock(&msg_list_lock_);
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

  callback(ZX_OK);
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

  peer_->GetFreeMessageItem(&status, &msg_id);
  if (status != ZX_OK) {
    return status;
  }

  auto item = peer_shared_items_[msg_id].get();
  if (msg_size > item->size()) {
    return ZX_ERR_NO_MEMORY;
  }

  void* buffer_ptr = item->PtrFromOffset(0);
  memcpy(buffer_ptr, msg, msg_size);

  peer_->NotifyMessageItemIsFilled(msg_id, msg_size, &status);
  return status;
}

zx_status_t TipcChannelImpl::GetMessage(uint32_t* msg_id, size_t* len) {
  FXL_DCHECK(msg_id);
  FXL_DCHECK(len);
  fbl::AutoLock lock(&msg_list_lock_);

  if (filled_list_.is_empty()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  auto item = filled_list_.pop_front();

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
  if (ready_callback_) {
    ready_callback_();
  }
}

}  // namespace ree_agent
