// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <ree_agent/cpp/fidl.h>
#include <zx/event.h>
#include <zx/port.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/ree_agent/cpp/id_alloc.h"

namespace ree_agent {

enum TipcEvent : uint32_t {
  READY = 0x1,
  ERROR = 0x2,
  HUP = 0x4,
  MSG = 0x8,
  SEND_UNBLOCKED = 0x10,
};

struct WaitResult {
  uint32_t handle_id;
  uint32_t event;
  void* cookie;
};

class TipcObject;
class TipcObjectSet;

class TipcObjectRef
    : public fbl::DoublyLinkedListable<fbl::unique_ptr<TipcObjectRef>> {
 public:
  TipcObjectRef() = delete;

  TipcObjectRef(TipcObject* obj) : obj_ref_(obj) {}

  TipcObject* get() const { return obj_ref_.get(); }

  TipcObject* operator->() const { return obj_ref_.get(); }

 private:
  fbl::RefPtr<TipcObject> obj_ref_;
};

class TipcObject : public fbl::RefCounted<TipcObject> {
 public:
  static constexpr zx_signals_t EVENT_PENDING = ZX_USER_SIGNAL_0;

  enum ObjectType { PORT, CHANNEL, OBJECT_SET };
  static constexpr uint32_t kInvalidHandle = UINT_MAX;

  TipcObject();
  virtual ~TipcObject();

  virtual zx_status_t Wait(WaitResult* result, zx::time deadline);

  virtual zx_status_t SignalEvent(uint32_t set_mask,
                                  TipcObject* notifier = nullptr);

  zx_status_t ClearEvent(uint32_t clear_mask);

  zx_status_t AddParent(TipcObjectSet* obj_set);
  void RemoveParent(TipcObjectSet* obj_set);
  void RemoveAllParents();

  bool is_port() { return (get_type() == ObjectType::PORT); }
  bool is_channel() { return (get_type() == ObjectType::CHANNEL); }
  bool is_object_set() { return (get_type() == ObjectType::OBJECT_SET); }

  uint32_t handle_id() const { return handle_id_; }
  void* cookie() { return cookie_; }

 protected:
  class TipcObjectRefList
      : public fbl::DoublyLinkedList<fbl::unique_ptr<TipcObjectRef>> {
   public:
    bool Contains(uint32_t handle_id) {
      auto it = find_if([&handle_id](const TipcObjectRef& ref) {
        return (handle_id == ref->handle_id());
      });
      return (it != end());
    }

    iterator Find(uint32_t handle_id) {
      auto it = find_if([&handle_id](const TipcObjectRef& ref) {
        return (handle_id == ref->handle_id());
      });
      return it;
    }
  };

  virtual ObjectType get_type() = 0;

 private:
  friend class TipcObjectManager;
  friend class TipcObjectSet;

  uint32_t handle_id_;
  void* cookie_;

  zx::event event_;
  uint32_t tipc_event_state();

  fbl::Mutex mutex_;
  uint32_t tipc_event_state_ FXL_GUARDED_BY(mutex_);
  TipcObjectRefList parent_list_ FXL_GUARDED_BY(mutex_);

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcObject);
};

}  // namespace ree_agent
