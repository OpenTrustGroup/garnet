// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/gzos/ree_agent/ree_message_impl.h"

static zx_status_t get_smc_service_ctrl_channel(zx::channel* out_ch) {
  zx_handle_t request = zx_get_startup_handle(PA_HND(PA_USER0, 0));
  if (request == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Can not get ree_agent channel";
    return ZX_ERR_NO_RESOURCES;
  }

  out_ch->reset(request);
  return ZX_OK;
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  ree_agent::ReeMessageImpl ree_message_impl;

  zx::channel ch;
  zx_status_t status = get_smc_service_ctrl_channel(&ch);
  if (status != ZX_OK)
    return 1;

  ree_message_impl.Bind(std::move(ch));

  loop.Run();
  return 0;
}
