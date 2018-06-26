// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>

#include "lib/fxl/logging.h"

namespace ree_agent {

template<uint32_t ID_MAX>
class IdAllocator {
 public:
  IdAllocator() { id_bitmap_.Reset(ID_MAX); }

  zx_status_t Alloc(uint32_t* id) {
    FXL_DCHECK(id);
    size_t first_unset;
    bool all_set = id_bitmap_.Get(0, ID_MAX, &first_unset);

    if (all_set) {
      return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = id_bitmap_.SetOne(first_unset);
    if (status != ZX_OK) {
      return status;
    }

    *id = first_unset;
    return ZX_OK;
  }

  zx_status_t Free(uint32_t id) {
    if (!InUse(id)) {
      return ZX_ERR_INVALID_ARGS;
    }

    return id_bitmap_.ClearOne(id);
  }

  bool InUse(uint32_t id) { return id_bitmap_.GetOne(id); }

 private:
  bitmap::RawBitmapGeneric<bitmap::FixedStorage<ID_MAX>> id_bitmap_;
};

}  // namespace ree_agent
