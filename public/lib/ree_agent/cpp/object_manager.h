// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <ree_agent/cpp/fidl.h>
#include <zx/port.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/ree_agent/cpp/object_set.h"

namespace ree_agent {

static constexpr uint32_t kMaxHandle = 64;

class TipcObjectManager {
 public:
  static TipcObjectManager* Instance();

  zx_status_t InstallObject(fbl::RefPtr<TipcObject> obj);
  void RemoveObject(uint32_t handle_id);
  zx_status_t GetObject(uint32_t handle_id, fbl::RefPtr<TipcObject>* obj_out);

  zx_status_t Wait(WaitResult* result, zx::time deadline);

 private:
  TipcObjectManager()
      : root_obj_set_(fbl::AdoptRef(new TipcObjectSet())) {}

  fbl::RefPtr<TipcObject> object_table_[kMaxHandle] FXL_GUARDED_BY(
      object_table_lock_);
  fbl::Mutex object_table_lock_;

  fbl::RefPtr<TipcObjectSet> root_obj_set_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcObjectManager);
};

}  // namespace ree_agent
