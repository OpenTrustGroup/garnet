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
#include "lib/gzos/trusty_ipc/cpp/id_alloc.h"

namespace trusty_ipc {

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
class TipcObjectObserver;
class TipcObjectSet;

struct TipcObjectRef
    : public fbl::RefCounted<TipcObjectRef>,
      public fbl::DoublyLinkedListable<fbl::RefPtr<TipcObjectRef>> {
  TipcObjectRef() = delete;

  bool InPendingList() { return pending_list_node.InContainer(); }
  bool InChildList() { return child_list_node.InContainer(); }

  TipcObjectRef(TipcObject* o) : obj(o) {}

  TipcObjectObserver* parent;

  fbl::RefPtr<TipcObject> obj;

  using NodeState = fbl::DoublyLinkedListNodeState<fbl::RefPtr<TipcObjectRef>>;
  NodeState pending_list_node;
  NodeState child_list_node;
};

class TipcObjectObserver {
 public:
  virtual void OnChildRemoved(fbl::RefPtr<TipcObjectRef> child_ref) = 0;

  virtual void OnEvent(fbl::RefPtr<TipcObjectRef> child_ref) = 0;
};

class TipcObject : public fbl::RefCounted<TipcObject> {
 public:
  static constexpr zx_signals_t EVENT_PENDING = ZX_USER_SIGNAL_0;

  enum ObjectType { PORT, CHANNEL, OBJECT_SET, ANY };
  static constexpr uint32_t kInvalidHandle = UINT_MAX;

  TipcObject();
  virtual ~TipcObject();

  virtual zx_status_t Wait(WaitResult* result, zx::time deadline);
  virtual void Close();
  virtual uint32_t tipc_event_state();

  void SignalEvent(uint32_t set_mask);

  void ClearEvent(uint32_t clear_mask);

  zx_status_t AddParent(TipcObjectSet* parent,
                        fbl::RefPtr<TipcObjectRef>* child_ref_out);
  void RemoveParent(TipcObjectSet* parent);

  bool is_port() { return (get_type() == ObjectType::PORT); }
  bool is_channel() { return (get_type() == ObjectType::CHANNEL); }
  bool is_object_set() { return (get_type() == ObjectType::OBJECT_SET); }

  ObjectType type() { return get_type(); }
  uint32_t handle_id() const { return handle_id_; }
  void* cookie() const { return cookie_; }
  void set_cookie(void* cookie) { cookie_ = cookie; }

 protected:
  virtual ObjectType get_type() = 0;

  bool IsMyAncestor(TipcObjectSet* ancestor);

 private:
  friend class TipcObjectManager;

  void RemoveAllParents();

  uint32_t handle_id_;
  void* cookie_;

  zx::event event_;

  using RefList = fbl::DoublyLinkedList<fbl::RefPtr<TipcObjectRef>>;

  RefList ref_list_ FXL_GUARDED_BY(mutex_);
  uint32_t tipc_event_state_ FXL_GUARDED_BY(mutex_);
  fbl::Mutex mutex_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcObject);
};

}  // namespace trusty_ipc
