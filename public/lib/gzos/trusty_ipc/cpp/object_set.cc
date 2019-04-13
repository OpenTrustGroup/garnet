// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gzos/trusty_ipc/cpp/object_set.h"
#include "lib/gzos/trusty_ipc/cpp/object_manager.h"

namespace trusty_ipc {

zx_status_t TipcObjectSet::AddObject(fbl::RefPtr<TipcObject> obj) {
  return AddObject(obj, nullptr, ~0u);
}

zx_status_t TipcObjectSet::AddObject(fbl::RefPtr<TipcObject> obj,
                                     void* cookie, uint32_t event_mask) {
  FXL_DCHECK(obj);
  fbl::RefPtr<TipcObjectRef> child_ref;

  // Sanity check before add
  if (obj->is_object_set()) {
    auto obj_set = static_cast<TipcObjectSet*>(obj.get());

    // Add self is not allowed
    if (obj_set == this) {
      return ZX_ERR_INVALID_ARGS;
    }

    // Circular reference is not allowed
    if (IsMyAncestor(obj_set)) {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  zx_status_t err = obj->AddParent(this, &child_ref);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add parent: " << err;
    return err;
  }

  child_ref->cookie = cookie;
  child_ref->event_mask = event_mask;

  // If the object already has event before adding to object set,
  // we need to add it to pending list, or the event may lost
  auto event = obj->tipc_event_state();
  if (event) {
    obj->SignalEvent(event);
  }

  AppendToChildList(child_ref);
  return ZX_OK;
}

void TipcObjectSet::RemoveObject(fbl::RefPtr<TipcObject> obj) {
  FXL_DCHECK(obj);
  obj->RemoveParent(this);
}

zx_status_t TipcObjectSet::ModifyObject(fbl::RefPtr<TipcObject> obj,
                                     void* cookie, uint32_t event_mask) {
  FXL_DCHECK(obj);
  {
    fbl::AutoLock lock(&mutex_);

    auto it = child_list_.find_if(
        [&obj](const TipcObjectRef& ref) { return ref.obj == obj; });

    if (it == child_list_.end()) {
      return ZX_ERR_NOT_FOUND;
    }

    it->cookie = cookie;
    it->event_mask = event_mask;
  }

  auto event = obj->tipc_event_state();
  if (event) {
    obj->SignalEvent(event);
  }

  return ZX_OK;
}

void TipcObjectSet::OnChildRemoved(fbl::RefPtr<TipcObjectRef> child_ref) {
  RemoveFromPendingList(child_ref);
  RemoveFromChildList(child_ref);
}

void TipcObjectSet::OnEvent(fbl::RefPtr<TipcObjectRef> child_ref) {
  AppendToPendingList(child_ref);
}

void TipcObjectSet::AppendToPendingList(fbl::RefPtr<TipcObjectRef> child_ref) {
  fbl::AutoLock lock(&mutex_);

  if (!child_ref->InPendingList()) {
    pending_list_.push_back(child_ref);
    SignalEvent(TipcEvent::READY);
  }
}

void TipcObjectSet::RemoveFromPendingList(
    fbl::RefPtr<TipcObjectRef> child_ref) {
  fbl::AutoLock lock(&mutex_);

  if (!child_ref->InPendingList()) {
    return;
  }

  pending_list_.erase_if([&child_ref](const TipcObjectRef& ref) {
    return ref.obj == child_ref->obj;
  });

  if (pending_list_.is_empty()) {
    ClearEvent(TipcEvent::READY);
  }
}

void TipcObjectSet::AppendToChildList(fbl::RefPtr<TipcObjectRef> child_ref) {
  fbl::AutoLock lock(&mutex_);

  if (!child_ref->InChildList()) {
    child_list_.push_back(child_ref);
  }
}

void TipcObjectSet::RemoveFromChildList(fbl::RefPtr<TipcObjectRef> child_ref) {
  fbl::AutoLock lock(&mutex_);

  if (child_ref->InChildList()) {
    child_list_.erase_if([&child_ref](const TipcObjectRef& ref) {
      return ref.obj == child_ref->obj;
    });
  }
}

bool TipcObjectSet::PollPendingEvents(WaitResult* result) {
  fbl::AutoLock lock(&mutex_);

  while (auto ref = pending_list_.pop_front()) {
    auto obj = ref->obj;
    zx_signals_t event = obj->tipc_event_state() & ref->event_mask;

    if (event) {
      result->handle_id = obj->handle_id();
      result->event = event;
      result->cookie = is_root_ ? obj->cookie() : ref->cookie;

      pending_list_.push_back(ref);
      return true;
    }
  }

  ClearEvent(TipcEvent::READY);

  return false;
}

zx_status_t TipcObjectSet::Wait(WaitResult* result, zx::time deadline) {
  FXL_DCHECK(result);

  {
    fbl::AutoLock lock(&mutex_);
    if (child_list_.is_empty()) {
      return ZX_ERR_NOT_FOUND;
    }
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

void TipcObjectSet::Close() {
  for (;;) {
    fbl::RefPtr<TipcObjectRef> ref;
    {
      fbl::AutoLock lock(&mutex_);
      ref = child_list_.pop_front();
    }

    if (ref == nullptr) {
      break;
    }

    FXL_LOG(INFO) << "remove child from object set, child handle: "
                  << ref->obj->handle_id();
    ref->obj->RemoveParent(this);
  }

  TipcObject::Close();
}

}  // namespace trusty_ipc