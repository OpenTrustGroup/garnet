// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
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

  service_instance = fbl::make_unique<SmcService>();
  if (service_instance == nullptr) {
    FXL_LOG(ERROR) << "Failed to create SmcService object";
    return nullptr;
  }

  return service_instance.get();
}

zx_status_t SmcService::AddSmcEntity(uint32_t entity_nr, SmcEntity* e) {
  fbl::AutoLock al(&lock_);

  if (smc_entities_[entity_nr] != nullptr)
    return ZX_ERR_ALREADY_EXISTS;

  if (e == nullptr || (entity_nr >= SMC_NUM_ENTITIES))
    return ZX_ERR_INVALID_ARGS;

  smc_entities_[entity_nr].reset(e);
  return ZX_OK;
}

SmcEntity* SmcService::GetSmcEntity(uint32_t entity_nr) {
  fbl::AutoLock al(&lock_);
  return (entity_nr < SMC_NUM_ENTITIES) ? smc_entities_[entity_nr].get()
                                        : nullptr;
};

zx_status_t SmcService::InitSmcEntities() {
  uint32_t i;
  for (i = 0; i < SMC_NUM_ENTITIES; i++) {
    SmcEntity* e = GetSmcEntity(i);

    if (e == nullptr) {
      continue;
    }

    zx_status_t status = e->Init();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to init smc entity: " << i
                     << " status:" << status;
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t SmcService::CreateSmcKernelObject() {
  fbl::AutoLock al(&lock_);

  zx_handle_t smc_handle = ZX_HANDLE_INVALID;
  zx_handle_t vmo_handle = ZX_HANDLE_INVALID;
  zx_info_smc_t smc_info = {};

  zx_status_t status = zx_smc_create(0, &smc_info, &smc_handle, &vmo_handle);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create smc kernel object, status:" << status;
    return status;
  }

  auto close_handle = fbl::MakeAutoCall([&]() { zx_handle_close(smc_handle); });

  fbl::RefPtr<SharedMem> shared_mem;
  zx::vmo shm_vmo(vmo_handle);
  zx_info_ns_shm_t shm_info = smc_info.ns_shm;
  status = SharedMem::Create(fbl::move(shm_vmo), shm_info, &shared_mem);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create shared memory object, status:"
                   << status;
    return status;
  }

  smc_handle_ = smc_handle;
  shared_mem_ = fbl::move(shared_mem);

  close_handle.cancel();
  return ZX_OK;
}

struct SmcService::ThreadArgs {
  SmcService* smc_service;
  uint32_t cpu_num;
};

zx_status_t SmcService::CreateNopThreads() {
  FXL_DCHECK(nop_threads_should_stop());

  fbl::AutoLock lock(&nop_threads_lock_);
  nop_threads_stop_.store(false);

  uint32_t cpu;
  for (cpu = 0; cpu < kMaxCpuNumbers; cpu++) {
    auto thread_entry = [](void* arg) {
      ThreadArgs* thrd_args = reinterpret_cast<ThreadArgs*>(arg);
      smc32_args_t smc_args{};
      SmcService* smc_svc = thrd_args->smc_service;
      uint32_t cpu = thrd_args->cpu_num;

      while (!smc_svc->nop_threads_should_stop()) {
        zx_status_t status =
            zx_smc_read_nop(smc_svc->GetHandle(), cpu, &smc_args);
        if (status != ZX_OK) {
          FXL_VLOG(1) << "Read nop request error, cpu:" << cpu
                      << " status:" << status;
          continue;
        }

        uint32_t entity_num = SMC_ENTITY(smc_args.params[0]);
        SmcEntity* entity = smc_svc->GetSmcEntity(entity_num);

        if (entity != nullptr) {
          entity->InvokeSmcFunction(&smc_args);
        }
      }

      FXL_VLOG(1) << "Nop thread stopped, cpu: " << cpu;
      return ZX_OK;
    };

    ThreadArgs args{this, cpu};
    fbl::StringBuffer<ZX_MAX_NAME_LEN> name_buffer;
    name_buffer.AppendPrintf("nop-thrd-%u", cpu);

    int ret = thrd_create_with_name(&nop_threads_[cpu], thread_entry, &args,
                                    name_buffer.c_str());
    if (ret != thrd_success) {
      FXL_LOG(ERROR) << "Failed to create nop thread for cpu: " << cpu;
      JoinNopThreads();
      return ZX_ERR_INTERNAL;
    }
  }

  return ZX_OK;
}

zx_status_t SmcService::Start(async_t* async) {
  auto stop_service = fbl::MakeAutoCall([&]() { Stop(); });

  zx_status_t status = CreateSmcKernelObject();
  if (status != ZX_OK) {
    return status;
  }

  status = CreateNopThreads();
  if (status != ZX_OK) {
    return status;
  }

  status = InitSmcEntities();
  if (status != ZX_OK) {
    return status;
  }

  status = WaitOnSmc(async);
  if (status != ZX_OK) {
    return status;
  }

  stop_service.cancel();
  return ZX_OK;
}

void SmcService::Stop() {
  JoinNopThreads();

  fbl::AutoLock al(&lock_);
  uint32_t i;
  for (i = 0; i < SMC_NUM_ENTITIES; i++) {
    smc_entities_[i].reset();
  }

  zx_handle_close(smc_handle_);
}

void SmcService::JoinNopThreads() {
  fbl::AutoLock lock(&nop_threads_lock_);

  if (nop_threads_stop_.exchange(true)) {
    return;
  }

  zx_smc_cancel_read_nop(GetHandle());

  uint32_t i;
  for (i = 0; i < kMaxCpuNumbers; i++) {
    if (nop_threads_[i]) {
      thrd_join(nop_threads_[i], nullptr);
    }
  }
}

zx_status_t SmcService::WaitOnSmc(async_t* async) {
  smc_wait_.set_object(GetHandle());
  smc_wait_.set_trigger(ZX_SMC_READABLE);
  return smc_wait_.Begin(async);
}

void SmcService::OnSmcReady(async_t* async, async::WaitBase* wait,
                            zx_status_t status,
                            const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    OnSmcClosed(status, "async wait on smc");
    return;
  }

  smc32_args_t smc_args = {};
  status = zx_smc_read(GetHandle(), &smc_args);
  if (status != ZX_OK) {
    OnSmcClosed(status, "zx_smc_read");
    return;
  }

  long result = SM_ERR_UNDEFINED_SMC;
  uint32_t entity_id = SMC_ENTITY(smc_args.smc_nr);

  SmcEntity* entity = GetSmcEntity(entity_id);

  if (entity != nullptr)
    result = entity->InvokeSmcFunction(&smc_args);

  status = zx_smc_set_result(GetHandle(), result);
  if (status != ZX_OK) {
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
  FXL_LOG(ERROR) << "Smc handling failed during step '" << action << "'"
                 << " status: " << status;
}

}  // namespace smc_service
