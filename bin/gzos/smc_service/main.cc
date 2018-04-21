// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "garnet/bin/gzos/smc_service/trusty_smc.h"

using smc_service::SmcService;
using smc_service::TrustySmcEntity;

// TODO(james): get ree agent control channel from devmgr
static zx_status_t get_ree_agent_ctrl_channel(zx_handle_t* out_ch) {
  zx_handle_t h1, h2;

  zx_status_t status = zx_channel_create(0, &h1, &h2);
  if (status != ZX_OK) {
    return status;
  }

  *out_ch = h1;
  return ZX_OK;
}

int main(int argc, const char** argv) {

  SmcService* s = SmcService::GetInstance();
  if (s == nullptr) {
    FXL_LOG(ERROR) << "Failed to get smc service instance";
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  zx_handle_t ree_agent_ctrl;
  zx_status_t status = get_ree_agent_ctrl_channel(&ree_agent_ctrl);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to get ree agent control channel";
    return 1;
  }

  s->AddSmcEntity(
      SMC_ENTITY_TRUSTED_OS,
      new TrustySmcEntity(loop.async(), ree_agent_ctrl, s->GetSharedMem()));

  s->Start(loop.async());

  loop.Run();

  FXL_LOG(INFO) << "smc service finished";

  return 0;
}
