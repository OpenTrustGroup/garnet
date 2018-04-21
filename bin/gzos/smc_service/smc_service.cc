// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <zx/vmo.h>

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "garnet/bin/gzos/smc_service/trusty_smc.h"

namespace smc_service {

static fbl::Mutex instance_lock;
static fbl::unique_ptr<SmcService> service_instance;

SmcService* SmcService::GetInstance() {
  fbl::AutoLock al(&instance_lock);

  if (service_instance != nullptr)
    return service_instance.get();

  zx_handle_t smc_handle = ZX_HANDLE_INVALID;
  zx_handle_t vmo_handle = ZX_HANDLE_INVALID;
  zx_info_smc_t smc_info = {};
  zx_status_t status = zx_smc_create(0, &smc_info, &smc_handle, &vmo_handle);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create smc kernel object, status=" << status;
    return nullptr;
  }

  auto close_handle = fbl::MakeAutoCall([&](){ zx_handle_close(smc_handle); });

  fbl::RefPtr<SharedMem> shared_mem;
  zx::vmo shm_vmo(vmo_handle);
  zx_info_ns_shm_t shm_info = smc_info.ns_shm;
  status = SharedMem::Create(fbl::move(shm_vmo), shm_info, &shared_mem);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create shared memory object, status=" << status;
    return nullptr;
  }

  fbl::AllocChecker ac;
  service_instance = fbl::make_unique_checked<SmcService>(&ac,
      smc_handle, fbl::move(shared_mem));
  if (!ac.check()) {
    FXL_LOG(ERROR) << "Failed to create SmcService object due to not enough memory";
    return nullptr;
  }

  close_handle.cancel();

  return service_instance.get();
}

void SmcService::AddSmcEntity(uint32_t entity_nr, SmcEntity* e) {
  fbl::AutoLock al(&lock_);
  if (smc_entities_[entity_nr] == nullptr && e != nullptr) {
    if (e->Init() == ZX_OK)
      smc_entities_[entity_nr].reset(e);
  }
}

SmcEntity* SmcService::GetSmcEntity(uint32_t entity_nr) {
  fbl::AutoLock al(&lock_);
  return (entity_nr < SMC_NUM_ENTITIES) ? smc_entities_[entity_nr].get() : nullptr;
};

zx_status_t SmcService::Start(async_t* async) {
  return WaitOnSmc(async);
}

zx_status_t SmcService::WaitOnSmc(async_t* async) {
  smc_wait_.set_object(smc_handle_);
  smc_wait_.set_trigger(ZX_SMC_READABLE);
  return smc_wait_.Begin(async);
}

void SmcService::OnSmcReady(async_t* async,
                            async::WaitBase* wait,
                            zx_status_t status,
                            const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    OnSmcClosed(status, "async wait on smc");
    return;
  }

  smc32_args_t smc_args = {};
  status = zx_smc_read(smc_handle_, &smc_args);
  if (status != ZX_OK) {
    OnSmcClosed(status, "zx_smc_read");
    return;
  }

  long result = SM_ERR_UNDEFINED_SMC;
  SmcEntity* entity = GetSmcEntity(SMC_ENTITY(smc_args.smc_nr));
  if (entity != nullptr)
    result = entity->InvokeSmcFunction(&smc_args);

  status = zx_smc_set_result(smc_handle_, result);
  if(status != ZX_OK) {
    OnSmcClosed(status, "zx_smc_set_result");
    return;
  }

  /* wait for next smc request */
  status = wait->Begin(async);
  if (status != ZX_OK) {
    OnSmcClosed(status, "async wait on smc");
  }
  return;
}

void SmcService::OnSmcClosed(zx_status_t status, const char* action) {
  smc_wait_.Cancel();
  FXL_LOG(ERROR) << "Smc handling failed during step '" << action << "' (" << status
                 << ")";
}

} // namespace smc_service

