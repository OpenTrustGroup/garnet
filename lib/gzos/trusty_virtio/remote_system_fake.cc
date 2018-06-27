// Copyright 2018 OpenTrustGroup. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/gzos/trusty_virtio/remote_system_fake.h"

namespace trusty_virtio {

TrustyVirtioDeviceFrontendFake* RemoteSystemFake::GetFrontend(
    uint32_t notify_id) {
  for (const auto& frontend : frontends_) {
    if (frontend->notify_id() == notify_id)
      return frontend.get();
  }

  return nullptr;
}

zx_status_t RemoteSystemFake::HandleResourceTable(
    resource_table* table, const fbl::Vector<fbl::RefPtr<VirtioDevice>>& devs) {
  if (table->num != devs.size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (uint32_t i = 0; i < table->num; i++) {
    auto descr = rsc_entry<trusty_vdev_descr>(table, i);
    auto dev = static_cast<TrustyVirtioDevice*>(devs[i].get());

    if (descr->hdr.type != RSC_VDEV) {
      return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    auto frontend = fbl::make_unique_checked<TrustyVirtioDeviceFrontendFake>(
        &ac, dev, this);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = frontend->Init(&descr->vdev);
    if (status != ZX_OK) {
      return status;
    }

    frontends_.push_back(fbl::move(frontend), &ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  return ZX_OK;
}

}  // namespace trusty_virtio
