// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/gzos/ree_agent/ree_message_impl.h"
#include "garnet/bin/gzos/ree_agent/ta_service.h"

#include "lib/svc/cpp/services.h"

namespace ree_agent {

class TaServiceProvider : public TaServices {
 public:
  TaServiceProvider(zx::channel channel) {
    service_provider_.Bind(std::move(channel));
  }

  void ConnectToService(zx::channel request, const std::string& service_name) {
    service_provider_.ConnectToService(std::move(request), service_name);
  }

 private:
  fuchsia::sys::Services service_provider_;
};

}  // namespace ree_agent

static zx_status_t get_startup_channels(zx::channel* ree_agent_out,
                                        zx::channel* appmgr_out) {
  zx_handle_t h0 = zx_take_startup_handle(PA_HND(PA_USER0, 0));
  zx_handle_t h1 = zx_take_startup_handle(PA_HND(PA_USER0, 1));

  if (h0 == ZX_HANDLE_INVALID || h1 == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Can not get startup handles";
    return ZX_ERR_NO_RESOURCES;
  }

  ree_agent_out->reset(h0);
  appmgr_out->reset(h1);
  return ZX_OK;
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  zx::channel ree_agent_srv;
  zx::channel appmgr_cli;
  zx_status_t status = get_startup_channels(&ree_agent_srv, &appmgr_cli);
  if (status != ZX_OK)
    return 1;

  ree_agent::TaServiceProvider ta_service_provider(std::move(appmgr_cli));

  ree_agent::ReeMessageImpl ree_message_impl(ta_service_provider);
  ree_message_impl.Bind(std::move(ree_agent_srv));

  loop.Run();
  return 0;
}
