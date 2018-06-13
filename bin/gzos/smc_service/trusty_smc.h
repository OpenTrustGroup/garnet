// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "garnet/lib/trusty/virtio_device.h"

#include <ree_agent/cpp/fidl.h>

#include <lib/zx/channel.h>

namespace smc_service {

class TrustySmcEntity final : public SmcEntity {
 public:
  TrustySmcEntity(async_t* async, zx::channel ch, fbl::RefPtr<SharedMem> shm);
  ~TrustySmcEntity() {}

  zx_status_t Init() override;
  long InvokeSmcFunction(smc32_args_t* args) override;

 private:
  zx_status_t GetNsBuf(smc32_args_t* args, void** buf, size_t* size);
  zx_status_t InvokeNopFunction(smc32_args_t* args);

  async_t* async_;
  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::unique_ptr<trusty::VirtioBus> vbus_;
  ree_agent::ReeMessageSyncPtr ree_message_;
};

} // namespace smc_service
