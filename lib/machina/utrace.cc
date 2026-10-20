// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <atomic>
#include <vector>

#include <iostream>
#include <thread>

#include <acrn/atomic.h>

#include <libfdt.h>
#include <zircon/device/cpufreq.h>
#include <zircon/device/ktrace.h>
#include <zircon/device/sysinfo.h>
#include <zircon/nbl_trace/nbl_trace.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "garnet/lib/machina/fdt_utils.h"
#include "garnet/lib/machina/utrace.h"
#include "lib/fsl/handles/object_info.h"

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
static zx_status_t get_root_resource(zx::resource* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  resource->reset(rsc_handle);
  close(fd);
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

static bool is_fifo_mode(ktrace_ringbuf_t* rb) {
  return (rb->mode == KTRACE_MODE_FIFO);
}

static bool is_snapshot_mode(ktrace_ringbuf_t* rb) {
  return (rb->mode == KTRACE_MODE_SNAPSHOT);
}

static volatile char* ktrace_start_addr(ktrace_ringbuf_t* rb) {
  return is_snapshot_mode(rb) ? (rb->data + rb->meta_len) : rb->data;
}

static uint32_t ktrace_buf_len(ktrace_ringbuf_t* rb) {
  return is_snapshot_mode(rb) ? (rb->sz - rb->meta_len) : rb->sz;
}

static bool should_alloc_again(ktrace_ringbuf_t* rb,
                               uint64_t off,
                               size_t size) {
  bool wrapped = ((off % ktrace_buf_len(rb)) < size);

  // Here we filled a dummy trace record to skip the wrapped buffer
  if (unlikely(wrapped)) {
    // each trace record is guaranteed to be mutiplier of 4, and ring buffer
    // size is also multiplier of 4. Thus, we can simply cast the off to
    // uint32_t.
    auto tag = (uint32_t*)(ktrace_start_addr(rb) + (off % ktrace_buf_len(rb)));
    *tag = KTRACE_TAG(DUMMY_EVT, 0, size);
  }

  return wrapped;
}

static void* utrace_alloc_space(std::shared_ptr<machina::Utrace> trace,
                                uint32_t tag) {
  FXL_CHECK(trace != nullptr);
  if (trace == nullptr)
    return nullptr;
  auto rb = reinterpret_cast<ktrace_ringbuf_t*>(trace->GetTraceBuf(GRP_KTRACE));
  if (!(rb && rb->enable.load() && (KTRACE_GROUP(tag) & rb->grpmask)))
    return nullptr;

  if (unlikely(KTRACE_GRP_META == KTRACE_GROUP(tag))) {
    if (is_snapshot_mode(rb)) {
        auto len = KTRACE_LEN(tag);
        uint64_t off = (uint32_t)(rb->meta_write_pos.fetch_add(len));
        if (unlikely(off > KTRACE_META_LEN)) {
            rb->enable.store(0);
            return nullptr;
        }
        return (uint8_t*)rb->data + off;
    }
  }

  auto len = KTRACE_LEN(tag);
  uint64_t off = (uint64_t)(rb->write_pos.fetch_add(len));

  if (unlikely(should_alloc_again(rb, off, len)))
    off = (uint64_t)(rb->write_pos.fetch_add(len));

  if (unlikely(is_fifo_mode(rb) &&
               ((off - rb->read_pos) > ktrace_buf_len(rb)))) {
    // buf is overflow, just stop recording
    rb->enable.store(0);
    return nullptr;
  }

  return (uint8_t*)ktrace_start_addr(rb) + (off % ktrace_buf_len(rb));
}

void utrace(uint32_t tag,
            uint8_t meta_category,
            uint8_t meta_event,
            uint8_t meta_a,
            uint64_t a,
            uint8_t meta_b,
            uint64_t b,
            uint8_t meta_c,
            uint64_t c,
            uint8_t meta_d,
            uint64_t d) {
  auto trace = machina::Utrace::GetInstanceNoLock();
  if (!trace)
    return;
  FXL_CHECK(trace != nullptr);
  ktrace_rec_comm_t* rec = (ktrace_rec_comm_t*)utrace_alloc_space(trace, tag);
  if (rec == nullptr)
    return;
  rec->ts = zx_ticks_get();
  rec->tag = tag;
  rec->tid = (uint32_t)(fsl::GetCurrentThreadKoid());
  rec->curr_cpu = 0;
  rec->meta_mask = (uint64_t)meta_d | ((uint64_t)meta_c << 8) |
                   ((uint64_t)meta_b << 16) | ((uint64_t)meta_a << 24) |
                   ((uint64_t)meta_event << 32) |
                   ((uint64_t)meta_category << 40);
  rec->a = a;
  rec->b = b;
  rec->c = c;
  rec->d = d;
}

namespace machina {

static constexpr char kKtraceDevPath[] = "/dev/misc/ktrace";
static constexpr char kCpuFreqDevPath[] = "/dev/misc/cpufreq";

// Static methods should be defined outside the class.
std::shared_ptr<Utrace> Utrace::pinstance_{nullptr};
std::mutex Utrace::mutex_;

// The first time we call GetInstance we will lock the storage location
// and then we make sure again that the variable is null and then we
// set the value. RU:
std::shared_ptr<Utrace> Utrace::GetInstance(void) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (pinstance_ == nullptr)
    pinstance_ = std::shared_ptr<Utrace>(new Utrace());
  return pinstance_;
}

zx_status_t Utrace::SetTraceBufToKernel(void) {
  int fd = open(kKtraceDevPath, O_WRONLY);
  if (fd < 0) {
    FXL_LOG(ERROR) << "failed to open ktrace!";
    return ZX_ERR_BAD_PATH;
  }

  uint64_t args[2] = {trace_mem_pa_, trace_mem_sz_};

  FXL_LOG(INFO) << "Utrace::SetTraceBufToKernel, pa: " << args[0]
                << ", size: " << args[1];

  zx_status_t status = ioctl_nbl_trace_set_buf(fd, args);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "ioctl_nbl_trace_set_buf failed: status=" << status;

  close(fd);
  return status;
}

zx_status_t Utrace::SetTraceBufToCpuFreq(void) {
  int fd = open(kCpuFreqDevPath, O_WRONLY);
  if (fd < 0) {
    FXL_LOG(ERROR) << "failed to open cpufreq!";
    return ZX_ERR_BAD_PATH;
  }

  uint64_t args[2] = {trace_mem_pa_, trace_mem_sz_};
  ssize_t ret = ioctl_cpufreq_trace_set_buf(fd, &args[0]);
  if (ret < 0)
    FXL_LOG(ERROR) << "ioctl_cpufreq_trace_set_buf failed: ret=" << ret;

  close(fd);
  return ret;
}

zx_status_t Utrace::MapTraceBuf(Guest& guest, bool is_sos) {
  if (!trace_mem_pa_ || !trace_mem_sz_)
    return ZX_ERR_NOT_FOUND;

  if (guest.vmar() == ZX_HANDLE_INVALID)
    return ZX_ERR_INVALID_ARGS;

  zx::vmo vmo;
  zx::resource root_resource;

  zx_status_t status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource " << status;
    return status;
  }

  status = zx_vmo_create_physical(root_resource.get(), trace_mem_pa_,
                                  trace_mem_sz_, vmo.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create physical VMO " << status;
    return status;
  }

  status = vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set cache policy on VMO " << status;
    return status;
  }

  // mapping for nebula internal use
  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
  status = zx_vmar_map(zx_vmar_root_self(), map_flags, 0, vmo.get(), 0,
                       trace_mem_sz_, &trace_mem_va_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map trace physical memory " << status;
    return status;
  }

  FXL_CHECK(vmo);
  trace_vmo_ = std::move(vmo);

  return ZX_OK;
}

