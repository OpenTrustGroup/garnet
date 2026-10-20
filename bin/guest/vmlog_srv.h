// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "lib/fxl/logging.h"
#include "lib/fxl/logging_rate_limiter.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/files/unique_fd.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/vm_id.h"
#include "garnet/lib/vmlog_store/vmlog_store.h"
#include "garnet/public/lib/guest_allocator/cpp/guest_allocator.h"

#include <fbl/mutex.h>
#include <fbl/atomic.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>
#include <zircon/vmid.h>
#include <lib/zx/log.h>

#include <thread>

#include "tipc_vqueue_notifier.h"

/*
 * Ring buffer that supports one secure producer thread and one
 * linux side consumer thread.
 */
struct __attribute__((packed)) log_rb {
  volatile uint32_t alloc;
  volatile uint32_t put;
  uint32_t sz;
  volatile char data[];
};

class VmlogSrv {
 public:
  explicit VmlogSrv(fxl::UniqueFD &kGuestMemoryAllocatorFd, int32_t vmid, VmlogStore* log_store);
  ~VmlogSrv();
  zx_status_t Initialize(void);
  zx_status_t CreateVmlogSinkNodes(const machina::Guest& guest,
                                      GuestConfig& cfg);
  zx_status_t CreateAlpsVmlogSinkNodes(void* dtb, size_t dtb_size);
  zx_status_t Start(TipcVqueueNotifier* vqueue_notifier);
  void enable_vmlog(void);
  void disable_vmlog(void);
  int log_read_line(struct log_rb *log_ptr, uint32_t put, uint32_t get);
  enum {
    VMLOG_SOS_VMID = ZX_USER_VMID_SOS,
    VMLOG_ALPS_VMID = ZX_USER_VMID_ALPS,
    VMLOG_TBOX_VMID = ZX_USER_VMID_TBOX,
  };
  enum TrustyLogDumpState {
    TRUSTY_LOG_IS_DUMPING,
    TRUSTY_LOG_IS_DUMPED,
  };
  zx_status_t TrustyDumpLogs(void);
  void Shutdown(void);

 private:
  TipcVqueueNotifier* vqueue_notifier_;
  const fxl::UniqueFD &kGuestMemoryAllocatorFd;

  int32_t vmid_;
  uint64_t reserved_memory_pa_;
  uint64_t reserved_memory_size_;
  void StartLoopThread(TipcVqueueNotifier* vqueue_notifier) {
    vqueue_notifier_ = vqueue_notifier;
    vmlog_thrd_ = std::thread(&VmlogSrv::VmlogSinkLoop, this);
  }
  void VmlogSinkLoop(void);

  pthread_spinlock_t fast_lock;  // for handling logs in IRQ context
  fbl::Mutex lock;
  struct log_rb *log_;
  uint64_t rb_sz;
  uint32_t get_;
  enum { TRUSTY_LINE_BUFFER_SIZE = 200 };
  char line_buffer[TRUSTY_LINE_BUFFER_SIZE];
  fbl::atomic<bool> enabled;

  zx_handle_t vmo_handle_ = ZX_HANDLE_INVALID;
  std::thread vmlog_thrd_;
  fbl::atomic<bool> shutdown_;
  fbl::atomic<bool> Trusty_Log_Dump_State_{TRUSTY_LOG_IS_DUMPED};
  VmlogStore* log_store_;
};
