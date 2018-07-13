// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/gzos/ree_agent/ree_message_impl.h"
#include "garnet/bin/gzos/ree_agent/ta_service.h"
#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "garnet/bin/gzos/smc_service/trusty_smc.h"

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

static zx_status_t get_startup_channels(zx::channel* ree_agent_cli,
                                        zx::channel* ree_agent_srv,
                                        zx::channel* appmgr_out) {
  zx_handle_t appmgr_svc = zx_take_startup_handle(PA_HND(PA_USER0, 0));
  if (appmgr_svc == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Can not get startup handles";
    return ZX_ERR_NO_RESOURCES;
  }

  zx::channel h0, h1;

  zx_status_t status = zx::channel::create(0, &h0, &h1);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create new channel";
    return status;
  }

  *ree_agent_cli = fbl::move(h0);
  *ree_agent_srv = fbl::move(h1);
  appmgr_out->reset(appmgr_svc);
  return ZX_OK;
}


int main(int argc, const char** argv) {
  zx::channel ree_agent_cli;
  zx::channel ree_agent_srv;
  zx::channel appmgr_svc;
  zx_status_t status = get_startup_channels(&ree_agent_cli, &ree_agent_srv,
                                            &appmgr_svc);
  if (status != ZX_OK)
    return 1;

  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  // Start ree agent
  ree_agent::TaServiceProvider ta_service_provider(std::move(appmgr_svc));
  ree_agent::ReeMessageImpl ree_message_impl(ta_service_provider);
  ree_message_impl.Bind(std::move(ree_agent_srv));

  loop.StartThread();

  // Start smc service
  smc_service::SmcService* s = smc_service::SmcService::GetInstance();
  if (s == nullptr)
    return 1;

  status = s->AddSmcEntity(
      SMC_ENTITY_TRUSTED_OS,
      new smc_service::TrustySmcEntity(loop.async(), fbl::move(ree_agent_cli)));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add Trusty smc entity, status=" << status;
    return 1;
  }

  s->Start(loop.async());

  loop.Run();
  return 0;
}
