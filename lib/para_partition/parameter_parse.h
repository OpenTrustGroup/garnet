// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "lib/fxl/logging.h"
#include <lib/fxl/strings/string_printf.h>
#include "lib/fxl/synchronization/thread_annotations.h"

#include <block-client/client.h>
#include <fbl/unique_fd.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/mapped-vmo.h>
#include <fbl/atomic.h>
#include <zircon/types.h>
#include <lib/zx/log.h>

#include <thread>
#include <mutex>
#include <cstdio>
#include <unordered_map>

class ParaParse {
 public:
  explicit ParaParse();
  ~ParaParse();

  char* sysenv_get(const char*env);
  zx_status_t Initialize(void);
  void DeInit(void);

 private:
  zx_status_t DumpParaFromBlockDevice(const fbl::unique_fd& fd);
  size_t ParseAndInsertEnvBlock(const uint8_t* block_start, const uint8_t* block_end);
  size_t ParsePara(MappedVmo* vmo);
  zx_status_t register_fast_block_io(const fbl::unique_fd& fd,
                                      zx_handle_t vmo, txnid_t* txnid_out,
                                      vmoid_t* vmoid_out, fifo_client_t** client_out);

  std::mutex sys_env_mutex_;
  std::unordered_map<std::string, std::string> sys_env_map_ __TA_GUARDED(sys_env_mutex_);
  fbl::unique_ptr<MappedVmo> Para_Vmo_;
  fbl::atomic<bool> sys_env_init_{false};
};
