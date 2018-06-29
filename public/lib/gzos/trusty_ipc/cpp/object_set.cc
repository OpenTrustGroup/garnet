// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gzos/trusty_ipc/cpp/object_set.h"
#include "lib/gzos/trusty_ipc/cpp/object_manager.h"

namespace trusty_ipc {

zx_status_t TipcObjectSet::AddObject(fbl::RefPtr<TipcObject> obj) {
  FXL_DCHECK(obj);
  fbl::RefPtr<TipcObjectRef> child_ref;

  zx_status_t err = obj->AddParent(this, &child_ref);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add parent: " << err;
    return err;
  }

  // If the object already has event before adding to object set,
  // we need to add it to pending list, or the event may lost
  if (child_ref->obj->tipc_event_state() != 0) {
    AppendToPendingList(child_ref);
  }

  fbl::AutoLock lock(&mutex_);
  children_count_++;

  return ZX_OK;
}

void TipcObjectSet::RemoveObject(fbl::RefPtr<TipcObject> obj) {
  FXL_DCHECK(obj);
  obj->RemoveParent(this);
}

void TipcObjectSet::OnChildRemoved(fbl::RefPtr<TipcObjectRef> child_ref) {
  RemoveFromPendingList(child_ref);

  fbl::AutoLock lock(&mutex_);
  children_count_--;
}

void TipcObjectSet::OnEvent(fbl::RefPtr<TipcObjectRef> child_ref) {
  AppendToPendingList(child_ref);
}

void TipcObjectSet::AppendToPendingList(fbl::RefPtr<TipcObjectRef> child_ref) {
  fbl::AutoLock lock(&mutex_);

  // ignore if already in the pending list
  for (const auto& ref : pending_list_) {
    if (ref.obj == child_ref->obj) {
      return;
    }
  }

  pending_list_.push_back(child_ref);
  SignalEvent(TipcEvent::READY);
}

void TipcObjectSet::RemoveFromPendingList(
    fbl::RefPtr<TipcObjectRef> child_ref) {
  fbl::AutoLock lock(&mutex_);

  pending_list_.erase_if([&child_ref](const TipcObjectRef& ref) {
    return ref.obj == child_ref->obj;
  });

  if (pending_list_.is_empty()) {
    ClearEvent(TipcEvent::READY);
  }
}

bool TipcObjectSet::PollPendingEvents(WaitResult* result) {
  fbl::AutoLock lock(&mutex_);

  while (auto ref = pending_list_.pop_front()) {
    auto obj = ref->obj;
    zx_signals_t event = obj->tipc_event_state();

    if (event) {
      result->handle_id = obj->handle_id();
      result->event = event;
      result->cookie = obj->cookie();

      pending_list_.push_back(ref);
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

}  // namespace trusty_ipc