zx_status_t Utrace::PatchTraceDts(Guest& guest, uintptr_t guest_phys_base) {
  uintptr_t dtb_offset = guest.dtb_spec().base - guest_phys_base;
  size_t dtb_size = guest.dtb_spec().size;

  // Validate device tree.
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int offs = fdt_path_offset(dtb, "/reserved-memory");
  if (offs < 0) {
    FXL_LOG(INFO) << "can not find /reserved-memory, now create a new node";

    offs = fdt_add_subnode(dtb, 0, "reserved-memory");
    check_status(offs, "reserved-memory");
    ret = fdt_setprop(dtb, offs, "ranges", nullptr, 0);
    check_status(ret, "ranges");
    fdt_setprop_cell(dtb, offs, "#size-cells", 0x02);
    fdt_setprop_cell(dtb, offs, "#address-cells", 0x02);

    offs = fdt_path_offset(dtb, "/reserved-memory");
    offs = fdt_add_subnode(dtb, offs, "nbl_trace");
    check_status(offs, "nbl_trace");
    ret = fdt_setprop_string(dtb, offs, "compatible", "mediatek,nbl_trace");
    check_status(ret, "compatible");
    ret = fdt_setprop_cells_u64(dtb, offs, "reg", 2, trace_mem_pa_,
                                trace_mem_sz_);
    check_status(ret, "reg");
    ret = fdt_setprop(dtb, offs, "no_map", nullptr, 0);
    check_status(ret, "no-map");
  } else {
    offs = fdt_add_subnode(dtb, offs, "nbl_trace");
    check_status(offs, "nbl_trace");
    ret = fdt_setprop_string(dtb, offs, "compatible", "mediatek,nbl_trace");
    check_status(ret, "compatible");
    ret = fdt_setprop_cells_u64(dtb, offs, "reg", 2, trace_mem_pa_,
                                trace_mem_sz_);
    check_status(ret, "reg");
    ret = fdt_setprop(dtb, offs, "no_map", nullptr, 0);
    check_status(ret, "no-map");
  }

  fdt_pack(dtb);

  return ZX_OK;
}

