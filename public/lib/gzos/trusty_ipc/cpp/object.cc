// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gzos/trusty_ipc/cpp/object.h"
#include "lib/gzos/trusty_ipc/cpp/object_manager.h"
#include "lib/gzos/trusty_ipc/cpp/object_set.h"

namespace trusty_ipc {

TipcObject::TipcObject()
    : handle_id_(kInvalidHandle), cookie_(nullptr), tipc_event_state_(0) {
  zx_status_t err = zx::event::create(0, &event_);
  FXL_CHECK(err == ZX_OK);
}

TipcObject::~TipcObject() = default;

zx_status_t TipcObject::AddParent(TipcObjectSet* parent,
                                  fbl::RefPtr<TipcObjectRef>* child_ref_out) {
  FXL_DCHECK(parent);
  FXL_DCHECK(child_ref_out);
  fbl::AutoLock lock(&mutex_);

  for (const auto& ref : ref_list_) {
    if (ref.parent == parent) {
      return ZX_ERR_ALREADY_EXISTS;
    }
  }

  auto ref = fbl::MakeRefCounted<TipcObjectRef>(this);
  if (!ref) {
    return ZX_ERR_NO_MEMORY;
  }
  ref->parent = parent;

  ref_list_.push_back(ref);
  *child_ref_out = ref;

  return ZX_OK;
}

void TipcObject::RemoveParent(TipcObjectSet* parent) {
  FXL_DCHECK(parent);
  fbl::AutoLock lock(&mutex_);

  auto it = ref_list_.find_if(
      [&parent](const TipcObjectRef& ref) { return ref.parent == parent; });

  if (it != ref_list_.end()) {
    auto ref = ref_list_.erase(it);
    it->parent->OnChildRemoved(ref);
  }
}

void TipcObject::RemoveAllParents() {
  fbl::AutoLock lock(&mutex_);
  while (auto ref = ref_list_.pop_front()) {
    ref->parent->OnChildRemoved(ref);
  }
}

bool TipcObject::IsMyAncestor(TipcObjectSet* ancestor) {
  fbl::AutoLock lock(&mutex_);

  if (ref_list_.is_empty()) {
    return false;
  }

  for (const auto& ref : ref_list_) {
    if (ref.parent == ancestor) {
      return true;
    }

    auto parent = static_cast<TipcObjectSet*>(ref.parent);
    if (parent->IsMyAncestor(ancestor)) {
      return true;
    }
  }

  return false;
}

void TipcObject::SignalEvent(uint32_t set_mask) {
  if (!set_mask) {
    return;
  }

  fbl::AutoLock lock(&mutex_);
  for (auto& ref : ref_list_) {
    ref.parent->OnEvent(fbl::WrapRefPtr(&ref));
  }

  tipc_event_state_ |= set_mask;
  zx_status_t err = event_.signal(0x0, EVENT_PENDING);
  FXL_DCHECK(err == ZX_OK);
}

void TipcObject::ClearEvent(uint32_t clear_mask) {
  fbl::AutoLock lock(&mutex_);

  tipc_event_state_ &= ~clear_mask;
  if (!tipc_event_state_) {
    zx_status_t err = event_.signal(EVENT_PENDING, 0x0);
    FXL_DCHECK(err == ZX_OK);
  }
}

zx_signals_t TipcObject::tipc_event_state() {
  fbl::AutoLock lock(&mutex_);
  return tipc_event_state_;
}

zx_status_t TipcObject::Wait(WaitResult* result, zx::time deadline) {
  FXL_DCHECK(result);

  zx_signals_t observed;
  zx_status_t err = zx_object_wait_one(event_.get(), EVENT_PENDING,
                                       deadline.get(), &observed);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for event " << err;
    return err;
  }

  result->event = ReadEvent();
  result->cookie = cookie_;
  result->handle_id = handle_id_;

  return ZX_OK;
}

uint32_t TipcObject::ReadEvent() {
  return tipc_event_state();
}

void TipcObject::Close() {
  // The reference count held by object manager should be released
  if (handle_id() != TipcObject::kInvalidHandle) {
    TipcObjectManager::Instance()->RemoveObject(handle_id());
  }
}

}  // namespace trusty_ipc
