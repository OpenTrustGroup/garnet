// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/object_set.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

zx_status_t TipcObjectSet::AddObject(fbl::RefPtr<TipcObject> obj) {
  FXL_DCHECK(obj);

  zx_status_t err = obj->AddParent(this);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add parent object " << err;
    return err;
  }

  fbl::AutoLock lock(&mutex_);
  children_count_++;

  return ZX_OK;
}

void TipcObjectSet::RemoveObject(fbl::RefPtr<TipcObject> obj) {
  FXL_DCHECK(obj);
  obj->RemoveParent(this);

  fbl::AutoLock lock(&mutex_);
  children_count_--;
}

zx_status_t TipcObjectSet::AppendToPendingList(TipcObject* obj) {
  FXL_DCHECK(obj);
  fbl::AutoLock lock(&mutex_);

  // ignore if already in the pending list
  if (pending_list_.Contains(obj->handle_id())) {
    return ZX_OK;
  }

  // TODO(sy): try not alloc memory here, thus we don't need
  // to return error code in this function
  auto ref = fbl::make_unique<TipcObjectRef>(obj);
  if (!ref) {
    return ZX_ERR_NO_MEMORY;
  }
  pending_list_.push_back(fbl::move(ref));

  return ZX_OK;
}

zx_status_t TipcObjectSet::SignalEvent(uint32_t set_mask,
                                       TipcObject* notifier) {
  if (set_mask != TipcEvent::READY) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t err = AppendToPendingList(notifier);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add to pending list: " << err;
    return err;
  }

  return TipcObject::SignalEvent(set_mask);
}

void TipcObjectSet::RemoveFromPendingList(TipcObject* obj) {
  FXL_DCHECK(obj);
  fbl::AutoLock lock(&mutex_);

  auto it = pending_list_.Find(obj->handle_id());
  if (it != pending_list_.end()) {
    pending_list_.erase(it);
  }
}

bool TipcObjectSet::PollPendingEvents(WaitResult* result) {
  fbl::AutoLock lock(&mutex_);

  while (auto ref = pending_list_.pop_front()) {
    zx_signals_t event = (*ref)->tipc_event_state();

    if (event) {
      result->handle_id = (*ref)->handle_id();
      result->event = event;
      result->cookie = (*ref)->cookie();

      pending_list_.push_back(fbl::move(ref));
      return true;
    }
  }

  ClearEvent(TipcEvent::READY);

  return false;
}

uint32_t TipcObjectSet::children_count() {
  fbl::AutoLock lock(&mutex_);
  return children_count_;
}

zx_status_t TipcObjectSet::Wait(WaitResult* result, zx::time deadline) {
  FXL_DCHECK(result);

  if (children_count() == 0) {
    return ZX_ERR_NOT_FOUND;
  }

  while (true) {
    bool found = PollPendingEvents(result);
    if (found) {
      break;
    }

    zx_status_t err = TipcObject::Wait(result, deadline);
    if (err != ZX_OK) {
      return err;
    }
  }

  return ZX_OK;
}

}  // namespace ree_agent