zx_status_t Utrace::TraceMemFromDtb(Guest& guest, uintptr_t guest_phys_base, uint8_t enable) {
  uint64_t gpaddr, gsize;
  auto dtb_spec = guest.dtb_spec();
  uintptr_t dtb_offset = dtb_spec.base - guest_phys_base;
  size_t dtb_size = dtb_spec.size;
  size_t trace_enable = enable;

  // Validate device tree.
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int offs = fdt_path_offset(dtb, "/reserved-memory");
  if (offs < 0) {
    FXL_LOG(ERROR) << "/reserved-memory not found";
    return ZX_ERR_NO_RESOURCES;
  }

  offs = fdt_node_offset_by_compatible(dtb, offs, "mediatek,nbl_trace");
  if (offs < 0) {
    FXL_LOG(ERROR) << "mediatek,nbl_trace is not found";
    return ZX_ERR_NO_RESOURCES;
  }

  ret = fdt_getprop_cells_u64(dtb, offs, "reg", 2, &gpaddr, &gsize);
  if (ret < 0) {
    FXL_LOG(ERROR) << "mediatek,nbl_trace reg failed";
    return ZX_ERR_NO_RESOURCES;
  }

  FXL_LOG(INFO) << "nbl_trace: mem-enable " << trace_enable;

  if (!trace_enable) {
    ret = fdt_setprop_cells_u64(dtb, offs, "reg", 2, gpaddr,
                                NBL_TRACE_MIN_RESERVE_MEM);
    check_status(ret, "reg");
  }

  fdt_pack(dtb);

  trace_mem_pa_ = gpaddr;
  trace_mem_sz_ = gsize;

  if (!trace_enable) {
    trace_mem_sz_ = NBL_TRACE_MIN_RESERVE_MEM;
  }

  FXL_LOG(INFO) << "nbl_trace: paddr & size " << trace_mem_pa_ << " "
                << trace_mem_sz_ << " #" << __func__;
  return ZX_OK;
};

zx_vaddr_t Utrace::GetTraceBuf(uint32_t group) {
  if (!buf())
    return 0;
  auto header = reinterpret_cast<nbl_trace_header_t*>(trace_mem_va_);
  if (header->magic != NBL_TRACE_MAGIC)
    // trace buffer is not ready
    return 0;

  auto info = &header->tracers[group];
  if (!info->supported)
    // this tracer group is not supported
    return 0;

  auto offs = info->buf_offs;
  return trace_mem_va_ + header->buf_offs + offs;
}

}  // namespace machina
