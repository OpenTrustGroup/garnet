// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>

#include "lib/gzos/trusty_ipc/cpp/id_alloc.h"
#include "lib/gzos/trusty_ipc/cpp/object_manager.h"

namespace trusty_ipc {

static IdAllocator<kMaxHandle> id_allocator_;

TipcObjectManager* TipcObjectManager::Instance() {
  static TipcObjectManager* instance;
  static fbl::Mutex instance_lock;
  fbl::AutoLock lock(&instance_lock);

  if (!instance) {
    instance = new TipcObjectManager();
  }

  return instance;
}

zx_status_t TipcObjectManager::InstallObject(fbl::RefPtr<TipcObject> object) {
  fbl::AutoLock lock(&object_table_lock_);
  FXL_DCHECK(object->handle_id() == TipcObject::kInvalidHandle);

  uint32_t new_id;
  zx_status_t err = id_allocator_.Alloc(&new_id);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to allocate handle id " << err;
    return err;
  }
  object->handle_id_ = new_id;
  auto free_id = fbl::MakeAutoCall([&new_id]() { id_allocator_.Free(new_id); });

  err = root_obj_set_->AddObject(object);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add object to root object set " << err;
    return err;
  }

  FXL_DCHECK(object_table_[new_id] == nullptr);
  object_table_[new_id] = object;

  free_id.cancel();
  return ZX_OK;
}

void TipcObjectManager::RemoveObject(uint32_t handle_id) {
  if (handle_id >= kMaxHandle) {
    return;
  }

  fbl::AutoLock lock(&object_table_lock_);
  auto obj = fbl::move(object_table_[handle_id]);
  if (obj) {
    obj->RemoveAllParents();

    zx_status_t err = id_allocator_.Free(handle_id);
    FXL_CHECK(err == ZX_OK);

    obj->handle_id_ = TipcObject::kInvalidHandle;
  }
}

zx_status_t TipcObjectManager::GetObject(uint32_t handle_id,
                                         fbl::RefPtr<TipcObject>* obj_out) {
  fbl::AutoLock lock(&object_table_lock_);
  if (handle_id >= kMaxHandle) {
    return ZX_ERR_BAD_HANDLE;
  }

  if (!object_table_[handle_id]) {
    return ZX_ERR_BAD_HANDLE;
  }

  *obj_out = object_table_[handle_id];
  return ZX_OK;
}

zx_status_t TipcObjectManager::Wait(WaitResult* result, zx::time deadline) {
  return root_obj_set_->Wait(result, fbl::move(deadline));
}

}  // namespace trusty_ipc
