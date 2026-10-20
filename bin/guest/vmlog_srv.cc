// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <uapi/err.h>

#include <lib/zx/vmo.h>
#include <lib/zx/resource.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include "vmlog_srv.h"
#include <libfdt.h>
#include "garnet/lib/machina/fdt_utils.h"

namespace {
static constexpr char kResourcePath[] = "/dev/misc/sysinfo";

#define check_status(ret, property_name)                     \
  {                                                          \
    if ((ret) < 0) {                                         \
      FXL_LOG(ERROR) << "Device tree operation failed at \"" \
                     << (property_name) << "\", "            \
                     << "errot code is " << (ret);           \
      return ZX_ERR_BAD_STATE;                               \
    }                                                        \
  }
};  // namespace

static zx_status_t vmlog_get_root_resource(zx::resource* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  resource->reset(rsc_handle);
  close(fd);
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

static bool is_power_of_2(uint64_t n) {
  return (n != 0 && ((n & (n - 1)) == 0));
}

static inline uint32_t u32_add_overflow(uint32_t a, uint32_t b) {
  uint32_t d;

  if (__builtin_add_overflow(a, b, &d)) {
    /*
     * silence the overflow,
     * what matters in the log buffer context
     * is the casted addition
     */
  }
  return d;
}

static inline uint32_t u32_sub_overflow(uint32_t a, uint32_t b) {
  uint32_t d;

  if (__builtin_sub_overflow(a, b, &d)) {
    /*
     * silence the overflow,
     * what matters in the log buffer context
     * is the casted substraction
     */
  }
  return d;
}

void VmlogSrv::enable_vmlog(void) {
  get_ = 0;
  memset((void*)log_, 0, sizeof(struct log_rb));
  __sync_synchronize();
  enabled.store(true);
}

void VmlogSrv::disable_vmlog(void) {
  enabled.store(false);
}

int VmlogSrv::log_read_line(struct log_rb *log_ptr, uint32_t put, uint32_t get) {
  struct log_rb *log = log_ptr;
  int i;
  char c = '\0';
  uint32_t aval_len = u32_sub_overflow(put, get);
  uint32_t line_buffer_len = sizeof(line_buffer) - 1;
  size_t max_to_read = std::min(aval_len, line_buffer_len);
  size_t mask = log->sz - 1;

  for (i = 0; i < max_to_read && c != '\n';) {
    c = log->data[get & mask];
    line_buffer[i++] = c;
    get = u32_add_overflow(get, 1);
  }
  line_buffer[i] = '\0';

  return i;
}

zx_status_t VmlogSrv::TrustyDumpLogs(void) {
  zx_status_t ret = ZX_OK;
  uint32_t read_idx, write_idx, alloc;
  int read_chars;

  if (unlikely(log_->sz == 0)) {
    FXL_LOG_ERR_RATELIMITED() << "log buffer not initialized yet, "
                      << fxl::StringPrintf("vmid: %d",
                                           zx_log_guest_vmid(vmid_));
    return ZX_ERR_INVALID_ARGS;
  }

  if (is_power_of_2(log_->sz) == false) {
    FXL_LOG_ERR_RATELIMITED() << "log buffer size is not power of 2: " << log_->sz;
    return ZX_ERR_INVALID_ARGS;
  }

  /*
  * For this ring buffer, at any given point, alloc >= put >= get.
  * The producer side of the buffer is not locked, so the put and alloc
  * pointers must be read in a defined order (put before alloc) so
  * that the above condition is maintained. A read barrier is needed
  * to make sure the hardware and compiler keep the reads ordered.
  */
  read_idx = get_;
  while ((write_idx = log_->put) != read_idx) {
    Trusty_Log_Dump_State_.store(TRUSTY_LOG_IS_DUMPING);
    /* Make sure that the read of put occurs before the read of log data */
    __sync_synchronize();

    /* Read a line from the log */
    read_chars = log_read_line(log_, write_idx, read_idx);

    /* Force the loads from log_read_line to complete. */
    __sync_synchronize();
    alloc = log_->alloc;

    /*
     * Discard the line that was just read if the data could
     * have been corrupted by the producer.
     */
    if (u32_sub_overflow(alloc, read_idx) > log_->sz) {
      FXL_LOG_WARN_RATELIMITED() << "log overflow,"
                       << fxl::StringPrintf("vmid: %d",
                                            zx_log_guest_vmid(vmid_));
      read_idx = u32_sub_overflow(alloc, log_->sz);
      continue;
    }

    log_store_->WriteLog(line_buffer, read_chars);

    /* compute next line index */
    read_idx = u32_add_overflow(read_idx, read_chars);
  }
  get_ = read_idx;
  Trusty_Log_Dump_State_.store(TRUSTY_LOG_IS_DUMPED);
  return ret;
}

void VmlogSrv::VmlogSinkLoop(void) {
#if VMLOG_SINKING_USE_NOTIFY_MODE
  zx_status_t ret = 0;
  zx_handle_t notifier = ZX_HANDLE_INVALID;
  zx_signals_t observed_signals = 0;
  notifier = vqueue_notifier_->getZxHandle();
  FXL_LOG(INFO) << "vqueue_notifier_,"
                << fxl::StringPrintf("0x%x", notifier);
#endif

  while (true) {
    if (shutdown_.load()) {
      FXL_LOG(INFO) << "VmlogSinkLoop shutdown";
      return;
    }
#if VMLOG_SINKING_USE_NOTIFY_MODE
    ret = zx_object_wait_one(notifier,
                      ZX_USER_SIGNAL_0, ZX_TIME_INFINITE,
                      &observed_signals);
    if (ret != ZX_OK) {
      return;
    }
    if(observed_signals & ZX_USER_SIGNAL_0) {
      ret = zx_object_signal(notifier, ZX_USER_SIGNAL_0, 0u);
      if (enabled.load()) {
        if (Trusty_Log_Dump_State_.load() == TRUSTY_LOG_IS_DUMPING) {
          continue;
        }
        pthread_spin_lock(&fast_lock);
        TrustyDumpLogs();
        pthread_spin_unlock(&fast_lock);
      }
    }
#else
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    if (enabled.load()) {
        if (Trusty_Log_Dump_State_.load() == TRUSTY_LOG_IS_DUMPING) {
          continue;
        }
        pthread_spin_lock(&fast_lock);
        TrustyDumpLogs();
        pthread_spin_unlock(&fast_lock);
    }
#endif
  }
}

zx_status_t VmlogSrv::Start(TipcVqueueNotifier* vqueue_notifier) {
  zx_status_t ret = ZX_OK;
  StartLoopThread(vqueue_notifier);
  enable_vmlog();
  FXL_LOG(INFO) << fxl::StringPrintf("StartLoopThread success");
  return ret;
}

zx_status_t VmlogSrv::CreateVmlogSinkNodes(const machina::Guest& guest,
                                      GuestConfig& cfg) {
  zx_status_t ret = ZX_OK;
  char dtb_node_path[256] = {};
  auto dtb_spec = cfg.dtb();
  uint64_t phys_base = cfg.phys_base();
  size_t ram = guest.phys_mem().size();
  if (dtb_spec.size == 0 || dtb_spec.base < phys_base) {
    FXL_LOG(ERROR) << "vmlog: DTB GPA 0x" << std::hex << dtb_spec.base
                   << " size 0x" << dtb_spec.size
                   << " outside guest RAM phys_base=0x" << phys_base
                   << " size=0x" << ram;
    return ZX_ERR_OUT_OF_RANGE;
  }
  uintptr_t dtb_offset = dtb_spec.base - phys_base;
  size_t dtb_size = dtb_spec.size;
  if (dtb_offset + dtb_size < dtb_offset || dtb_offset + dtb_size > ram) {
    FXL_LOG(ERROR) << "vmlog: DTB GPA 0x" << std::hex << dtb_spec.base
                   << " size 0x" << dtb_size
                   << " outside guest RAM phys_base=0x" << phys_base
                   << " size=0x" << ram;
    return ZX_ERR_OUT_OF_RANGE;
  }

  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

  ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (machina::IsSosVmid(vmid_)) {
    snprintf(dtb_node_path, sizeof(dtb_node_path) - 1, "/nebula_vmlog_sos_sinking");
  } else if (machina::IsTboxVmid(vmid_)) {
    snprintf(dtb_node_path, sizeof(dtb_node_path) - 1, "/nebula_vmlog_tbox_sinking");
  } else {
    FXL_LOG(ERROR) << "Incompatible vmid: "
                   << zx_log_guest_vmid(vmid_);
    return ZX_ERR_INVALID_ARGS;
  }

  int vmlog_offset = fdt_add_subnode(dtb, 0, dtb_node_path);
  check_status(vmlog_offset, "vmlog");

  ret = fdt_setprop_string(dtb, vmlog_offset, "compatible", "grt,vmlog-sink");
  check_status(ret, "compatible");

  ret = fdt_setprop_u32(dtb, vmlog_offset, "notify-vmid", vmid_);
  check_status(ret, "notify-vmid");

  uint64_t mmio_base = reserved_memory_pa_;
  uint64_t mmio_size = reserved_memory_size_;
  ret = fdt_setprop_cells_u64(dtb, vmlog_offset, "reg", 4, mmio_base, mmio_size, 0, 0);
  check_status(ret, "reg");

  ret = fdt_setprop_cell(dtb, vmlog_offset, "offset", 0x0);
  check_status(ret, "offset");

  ret = fdt_setprop_string(dtb, vmlog_offset, "status", "okay");
  check_status(ret, "status");

  fdt_pack(dtb);
  FXL_LOG(INFO) << fxl::StringPrintf("add %s dtb success", dtb_node_path);
  return ret;
}

zx_status_t VmlogSrv::CreateAlpsVmlogSinkNodes(void* dtb, size_t dtb_size) {
  zx_status_t ret = ZX_OK;
  char dtb_node_path[256] = {};
  ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (machina::IsAlpsVmid(vmid_)) {
    snprintf(dtb_node_path, sizeof(dtb_node_path) - 1, "/nebula_vmlog_alps_sinking");
  } else {
    FXL_LOG(ERROR) << "Incompatible vmid: "
                   << zx_log_guest_vmid(vmid_);
    return ZX_ERR_INVALID_ARGS;
  }

  int vmlog_offset = fdt_add_subnode(dtb, 0, dtb_node_path);
  check_status(vmlog_offset, "vmlog");

  ret = fdt_setprop_string(dtb, vmlog_offset, "compatible", "grt,vmlog-sink");
  check_status(ret, "compatible");

  ret = fdt_setprop_u32(dtb, vmlog_offset, "notify-vmid", vmid_);
  check_status(ret, "notify-vmid");

  uint64_t mmio_base = reserved_memory_pa_;
  uint64_t mmio_size = reserved_memory_size_;
  ret = fdt_setprop_cells_u64(dtb, vmlog_offset, "reg", 4, mmio_base, mmio_size, 0, 0);
  check_status(ret, "reg");

  ret = fdt_setprop_cell(dtb, vmlog_offset, "offset", 0x0);
  check_status(ret, "offset");

  ret = fdt_setprop_string(dtb, vmlog_offset, "status", "okay");
  check_status(ret, "status");

  fdt_pack(dtb);
  FXL_LOG(INFO) << fxl::StringPrintf("add %s dtb success", dtb_node_path);
  return ret;
}

zx_status_t VmlogSrv::Initialize(void) {
  zx::vmo vmlog_shm_vmo;
  zx_paddr_t mapped_addr;
  zx_status_t ret = ZX_OK;
  zx::resource root_resource;

  mem_req_t vmlog_req = {};
  mem_resp_t vmlog_resp = {};
  std::string vmlog_sos_resv_mem("vmlog_sink_sos");
  std::string vmlog_tbox_resv_mem("vmlog_sink_tbox");
  std::string vmlog_alps_resv_mem("vmlog_sink_alps");

  if (machina::IsSosVmid(vmid_)) {
    snprintf(vmlog_req.search_string, sizeof(vmlog_req.search_string),
                                          "%s", vmlog_sos_resv_mem.c_str());
  } else if (machina::IsTboxVmid(vmid_)) {
    snprintf(vmlog_req.search_string, sizeof(vmlog_req.search_string),
                                          "%s", vmlog_tbox_resv_mem.c_str());
  } else if (machina::IsAlpsVmid(vmid_)) {
    snprintf(vmlog_req.search_string, sizeof(vmlog_req.search_string),
                                          "%s", vmlog_alps_resv_mem.c_str());
  } else {
    FXL_LOG(ERROR) << "Incompatible vmid: "
                   << zx_log_guest_vmid(vmid_);
    return ZX_ERR_INVALID_ARGS;
  }

  FXL_LOG(INFO) << "IOCTL_GET_RESERVED_MEMORY for "<< vmlog_req.search_string;
  ret = ioctl_get_reserved_memory(kGuestMemoryAllocatorFd.get(), &vmlog_req, &vmlog_resp);
  if (ret <= 0) {
    FXL_LOG(ERROR) << "vmlog reserved memory not found: " << ret;
    return ret;
  }

  FXL_LOG(INFO) << "ioctl_get_reserved_memory ret= "<< ret;
  FXL_LOG(INFO) << "vmlog reserved memory base:"
                << fxl::StringPrintf("0x%lx", vmlog_resp.addr);
  FXL_LOG(INFO) << "vmlog reserved memory size:"
                << fxl::StringPrintf("0x%lx", vmlog_resp.size);

  ret = vmlog_get_root_resource(&root_resource);
  FXL_CHECK(ret == ZX_OK);

  ret = zx_vmo_create_physical(root_resource.get(), vmlog_resp.addr, vmlog_resp.size,
                                    vmlog_shm_vmo.reset_and_get_address());
  FXL_CHECK(ret == ZX_OK);

  ret = vmlog_shm_vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
  FXL_CHECK(ret == ZX_OK);

  ret = zx_vmar_map(zx_vmar_root_self(),
                        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                        0, vmlog_shm_vmo.get(), 0, vmlog_resp.size, &mapped_addr);
  FXL_CHECK(ret == ZX_OK);

  log_ = reinterpret_cast<struct log_rb *>(mapped_addr);
  reserved_memory_pa_ = vmlog_resp.addr;
  reserved_memory_size_ = vmlog_resp.size;
  FXL_LOG(INFO) << "vmlog reserved memory zx_vmar_map vaddr:"
                << fxl::StringPrintf("%p", log_);

  pthread_spin_init(&fast_lock, PTHREAD_PROCESS_PRIVATE);
  memset((void*)log_, 0, sizeof(struct log_rb));
  rb_sz = reserved_memory_size_;
  vmo_handle_ = vmlog_shm_vmo.get();
  memset(line_buffer, 0, TRUSTY_LINE_BUFFER_SIZE);
  FXL_LOG(INFO) << "vmlog success";

  return ZX_OK;
}

VmlogSrv::VmlogSrv(fxl::UniqueFD &kGuestMemoryAllocatorFd, int32_t vmid, VmlogStore* log_store)
    : kGuestMemoryAllocatorFd(kGuestMemoryAllocatorFd),
      vmid_(vmid), shutdown_(false), log_store_(log_store) {
  FXL_LOG(INFO) << "VmlogSrv Constructor vmid:"
                << zx_log_guest_vmid(vmid_);
}

VmlogSrv::~VmlogSrv() {
  Shutdown();
}

void VmlogSrv::Shutdown(void) {
  if (shutdown_.exchange(true)) {
    return;
  }

  FXL_LOG(INFO) << "VmlogSrv shutdown started";
  disable_vmlog();

  if (vqueue_notifier_ != nullptr) {
    FXL_LOG(INFO) << "Signaling vqueue notifier to stop VmlogSinkLoop thread";
    auto notifier = vqueue_notifier_->getZxHandle();
    zx_object_signal(notifier, 0u, ZX_USER_SIGNAL_0);

    if (vmlog_thrd_.joinable()) {
      vmlog_thrd_.join();
    }
  }

  int result = pthread_spin_destroy(&fast_lock);
  if (result != 0) {
    FXL_LOG(ERROR) << "Failed to destroy spinlock: " << result;
  }

  if (log_) {
    zx_vmar_unmap(zx_vmar_root_self(), reinterpret_cast<zx_vaddr_t>(log_), rb_sz);
    log_ = nullptr;
  }

  memset(line_buffer, 0, TRUSTY_LINE_BUFFER_SIZE);

  if (vmo_handle_ != ZX_HANDLE_INVALID) {
    zx_handle_close(vmo_handle_);
    vmo_handle_ = ZX_HANDLE_INVALID;
  }

  FXL_LOG(INFO) << "VmlogSrv shutdown successfully";
}
