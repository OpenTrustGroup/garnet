// Copyright 2018 OpenTrustGroup. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trusty/linux_fake.h"

namespace trusty {

zx_status_t LinuxFake::CreateDriver(TipcDevice* device) {
  fbl::AllocChecker ac;
  auto driver = fbl::make_unique_checked<TipcDriverFake>(&ac, device, this);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  drivers_.push_back(fbl::move(driver), &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

zx_status_t LinuxFake::HandleResourceTable(resource_table* table) {
  uint8_t idx = 0;
  for (const auto& driver : drivers_) {
    auto descr = rsc_entry<tipc_vdev_descr>(table, idx++);
    zx_status_t status = driver->Probe(&descr->vdev);
    if (status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}

}  // namespace trusty
