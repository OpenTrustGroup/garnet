// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>

#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc_service.h>

#include <threads.h>
#include <unordered_map>

#include "garnet/lib/gzos/trusty_virtio/shared_mem.h"
#include "lib/fxl/logging.h"

namespace smc_service {

static constexpr const uint32_t kMaxCpuNumbers = 4;

using trusty_virtio::SharedMem;
using SmcFunction = fbl::Function<long(smc32_args_t* args)>;

class SmcEntity {
 public:
  SmcEntity() {}

  virtual ~SmcEntity() {}

  virtual zx_status_t Init() { return ZX_ERR_NOT_SUPPORTED; };
  virtual long InvokeSmcFunction(smc32_args_t* args) {
    return SM_ERR_UNDEFINED_SMC;
  };
};

class SmcService {
 public:
  static SmcService* GetInstance();

  SmcService()
      : smc_handle_(ZX_HANDLE_INVALID), shared_mem_(nullptr),
        nop_threads_stop_(true) {}

  ~SmcService() { Stop(); }

  zx_status_t AddSmcEntity(uint32_t entity_nr, SmcEntity* e);
  zx_status_t Start(async_t* async);
  void Stop();
  fbl::RefPtr<SharedMem> GetSharedMem() {
    fbl::AutoLock lock(&lock_);
    return shared_mem_;
  };
  zx_handle_t GetHandle() {
    fbl::AutoLock lock(&lock_);
    return smc_handle_;
  };

  bool nop_threads_stop() { return nop_threads_stop_.load(); }

 private:
  struct ThreadArgs;

  SmcEntity* GetSmcEntity(uint32_t entity_nr);
  zx_status_t InitSmcEntities();
  zx_status_t WaitOnSmc(async_t* async);
  void OnSmcReady(async_t* async, async::WaitBase* wait, zx_status_t status,
                  const zx_packet_signal_t* signal);
  void OnSmcClosed(zx_status_t status, const char* action);
  zx_status_t CreateSmcKernelObject();
  zx_status_t CreateNopThreads();
  void JoinNopThreads();

  mutable fbl::Mutex lock_;
  zx_handle_t smc_handle_ __TA_GUARDED(lock_);
  async::WaitMethod<SmcService, &SmcService::OnSmcReady> smc_wait_{this};
  fbl::RefPtr<SharedMem> shared_mem_ __TA_GUARDED(lock_);
  fbl::unique_ptr<SmcEntity> smc_entities_[SMC_NUM_ENTITIES] __TA_GUARDED(lock_);
  fbl::Mutex nop_threads_lock_;
  thrd_t nop_threads_[kMaxCpuNumbers] __TA_GUARDED(nop_threads_lock_);
  fbl::atomic<bool> nop_threads_stop_;
};

}  // namespace smc_service
