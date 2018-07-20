// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <ree_agent/cpp/fidl.h>
#include <zx/port.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/gzos/trusty_ipc/cpp/object.h"

namespace trusty_ipc {

class TipcObjectSet : public TipcObject, public TipcObjectObserver {
 public:
  TipcObjectSet() = default;
  zx_status_t AddObject(fbl::RefPtr<TipcObject> obj);
  void RemoveObject(fbl::RefPtr<TipcObject> obj);

  virtual zx_status_t Wait(WaitResult* result, zx::time deadline) override;
  virtual void Close() override;

 protected:
  ObjectType get_type() override { return ObjectType::OBJECT_SET; }

 private:
  // |TipcObjectObserver|
  void OnChildRemoved(fbl::RefPtr<TipcObjectRef> child_ref) override;
  void OnEvent(fbl::RefPtr<TipcObjectRef> child_ref) override;

  void AppendToPendingList(fbl::RefPtr<TipcObjectRef> child_ref);
  void RemoveFromPendingList(fbl::RefPtr<TipcObjectRef> child_ref);

  void AppendToChildList(fbl::RefPtr<TipcObjectRef> child_ref);
  void RemoveFromChildList(fbl::RefPtr<TipcObjectRef> child_ref);

  bool PollPendingEvents(WaitResult* result);

  struct PendingListTraits {
    static TipcObjectRef::NodeState& node_state(TipcObjectRef& ref) {
      return ref.pending_list_node;
    }
  };
  using PendingList =
      fbl::DoublyLinkedList<fbl::RefPtr<TipcObjectRef>, PendingListTraits>;

  struct ChildListTraits {
    static TipcObjectRef::NodeState& node_state(TipcObjectRef& ref) {
      return ref.child_list_node;
    }
  };
  using ChildList =
      fbl::DoublyLinkedList<fbl::RefPtr<TipcObjectRef>, ChildListTraits>;

  PendingList pending_list_ FXL_GUARDED_BY(mutex_);
  ChildList child_list_ FXL_GUARDED_BY(mutex_);
  fbl::Mutex mutex_;
};

}  // namespace trusty_ipc
