// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/guest.h"

#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/string_buffer.h>
#include <libfdt.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <trusty_std.h>
#include <uapi/err.h>
#include "garnet/lib/machina/fdt_utils.h"

#include <zircon/device/grt-wdt.h>
#include "dump.h"
#include "garnet/lib/machina/io.h"
#include "ipc_service_impl.h"
#include "lib/fxl/logging.h"
#include <zircon/device/grt-wdt.h>
#include <zircon/device/grt-ramfb.h>
#include <garnet/public/lib/grt-spi.h>
#include <garnet/public/lib/grt-i2c.h>
#include <zircon/device/display.h>
#include <zircon/device/grt-gpio.h>
#include <zircon/device/grt-monitor.h>

#include <nebula/device/board.h>
#include <zircon/device/device.h>

#include <nbl_fwk/nmt_tee.h>

#define CVMD_CMD_ENABLE 5
#define CVMD_CMD_DISABLE  6

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";

uint64_t machina::Guest::thread_id = 0;
// Number of threads reading from the async device port.
static constexpr size_t kNumAsyncWorkers = 2;

static zx_status_t guest_get_resource(zx_handle_t* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0) {
    const int open_errno = errno;
    FXL_LOG(ERROR) << "Failed to open " << kResourcePath
                   << ", flags=O_RDWR, errno=" << open_errno << " ("
                   << strerror(open_errno) << ")";
    return ZX_ERR_IO;
  }

  ssize_t n = ioctl_sysinfo_get_hypervisor_resource(fd, resource);
  close(fd);

  if (n < 0) {
    FXL_LOG(ERROR) << "Failed to get hypervisor resource ioctl"
                   << ", request=IOCTL_SYSINFO_GET_HYPERVISOR_RESOURCE"
                   << ", status=" << n;
  }

  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

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

static constexpr uint32_t cache_policy(machina::MemoryPolicy policy) {
  switch (policy) {
    case machina::MemoryPolicy::kDevice:
      return ZX_CACHE_POLICY_UNCACHED_DEVICE;
    default:
      return ZX_CACHE_POLICY_CACHED;
  }
}

static constexpr uint32_t trap_kind(machina::TrapType type) {
  switch (type) {
    case machina::TrapType::MMIO_SYNC:
      return ZX_GUEST_TRAP_MEM;
    case machina::TrapType::MMIO_BELL:
      return ZX_GUEST_TRAP_BELL;
    case machina::TrapType::PIO_SYNC:
      return ZX_GUEST_TRAP_IO;
    default:
      ZX_PANIC("Unhandled TrapType %d\n", static_cast<int>(type));
      return 0;
  }
}

static uint32_t get_vmem_permission(std::string permission)
{
  uint32_t perm;
  if (permission == "RW") {
    perm = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
  } else if (permission == "RO") {
    perm = ZX_VM_FLAG_PERM_READ;
  } else if (permission == "WO") {
    perm =  ZX_VM_FLAG_PERM_WRITE;
  } else {
    perm = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
  }

  return perm;
}

namespace machina {

zx_status_t Guest::Init(const std::vector<machina::VmemSpec>& vmem) {
  zx_handle_t resource;
  zx_status_t status = guest_get_resource(&resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get hypervisor resource";
    return status;
  }

  status = zx_guest_create_new(resource, 0, vmid_, &guest_, &vmar_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create guest";
    return status;
  }
  zx_handle_close(resource);

  zx::resource root_resource;
  uint64_t index = 0;
  for (const machina::VmemSpec& spec : vmem) {
    zx::vmo vmo;
    if (index == 0) {
      FXL_LOG(INFO) << "Creating first VMO:"
                    << " name: " << spec.name << " ,base:0x" << std::hex
                    << spec.hpa_base << " ,size:0x" << std::hex << spec.size
                    << " ,policy " << static_cast<uint32_t>(spec.policy);
    }
    if (index == (vmem.size() - 1)) {
      FXL_LOG(INFO) << "Creating last VMO:"
                    << " name: " << spec.name << " ,base:0x" << std::hex
                    << spec.hpa_base << " ,size:0x" << std::hex << spec.size
                    << " ,policy " << static_cast<uint32_t>(spec.policy)
                    << " vmo cnt " << index;
    }
    ++index;

    if (spec.policy == machina::MemoryPolicy::kMpu) {
      FXL_LOG(INFO) << "Found MPU memory entry in vmems: " << spec.name;
      continue;
    }

    switch (spec.policy) {
      case machina::MemoryPolicy::kDemand:
        status = zx::vmo::create(spec.size, 0, &vmo);
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to create VMO " << status;
          return status;
        }
        break;
      case machina::MemoryPolicy::kDevice:
      case machina::MemoryPolicy::kCached:
        if (!root_resource) {
          status = get_root_resource(&root_resource);
          if (status != ZX_OK) {
            FXL_LOG(ERROR) << "Failed to get root resource " << status;
            return status;
          }
        }
        status = zx_vmo_create_physical(root_resource.get(), spec.hpa_base,
                                        spec.size, vmo.reset_and_get_address());
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to create physical VMO " << status;
          return status;
        }
        status = vmo.set_cache_policy(cache_policy(spec.policy));
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to set cache policy on VMO " << status;
          return status;
        }
        break;
      default:
        FXL_LOG(ERROR) << "Unknown memory policy "
                       << static_cast<uint32_t>(spec.policy);
        return ZX_ERR_INVALID_ARGS;
    }

    // The addr value is the same as spec.gpa_base after zx_vmar_map
    zx_paddr_t addr; // The addr value is the same as spec.gpa_base after zx_vmar_map
    uint32_t perm = get_vmem_permission(spec.permission);
    status = zx_vmar_map(vmar_,
                         ZX_VM_FLAG_PERM_EXECUTE | ZX_VM_FLAG_SPECIFIC |
                         ZX_VM_FLAG_MAP_CONTIGUOUS | perm,
                         spec.gpa_base, vmo.get(), 0, spec.size, &addr);

    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map guest physical memory " << status;
      return status;
    }
    if (!phys_mem_.vmo() && spec.is_physmem) {
      status = phys_mem_.Init(std::move(vmo), spec.hpa_base);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to initialize guest physical memory "
                       << status;
        return status;
      }
      FXL_LOG(INFO) << "Successfully initialized gpa_base:0x" << std::hex
                    << spec.gpa_base << " ,hpa_base:0x" << std::hex
                    << spec.hpa_base << " ,size:0x" << std::hex << spec.size
                    << " ,policy " << static_cast<uint32_t>(spec.policy);
    }
  }
  FXL_CHECK(phys_mem_.vmo());

  for (size_t i = 0; i < kNumAsyncWorkers; ++i) {
    fbl::StringBuffer<ZX_MAX_NAME_LEN> name_buffer;
    name_buffer.AppendPrintf("io-handler-%zu", i);
    status = device_loop_.StartThread(name_buffer.c_str());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create async worker";
      return status;
    }
  }

  for (size_t i = 0; i < kNumAsyncWorkers; ++i) {
    fbl::StringBuffer<ZX_MAX_NAME_LEN> name_buffer;
    name_buffer.AppendPrintf("trapbell-handler-%zu", i);
    trapbell_loop_.SetThreadPriority(kLooperPriority);
    status = trapbell_loop_.StartThread(name_buffer.c_str());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create trap bell worker";
      return status;
    }
  }

  vmems_ = std::move(vmem);

  FXL_CHECK(zx::event::create(0, &vm_dump_event_) == ZX_OK);

  return ZX_OK;
}

zx_status_t Guest::SetDefaultValue(const std::vector<machina::DefaultRegSpec>& reg_infos, bool vmchange) {
  zx_status_t status = ZX_OK;
  const uintptr_t align_mask = 0xFFF;

  if (vmchange) {
    reg_infos_ = reg_infos;
  }

  if (reg_infos.empty()) {
    return ZX_OK;
  }

  zx::resource root_resource;
  status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource " << status;
    return status;
  }
  for (const machina::DefaultRegSpec& reg_info : reg_infos) {
    if (((bool)(reg_info.vmstatus) != vmchange)) {
      continue;
    }
    if (!reg_info.regbase) {
      FXL_LOG(ERROR) << "Invaild regbase";
      continue;
    }

    uint64_t temp_valuenum = reg_info.value_num;
    if (temp_valuenum != 1 && temp_valuenum != 4 && temp_valuenum != 8) {
      FXL_LOG(WARNING) << "regdefault: invalid value_num " << temp_valuenum;
      return ZX_ERR_INVALID_ARGS;
    }

    auto regbase = (reg_info.regbase & ~align_mask);
    auto reg_offset = reg_info.regbase - regbase;
    zx::vmo vmo;

    status = zx_vmo_create_physical(root_resource.get(), regbase,
                                0x1000, vmo.reset_and_get_address());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create physical VMO " << status;
      return status;
    }
    status = vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to set cache policy on VMO " << status;
      return status;
    }
    uint32_t map_flags =
      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_CONTIGUOUS;
    zx_vaddr_t temp_rw_va_ = 0;
    uint64_t reg_value = reg_info.value_reg;
    FXL_LOG(INFO) << "regdefault: set reg_value : " << std::hex  << reg_value;
    status = zx_vmar_map(zx_vmar_root_self(), map_flags, 0, vmo.get(), 0,
                      0x1000, &temp_rw_va_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "regdefault: Failed to map guest physical memory " << status;
      return status;
    }
    switch (temp_valuenum) {
    case 1: {
        volatile uint8_t* reg_ptr = reinterpret_cast<volatile uint8_t*>(temp_rw_va_);
        volatile uint8_t* temp_offset_ptr = reg_ptr + reg_offset;
        volatile uint8_t* offset_ptr = reinterpret_cast<volatile uint8_t*>(temp_offset_ptr);
        uint8_t readvalue = *offset_ptr;

        *offset_ptr = static_cast<uint8_t>(reg_value);
        readvalue = *offset_ptr;
        FXL_LOG(INFO) << "regdefault: read register value, readvalue : " << std::hex << static_cast<uint64_t>(readvalue);
      }
      break;
    case 4: {
        volatile uint8_t* reg_ptr = reinterpret_cast<volatile uint8_t*>(temp_rw_va_);
        volatile uint8_t* temp_offset_ptr = reg_ptr + reg_offset;
        volatile uint32_t* offset_ptr = reinterpret_cast<volatile uint32_t*>(temp_offset_ptr);
        uint32_t readvalue = *offset_ptr;
        *offset_ptr = static_cast<uint32_t>(reg_value);
        readvalue = *offset_ptr;
        FXL_LOG(INFO) << "regdefault: read register value, readvalue : " << std::hex << static_cast<uint64_t>(readvalue);
      }
      break;
    case 8: {
        volatile uint8_t* reg_ptr = reinterpret_cast<volatile uint8_t*>(temp_rw_va_);
        volatile uint8_t* temp_offset_ptr = reg_ptr + reg_offset;
        volatile uint64_t* offset_ptr = reinterpret_cast<volatile uint64_t*>(temp_offset_ptr);
        uint64_t readvalue = *offset_ptr;

        *offset_ptr = static_cast<uint64_t>(reg_value);
        readvalue = *offset_ptr;
        FXL_LOG(INFO) << "regdefault: read register value , readvalue : " << std::hex << static_cast<uint64_t>(readvalue);
      }
      break;
    }
    if (temp_rw_va_) {
      status = zx_vmar_unmap(zx_vmar_root_self(), temp_rw_va_, 0x1000);
      if (status != ZX_OK) {
          FXL_LOG(WARNING) << "Failed to unmap memory at 0x" << std::hex
                          << temp_rw_va_ << ": " << status;
      }
    }
  }
  return status;
}

zx_status_t Guest::SetCpuAffinity(uint32_t* bind_pcpus, int size) {
  return zx_guest_set_cpu_affinity(guest_, bind_pcpus, size);
}

zx_status_t Guest::SetSmcIRQ(uint32_t smc_irq) {
  return zx_guest_set_smc_irq(guest_, smc_irq);
}

zx_status_t Guest::SetCvmLockIRQ(uint32_t cvm_lock_irq) {
  return zx_guest_set_cvm_lock_irq(guest_, cvm_lock_irq);
}

zx_status_t Guest::SetCvmLockVMO(zx_handle_t cvm_lock_vmo_handle) {
  return zx_guest_set_cvm_lock_vmo(guest_, cvm_lock_vmo_handle);
}

zx_status_t Guest::SetWakeupIrqs(uint32_t* wakeup_irqs, int size) {
  return zx_guest_set_wakeup_irqs(guest_, wakeup_irqs, size);
}

zx_status_t Guest::SetUserVmid(int32_t user_vmid) {
  return zx_guest_set_user_vmid(guest_, user_vmid);
}

zx_status_t Guest::TeeVmCreate(int32_t user_vmid) {
  return zx_guest_tee_vm_create(guest_, user_vmid);
}

zx_status_t Guest::TeeVmDestroy(int32_t user_vmid) {
  return zx_guest_tee_vm_destroy(guest_, user_vmid);
}

zx_status_t Guest::SetSchedId(uint8_t sched_id) {
  zx_status_t status;
  struct set_sched_id_req req;
  req.guest_handle = guest_;
  req.sched_id = sched_id;

  status =
      _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_ID, (void*)&req);

  if (status == ERR_NOT_SUPPORTED) {
    FXL_LOG(INFO) << "reg sched irq not supported.";
    return ZX_OK;
  }

  if (status != NO_ERROR) {
    FXL_LOG(ERROR) << "Failed to reg sched irq " << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t Guest::SetBindLkPcpu(int32_t bindlk_pcpu) {
  zx_status_t status;
  struct set_bindlk_pcpu_req req;
  req.guest_handle = guest_;
  req.bindlk_pcpu = bindlk_pcpu;
  status = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_BINDLK_PCPU,
      (void*) &req);

  if (status == ERR_NOT_SUPPORTED) {
    FXL_LOG(INFO) << "reg bind lk cpu not supported.";
    return ZX_OK;
  }

  if (status != NO_ERROR) {
    FXL_LOG(ERROR) << "Failed to reg bind lk pcpu " << status;
    return status;
  }
  FXL_LOG(INFO) << "successful to bind lk pcpu num " << bindlk_pcpu;
  return ZX_OK;
}

Guest::~Guest() {
  FXL_LOG(INFO) << "vmid=" << vmid_ << " event=guest_destructor_begin";
  ShutdownAsyncLoops();
  zx_status_t status = SetDefaultValue(reg_infos_, false);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to restore default register value " << status;
  }
  for (const auto& range : gpa_ranges_) {
    if (range->vmar_ != ZX_HANDLE_INVALID) {
      zx_vmar_destroy(range->vmar_);
      zx_handle_close(range->vmar_);
    }
  }
  if (wdt_fd_ >= 0) {
    close(wdt_fd_);
    wdt_fd_ = -1;
  }
  zx_handle_close(guest_);
  zx_handle_close(vmar_);
  FXL_LOG(INFO) << "vmid=" << vmid_ << " event=guest_destructor_complete";
}

void Guest::ShutdownAsyncLoops() {
  device_loop_.Shutdown();
  trapbell_loop_.Shutdown();
}

zx_status_t Guest::CreateMapping(TrapType type,
                                 uint64_t addr,
                                 size_t size,
                                 uint64_t offset,
                                 IoHandler* handler) {
  uint32_t kind = trap_kind(type);
  fbl::AllocChecker ac;
  auto mapping = fbl::make_unique_checked<IoMapping>(&ac, kind, addr, size,
                                                     offset, handler);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = mapping->SetTrap(this);
  if (status != ZX_OK) {
    return status;
  }

  mappings_.push_front(fbl::move(mapping));
  return ZX_OK;
}

zx_status_t Guest::MapPhysicalMemory(uintptr_t gpaddr,
                                     std::vector<uintptr_t>& page_list,
                                     bool writeable) {
  zx::resource rsc;
  zx_status_t status = get_root_resource(&rsc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource";
    return status;
  }

  zx::vmo vmo;
  status = zx_vmo_create_physical_paged(rsc.get(), page_list.data(),
                                        page_list.size(),
                                        vmo.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create vmo: " << status;
    return status;
  }

  status = vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set cache policy on VMO: " << status;
    return status;
  }

  return MapPhysicalMemory(gpaddr, vmo, writeable);
}

zx_status_t Guest::MapPhysicalMemory(uintptr_t gpaddr,
                                     zx::vmo& vmo,
                                     bool writeable) {
  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_EXECUTE |
                       ZX_VM_FLAG_SPECIFIC | ZX_VM_FLAG_MAP_RANGE;
  if (writeable) {
    map_flags |= ZX_VM_FLAG_PERM_WRITE;
  }

  size_t map_size;
  auto status = vmo.get_size(&map_size);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get size of VMO: " << status;
    return status;
  }

  zx_paddr_t mapped_addr;
  status = zx_vmar_map(vmar_, map_flags, gpaddr, vmo.get(), 0, map_size,
                       &mapped_addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map guest physical memory " << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t Guest::UnmapPhysicalMemory(uintptr_t gpaddr, size_t length) {
  zx_status_t status;

  if (phys_mem_.size() > 0) {
    status = phys_mem_.UnmapPhysicalMemory(gpaddr, length);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to unmap memory region of the guest process "
                        "VMAR by guest PA: "
                     << " gpa: " << std::hex << gpaddr << ", size: " << std::hex
                     << length;
      return status;
    }
  }

  status = zx_vmar_unmap(vmar_, gpaddr, length);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to unmap memory region of the guest stage 2 VMAR "
                      "by guest PA: "
                   << " gpa: " << std::hex << gpaddr << ", size: " << std::hex
                   << length;
  }
  return status;
}

zx_status_t Guest::ReserveGpaRange(uintptr_t gpaddr,
                                   size_t size,
                                   GpaRange** out_range) {
  if (out_range == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out_range = nullptr;
  if (size == 0 || gpaddr % PAGE_SIZE != 0 || size % PAGE_SIZE != 0 ||
      gpaddr + size < gpaddr) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::unique_ptr<GpaRange> range(new GpaRange(gpaddr, size));

  uint32_t flags = ZX_VM_FLAG_CAN_MAP_READ | ZX_VM_FLAG_CAN_MAP_WRITE |
                   ZX_VM_FLAG_CAN_MAP_EXECUTE |
                   ZX_VM_FLAG_CAN_MAP_SPECIFIC | ZX_VM_FLAG_SPECIFIC;
  zx_vaddr_t mapped_addr;
  zx_handle_t range_vmar;
  zx_status_t status = zx_vmar_allocate(vmar_, flags, gpaddr, size,
                                         &range_vmar, &mapped_addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to reserve GPA memory range: base=0x"
                   << std::hex << gpaddr << ", size=0x" << size
                   << ", status=" << status;
    return status;
  }
  if (mapped_addr != gpaddr) {
    zx_vmar_destroy(range_vmar);
    zx_handle_close(range_vmar);
    return ZX_ERR_INTERNAL;
  }

  range->vmar_ = range_vmar;
  *out_range = range.get();
  gpa_ranges_.push_back(std::move(range));
  FXL_LOG(INFO) << "Reserved GPA memory range: base=0x" << std::hex
                << gpaddr << ", size=0x" << size;
  return ZX_OK;
}

bool Guest::ContainsGpaRange(const GpaRange* range,
                             uintptr_t gpaddr,
                             size_t size) const {
  if (range == nullptr || size == 0 || gpaddr + size < gpaddr) {
    return false;
  }

  for (const auto& candidate : gpa_ranges_) {
    if (candidate.get() != range) {
      continue;
    }

    return gpaddr >= range->base_ && size <= range->size_ &&
           gpaddr - range->base_ <= range->size_ - size;
  }
  return false;
}

zx_status_t Guest::MapPhysicalMemory(GpaRange* range,
                                     uintptr_t gpaddr,
                                     uintptr_t paddr,
                                     size_t size,
                                     bool writeable) {
  if (!ContainsGpaRange(range, gpaddr, size) ||
      range->vmar_ == ZX_HANDLE_INVALID) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx::resource rsc;
  zx_status_t status = get_root_resource(&rsc);
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo vmo;
  status = zx_vmo_create_physical(rsc.get(), paddr, size,
                                  vmo.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  status = vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_EXECUTE |
                       ZX_VM_FLAG_SPECIFIC | ZX_VM_FLAG_MAP_RANGE;
  if (writeable) {
    map_flags |= ZX_VM_FLAG_PERM_WRITE;
  }

  zx_paddr_t mapped_addr;
  status = zx_vmar_map(range->vmar_, map_flags, gpaddr - range->base_,
                       vmo.get(), 0, size,
                       &mapped_addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map reserved GPA memory: " << status;
    return status;
  }
  return mapped_addr == gpaddr ? ZX_OK : ZX_ERR_INTERNAL;
}

zx_status_t Guest::UnmapPhysicalMemory(GpaRange* range,
                                       uintptr_t gpaddr,
                                       size_t size) {
  if (!ContainsGpaRange(range, gpaddr, size) ||
      range->vmar_ == ZX_HANDLE_INVALID) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  return zx_vmar_unmap(range->vmar_, gpaddr, size);
}

void Guest::RegisterVcpuFactory(VcpuFactory factory) {
  vcpu_factory_ = fbl::move(factory);
}

void Guest::RegisterVhmRequestHandler(VhmRequestHandler* handler) {
  vhm_req_handler_ = handler;
}

#define HC_ID(x) ((x >> 24) & 0xff)
#define HC_ID_IPC 0xA0UL
#define HC_IPC_SET_RX_READY 0xA0000000
#define HC_IPC_SET_TX_DONE 0xA0000001
#define HC_IPC_GET_TX_DONE_STATUS 0xA0000002
#define HC_IPC_GET_RX_READY_STATUS 0xA0000003
#define HC_IPC_CLEAR_TX_DONE_STATUS 0xA0000004
#define HC_IPC_CLEAR_RX_READY_STATUS 0xA0000005
#define HC_IPC_VM_CALL 0xA0000006
#define HC_IPC_VM_WRITE 0xA0000007
#define HC_IPC_SET_VM_SERVER_STATE 0xA0000008
#define HC_IPC_VM_READ 0xA0000009
#define HC_IPC_NOTIFY_MBOX_SUSPEND 0xA000000A
#define HC_IPC_NOTIFY_MBOX_RESUME 0xA000000B
#define HC_IPC_READ_MBOX_POWER_STATE 0xA000000C

#define HC_ID_GUEST_CALL 0xB0UL
#define HC_GUEST_PATCH_DEVICE_TREE 0xB0000000
#define HC_GUEST_GET_MEMORY_INFO 0xB0000001
#define HC_GUEST_GET_FRAMEBUF_INFO 0xB0000002
#define HC_GUEST_VCPU_SET_TIME_SLICE 0xB0000003
#define HC_GUEST_VCPU_SET_REALTIME 0xB0000004
#define HC_GUEST_VCPU_GET_TIME_SLICE 0xB0000005
#define HC_GUEST_START_KTRACE 0xB0000006
#define HC_GUEST_VCPU_GET_REALTIME 0xB0000007
#define HC_GUEST_WDTK_KICK 0xB0000008
#define HC_GUEST_WDTK_SET_RST_STATUS 0xB0000009
#define HC_GUEST_WDTK_SUSPEND 0xB000000A
#define HC_GUEST_WDTK_RESUME 0xB000000B
#define HC_GUEST_SET_SCHED_PARAM 0xB000000C
#define HC_GUEST_GET_SCHED_PARAM 0xB000000D
#define HC_GUEST_START_TEST_WDT 0xB000000E
#define HC_GUEST_START_SMMU 0xB000000F
#define HC_GUEST_CVMD_CMD 0xB0000010
#define HC_GUEST_SET_RTC 0xB0000011
#define HC_GUEST_GET_RTC 0xB0000012
#define HC_GUEST_DEINIT 0xB0000013
#define HC_GUEST_STOP_SPI_DMA 0xB0000014
#define HC_HEART_BEAT 0xB0000015
#define HC_GUEST_KTRACE_SNAPSHORT_CTL 0xB0000016
#define HC_GUEST_PATCH_DEVICE_TREE_MD 0xB0000017
#define HC_GUEST_STOP_I2C_DMA 0xB0000101
#define GUEST_IPC_REQUEST     0xFFFF0000

zx_status_t Guest::HandleNebulaHyperCall(zx_vcpu_state_t* state) {
  uint64_t id = state->x[1];

  if (HC_ID(id) == HC_ID_IPC) {
    return HandleIpcRequest(state);
  }

  if (HC_ID(id) == HC_ID_GUEST_CALL) {
    return HandleGuestCall(state);
  }

  FXL_CHECK(vhm_req_handler_ != nullptr);
  return vhm_req_handler_->HandleVhmRequest(state);
}

zx_status_t Guest::HandleIpcRequest(zx_vcpu_state_t* state) {
  uint64_t cmd = state->x[1];
  uint32_t arg = state->x[2];
  uint64_t way = state->x[6];
  uint64_t result = 0;
  IpcServiceImpl* ipc_service;

  machina::IpcRequestHandler* ipc_req_handler = NULL;
  FXL_CHECK(ipc_req_handler_ != nullptr);

  if (way == GUEST_IPC_REQUEST)
    ipc_req_handler = ipc_req_handler_;
  else {
    ipc_service = (IpcServiceImpl*)ipc_req_handler_;
    auto it = ipc_service->getIpchandlerMap().find(way);
    FXL_CHECK(it != ipc_service->getIpchandlerMap().end());
    ipc_req_handler = it->second.get();
  }

  FXL_CHECK(ipc_req_handler != NULL);

  switch (cmd) {
    case HC_IPC_SET_RX_READY:
      FXL_CHECK(arg < kNumChannels);
      ipc_req_handler->NotifyRxReady(arg);
      break;
    case HC_IPC_SET_TX_DONE:
      FXL_CHECK(arg < kNumChannels);
      ipc_req_handler->NotifyTxDone(arg);
      break;
    case HC_IPC_GET_TX_DONE_STATUS:
      result = ipc_req_handler->GetTxDoneStatus();
      break;
    case HC_IPC_GET_RX_READY_STATUS:
      result = ipc_req_handler->GetRxReadyStatus();
      break;
    case HC_IPC_CLEAR_TX_DONE_STATUS:
      ipc_req_handler->ClearTxDoneStatus(arg);
      break;
    case HC_IPC_CLEAR_RX_READY_STATUS:
      ipc_req_handler->ClearRxReadyStatus(arg);
      break;
    case HC_IPC_VM_CALL:
      result = ipc_req_handler->VMCall(state->x[2], state->x[3]);
      break;
    case HC_IPC_VM_WRITE:
      result = ipc_req_handler->VMWrite(state->x[2], state->x[3], state->x[4]);
      break;
    case HC_IPC_SET_VM_SERVER_STATE:
      ipc_req_handler->SetVMServerState(state->x[2], state->x[3], state->x[4]);
      break;
    case HC_IPC_NOTIFY_MBOX_SUSPEND:
      ipc_req_handler->SetMailboxSuspend(vmid(), state->x[2]);
      break;
    case HC_IPC_NOTIFY_MBOX_RESUME:
      ipc_req_handler->SetMailboxResume(vmid(), state->x[2]);
      break;
    case HC_IPC_READ_MBOX_POWER_STATE:
      result = ipc_req_handler->GetMailboxPowerState(state->x[2]);
      break;
    case HC_IPC_VM_READ: {
      zx_txid_t tx_id;
      result = ipc_req_handler->VMRead(state->x[2], &tx_id);
      if (result == ZX_OK) {
        state->x[1] = tx_id;
      }
      break;
    }
    default:
      FXL_LOG(ERROR) << "Unhandled ipc command " << std::hex << cmd;
      result = -1;
  }

  state->x[0] = result;
  return ZX_OK;
}

zx_status_t Guest::HandleGuestCall(zx_vcpu_state_t* state) {
  uint64_t cmd = state->x[1];
  uint64_t arg1 = state->x[2];
  uint64_t arg2 = state->x[3];
  uint64_t result = 0;

  switch (cmd) {
    case HC_GUEST_PATCH_DEVICE_TREE: {
      if (!device_tree_patcher_) {
        FXL_LOG(ERROR) << "HC_GUEST_PATCH_DEVICE_TREE: patcher not registered";
        result = static_cast<uint64_t>(-1);
        break;
      }
      device_tree_patcher_(arg1, arg2);
      break;
    }
    case HC_GUEST_PATCH_DEVICE_TREE_MD: {
      if (!device_tree_md_patcher_) {
        FXL_LOG(ERROR)
            << "HC_GUEST_PATCH_DEVICE_TREE_MD: patcher not registered";
        result = static_cast<uint64_t>(-1);
        break;
      }
      device_tree_md_patcher_(arg1, arg2);
      break;
    }
    case HC_GUEST_GET_MEMORY_INFO: {
      state->x[1] = phys_mem_.phys_base();
      state->x[2] = phys_mem_.size();
      break;
    }
    case HC_GUEST_GET_FRAMEBUF_INFO: {
      state->x[1] = 0;
      state->x[2] = 0;
      for (const machina::VmemSpec& spec : vmems_) {
        if (spec.name.compare("framebuffer") == 0) {
          state->x[1] = spec.hpa_base;
          state->x[2] = spec.size;
          FXL_LOG(INFO) << "found framebuffer base " << std::hex << state->x[1]
                        << " size " << std::hex << state->x[2];
          break;
        }
      }
      break;
    }
    case HC_GUEST_VCPU_SET_TIME_SLICE: {
      uint64_t cpu_mask = arg1;
      uint64_t time_slice = arg2;

      for (size_t id = 0; id != kMaxVcpus; ++id) {
        if (vcpus_[id] == nullptr || !((1u << id) & cpu_mask)) {
          continue;
        }
        vcpus_[id]->SetTimeSlice(time_slice);
      }
      break;
    }
    case HC_GUEST_VCPU_SET_REALTIME: {
      result = _trusty_ioctl(
          SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_REALTIME, (void*)&arg1);
      break;
    }
    case HC_GUEST_VCPU_GET_TIME_SLICE: {
      uint64_t vcpu_id = arg1;

      if (vcpu_id < kMaxVcpus && vcpus_[vcpu_id] != nullptr) {
        result = vcpus_[vcpu_id]->GetTimeSlice();
      }
      break;
    }
    case HC_GUEST_START_KTRACE: {
      int rc = nbl_act_feature(NMT_ACT_FEAT_TRACE_TOOL);
      if (rc <= 0) {
        FXL_LOG(ERROR) << "License feature [trace_tool] not available, rc="
                       << rc;
        result = ZX_ERR_ACCESS_DENIED;
      } else {
        result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_START_KTRACE,
                               (void*)&arg1);
      }
      break;
    }
    case HC_GUEST_KTRACE_SNAPSHORT_CTL: {
      result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_KTRACE_CTRL,
        (void*) &arg1);
      break;
    }
    case HC_GUEST_START_TEST_WDT: {
      printf("HC_GUEST_START_TEST_WDT arg:%lu \n", (uint64_t)arg1);
      result =
          _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_TEST_WDT, (void*)&arg1);
      break;
    }
    case HC_GUEST_VCPU_GET_REALTIME: {
      int64_t out;
      result = _trusty_ioctl(SYS_PLATFORM_FD,
                             SYS_PLATFORM_GET_CUR_THREAD_REALTIME, (void*)&out);
      if (result == 0) {
        result = out;
      }
      break;
    }
    case HC_GUEST_WDTK_KICK: {
      ++wdt_kick_call_count_;
      const bool platform = watchdog_policy().UsesPlatformWatchdog(vmid_);
      if (platform) {
        result = wdt_fd_ >= 0 ? ioctl_grt_wdt_ping(wdt_fd_)
                             : ZX_ERR_BAD_STATE;
      } else if (wdt_ != nullptr) {
        wdt_->Heartbeat();
      } else {
        result = ZX_ERR_BAD_STATE;
      }
      if (result != ZX_OK && wdt_kick_missing_log_count_++ < 3) {
        FXL_LOG(ERROR) << "event=guest_watchdog_kick_failed user_vmid=" << vmid_
                       << " guest_vmid=" << zx_log_guest_vmid(vmid_)
                       << " backend=" << (platform ? "platform" : "software")
                       << " zx_status=" << static_cast<int64_t>(result);
      } else if (wdt_kick_call_count_ == 1) {
        FXL_LOG(INFO) << "event=guest_watchdog_first_kick user_vmid=" << vmid_
                      << " backend=" << (platform ? "platform" : "software");
      }
      break;
    }
    case HC_GUEST_WDTK_SET_RST_STATUS: {
      result = wdt_fd_ >= 0 ? ioctl_grt_wdt_set_rst_status(wdt_fd_)
                           : ZX_ERR_BAD_STATE;
      break;
    }
    case HC_GUEST_WDTK_SUSPEND:
    case HC_GUEST_WDTK_RESUME: {
      const bool suspend = cmd == HC_GUEST_WDTK_SUSPEND;
      if (wdt_) {
        if (suspend) {
          wdt_->OnSuspend();
        } else {
          wdt_->OnResume();
        }
      }
      // Only SOS may suspend the platform supervisor. A suspended TBox must
      // not mask host CPU or SOS heartbeat failures.
      if (IsSosVmid(vmid_) && wdt_fd_ >= 0) {
        result = suspend ? ioctl_grt_wdt_set_suspend(wdt_fd_)
                         : ioctl_grt_wdt_set_resume(wdt_fd_);
      }
      FXL_LOG(INFO) << "event=guest_watchdog_pm user_vmid=" << vmid_
                    << " suspend=" << suspend << " zx_status=" << static_cast<int64_t>(result);
      break;
    }
    case HC_GUEST_SET_SCHED_PARAM: {
      // using struct sched_param_req defined in kernel.
      result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM,
                             (void*)&state->x[2]);
      break;
    }
    case HC_GUEST_GET_SCHED_PARAM: {
      // using struct sched_param_req defined in kernel.
      _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_SCHED_PARAM,
                    (void*)&state->x[2]);
      result = state->x[2];
      break;
    }
    case HC_GUEST_CVMD_CMD: {
      if (arg1 == CVMD_CMD_ENABLE || arg1 == CVMD_CMD_DISABLE) {
        vcpus_[0]->SetCvmDumpEnable(arg1);
      } else {
        // using struct cmvd_cmd defined in kernel.
        result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_CVMD_CMD, (void*) &state->x[2]);
      }
      break;
    }
    case HC_GUEST_START_SMMU: {
      result =
          _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_START_SMMU, (void*)&arg1);
      break;
    }
    case HC_GUEST_SET_RTC: {
      if (IsSosVmid(this->vmid())) {
        zx::resource root_resource;
        zx_status_t status = get_root_resource(&root_resource);
        FXL_CHECK(status == ZX_OK);

        uint64_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
        int64_t offset = ZX_NSEC(arg1) - now;
        FXL_LOG(INFO) << "set RTC from sos (tv_nsec): " << arg1
                      << " (tz_min_west): " << arg2;
        result = zx_clock_adjust(root_resource.get(), ZX_CLOCK_RTC, offset);
        FXL_CHECK(result == ZX_OK);
        result =
            zx_clock_adjust(root_resource.get(), ZX_CLOCK_TZ_MIN_WEST, arg2);
        FXL_CHECK(result == ZX_OK);
      }
      break;
    }
    case HC_GUEST_GET_RTC: {
      uint64_t now_rtc = zx_clock_get(ZX_CLOCK_RTC);
      uint64_t tz = zx_clock_get(ZX_CLOCK_TZ_MIN_WEST);

      FXL_LOG(INFO) << "get RTC from vmid "
                    << zx_log_guest_vmid(this->vmid())
                    << " (tv_nsec): " << now_rtc << " (tz_min_west): " << tz;
      state->x[1] = now_rtc;
      state->x[2] = tz;
      result = 0;
      break;
    }
    case HC_GUEST_DEINIT: {
      machina::InterruptController interrupt_controller(this);
      std::vector<uint16_t> irqs;
      int fd;

      /* disable disp irq */
      FXL_LOG(INFO) << "send ioctl to ramfb";
      fd = open(GRT_RAMFB_CONTROL_DEVICE, O_RDWR);
      if (fd < 0) {
        FXL_LOG(ERROR) << "open framebuffer fd failed ";
      } else {
          ioctl_display_notify_ramfb(fd);
          close(fd);
      }

      /* PassThroughInterrupts disp irq */
      irqs.push_back(arg1);
      zx_status_t status = interrupt_controller.PassThroughInterrupts(irqs);
      FXL_LOG(INFO) << "PassThroughInterrupts disp status:" << status
                    << ", irq:" << arg1;
      FXL_CHECK(status == ZX_OK);
      break;
    }
    case HC_GUEST_STOP_SPI_DMA: {
      if (!device_access_policy_.spi_dma_control_enabled) {
        FXL_LOG(WARNING) << "SPI DMA control is disabled";
        result = ZX_ERR_ACCESS_DENIED;
        break;
      }

      FXL_LOG(INFO) << "send ioctl to spi stop dma.";
      int fd;
      for (int i = 0; i < kSpiNodesCount; i++) {
        fd = open(kSpiNodes[i], O_RDWR);
        if (fd < 0) {
          FXL_LOG(ERROR) << "open spi fd failed ";
          return fd;
        }
        ioctl_grt_stop_spi_dma_transfer(fd);
        close(fd);
      }

      break;
    }
    case HC_GUEST_STOP_I2C_DMA: {
      if (!device_access_policy_.i2c_dma_control_enabled) {
        FXL_LOG(WARNING) << "I2C DMA control is disabled";
        result = ZX_ERR_ACCESS_DENIED;
        break;
      }

      FXL_LOG(INFO) << "send ioctl to spi stop dma.";
      int fd;
      for (int i = 0; i < kI2cNodesCount; i++) {
        fd = open(kI2cNodes[i], O_RDWR);
        if (fd < 0) {
          FXL_LOG(ERROR) << "open i2c fd failed ";
          return fd;
        }
        ioctl_grt_stop_i2c_dma_transfer(fd);
        close(fd);
      }

      break;
    }
    case HC_HEART_BEAT: {
      FXL_LOG(INFO) << "send heart beat to hyper cluster.";
      int fd;

      fd = open(GRT_MONITOR_CONTROL_DEVICE, O_RDWR);
      if (fd < 0) {
        FXL_LOG(ERROR) << "open monitor fd failed " << GRT_MONITOR_CONTROL_DEVICE;
        return fd;
      }

      grt_monitor_ioctl_data_t data;
      data.phy_addr = arg1;
      data.size = arg2;
      FXL_LOG(INFO) << "pa: " << std::hex << data.phy_addr;

      ioctl_grt_monitor_send_data(fd, &data);

      close(fd);
      break;
    }
    default:
      FXL_LOG(ERROR) << "Unhandled guest call " << std::hex << cmd;
      result = -1;
  }

  state->x[0] = result;
  return ZX_OK;
}

zx_status_t Guest::AddVmemDeviceTreeNodes(const DeviceTreeSpec& dtb_spec,
                                          uintptr_t phys_base) {
  uintptr_t dtb_offset = dtb_spec.base - phys_base;
  size_t dtb_size = dtb_spec.size;

  // Validate device tree.
  void* dtb = this->phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int reserved_mem_offset = fdt_path_offset(dtb, "/reserved-memory");
  if (reserved_mem_offset < 0) {
    FXL_LOG(WARNING) << "/reserved-memory path not found, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }

  for (const machina::VmemSpec& spec : vmems_) {
    char compatible[128];

    /* TODO: implemented an 'export' flags in vm config to decide which
     *       vmem should be exported to guest VM's device tree.
     */
    if (spec.name.empty() == true || spec.name.compare("devices") == 0 ||
        spec.is_reservedmem == 0) {
      continue;
    }

    // skip add device tree nbl_trace because it allready done
    if (0 == spec.name.compare("nbl_trace")) {
      continue;
    }

    const char* vmem_name = spec.name.c_str();
    int vmem_offset = fdt_add_subnode(dtb, reserved_mem_offset, vmem_name);
    check_status(vmem_offset, vmem_name);

    snprintf(compatible, sizeof(compatible), "vmem,%s", vmem_name);
    int ret = fdt_setprop_string(dtb, vmem_offset, "compatible", compatible);
    check_status(ret, "compatible");

    ret = fdt_setprop(dtb, vmem_offset, "no-map", NULL, 0);
    check_status(ret, "no-map");

    auto vmem_base = spec.hpa_base;
    auto vmem_size = spec.size;
    ret =
        fdt_setprop_cells_u64(dtb, vmem_offset, "reg", 2, vmem_base, vmem_size);
    check_status(ret, "reg");

    FXL_LOG(INFO) << "Adding reserved memory to device tree: " << compatible;
  }

  fdt_pack(dtb);
  return ZX_OK;
}

zx_status_t Guest::StartVcpu(uintptr_t entry, uint64_t id) {
  FXL_VLOG(2) << "VCPU-" << id << " entry 0x" << std::hex << entry;
  fbl::AutoLock lock(&mutex_);
  if (vcpu_nums_ >= cpu_nums_) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (vcpus_[0] == nullptr && id != 0) {
    FXL_LOG(ERROR) << "VCPU-0 must be started before other VCPUs";
    return ZX_ERR_BAD_STATE;
  }
  if (vcpus_[id] != nullptr) {
    // The guest might make multiple requests to start a particular VCPU. On
    // x86, the guest should send two START_UP IPIs but we initialise the VCPU
    // on the first. So, we ignore subsequent requests.
    return ZX_OK;
  }
  auto vcpu = fbl::make_unique<Vcpu>();
  zx_status_t status = vcpu_factory_(this, entry, id, vcpu.get());
  if (status != ZX_OK) {
    return status;
  }
  vcpus_[id] = fbl::move(vcpu);
  vcpu_nums_++;

  return ZX_OK;
}

bool IsValidSpiInterrupt(uint32_t vector) {
  return ((vector >= 32) && (vector <= 1024)) ||
         ((vector >= 4096) && (vector <= 5119));
}

zx_status_t Guest::SignalInterrupt(uint32_t mask, uint16_t vector) {
  for (size_t id = 0; id != kMaxVcpus; ++id) {
    if (vcpus_[id] == nullptr || !((1u << id) & mask)) {
      continue;
    }
    zx_status_t status = vcpus_[id]->Interrupt(vector);
    if (status != ZX_OK) {
      return status;
    }

    if (IsValidSpiInterrupt(vector)) {
      break;
    }
  }
  return ZX_OK;
}

zx_status_t Guest::SignalLpiInterrupt(uint32_t mask, uint16_t vector) {
  for (size_t id = 0; id != kMaxVcpus; ++id) {
    if (vcpus_[id] == nullptr || !((1u << id) & mask)) {
      continue;
    }
    zx_status_t status = vcpus_[id]->Interrupt(vector);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to signal lpi interrupt, vector:" << vector
                     << ",mask:" << mask;
    }
    return status;
  }
  return ZX_OK;
}

zx_status_t Guest::Join() {
  FXL_LOG(INFO) << "vmid=" << vmid_ << " event=guest_join_begin";
  zx_status_t status = ZX_OK;

  for (size_t id = 0; id != kMaxVcpus; ++id) {
    if (vcpus_[id] != nullptr) {
      vcpus_[id]->Stop();

      zx_status_t vcpu_status = vcpus_[id]->Join();
      if (vcpu_status != ZX_OK) {
        FXL_LOG(WARNING) << "vmid=" << vmid_
                         << " event=guest_join_vcpu_failed vcpu=" << id
                         << " zx_status=" << vcpu_status;
        status = vcpu_status;
      }
    }
  }

  FXL_LOG(INFO) << "vmid=" << vmid_ << " event=guest_join_complete zx_status="
                << status;
  return status;
}

// constexpr size_t kDumpStackSize = 2048;
constexpr size_t kDumpBufferSize = 1024;

void Guest::Dump(std::vector<std::string>& vcpu_states) {
  vcpu_states.clear();

  fbl::AutoLock lock(&mutex_);
  for (auto& vcpu : vcpus()) {
    if (vcpu) {
      std::string state(kDumpBufferSize, 0);
      vcpu->Dump(state);
      state.resize(strlen(state.c_str()));
      state.shrink_to_fit();
      vcpu_states.push_back(std::move(state));
    }
  }
}

zx_status_t Guest::GetGICInterruptCellSize(void* fdt, uint32_t& cells_size) {
  int gic_offset;
  int len;

  gic_offset = fdt_node_offset_by_compatible(fdt, -1, "arm,gic-v3");
  if (gic_offset < 0) {
    return ZX_ERR_NOT_FOUND;
  }

  auto prop = reinterpret_cast<const uint32_t*>(
      fdt_getprop(fdt, gic_offset, "#interrupt-cells", &len));
  if (prop == nullptr || len != sizeof(uint32_t)) {
    return ZX_ERR_NOT_FOUND;
  }

  cells_size = fdt32_to_cpu(prop[0]);

  return ZX_OK;
}

zx_status_t Guest::SchedMemFromDtb(uint64_t guest_phys_base) {
  uintptr_t dtb_offset = dtb_spec_.base - guest_phys_base;
  size_t dtb_size = dtb_spec_.size;

  // Validate device tree.
  void* dtb = phys_mem().as<void>(dtb_offset, dtb_size);

  has_sched_mem_ = false;

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int offs = fdt_path_offset(dtb, "/reserved-memory");
  if (offs < 0)
    return ZX_ERR_NO_RESOURCES;

  offs = fdt_node_offset_by_compatible(dtb, offs, "mediatek,nbl_sched");
  if (offs < 0)
    return ZX_ERR_NO_RESOURCES;

  uint64_t gpaddr, gsize;
  ret = fdt_getprop_cells_u64(dtb, offs, "reg", 2, &gpaddr, &gsize);
  if (offs < 0)
    return ZX_ERR_NO_RESOURCES;

  int root_offs = fdt_path_offset(dtb, "/");
  if (root_offs < 0)
    return ZX_ERR_NO_RESOURCES;

  int device_offs = fdt_add_subnode(dtb, root_offs, "nbl_sched");
  check_status(device_offs, "nbl_sched");

  ret = fdt_setprop_string(dtb, device_offs, "compatible", "grt,nbl_sched");
  check_status(ret, "compatible");
  ret = fdt_setprop_cells_u64(dtb, device_offs, "reg", 2, gpaddr, gsize);
  check_status(ret, "reg");

  uint32_t cells_size = 3;
  GetGICInterruptCellSize(dtb, cells_size);

  std::vector<uint32_t> props;

  if (sched_irq_) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(sched_irq_ - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI));
    if (cells_size == 4) {
      props.push_back(0);
    }
  }

  ret = fdt_setprop(dtb, device_offs, "interrupts", props.data(),
      props.size() * sizeof(uint32_t));
  check_status(ret, "interrupts");

  fdt_pack(dtb);
  sched_mem_pa_ = gpaddr;
  sched_mem_sz_ = gsize;
  has_sched_mem_ = true;
  return ZX_OK;
}

zx_status_t Guest::CrossVmDumpMemFromDtb(uint64_t guest_phys_base, const std::vector<uint32_t>& crossvm_times) {
  uintptr_t dtb_offset = dtb_spec_.base - guest_phys_base;
  size_t dtb_size = dtb_spec_.size;

  // Validate device tree.
  void* dtb = phys_mem().as<void>(dtb_offset, dtb_size);
  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int offs = fdt_path_offset(dtb, "/reserved-memory");
  if (offs < 0)
    return ZX_ERR_NO_RESOURCES;

  offs = fdt_node_offset_by_compatible(dtb, offs, "mediatek,crossvm_dump");
  if (offs < 0)
    return ZX_ERR_NO_RESOURCES;

  uint64_t gpaddr, gsize;
  ret = fdt_getprop_cells_u64(dtb, offs, "reg", 2, &gpaddr, &gsize);
  if (offs < 0)
    return ZX_ERR_NO_RESOURCES;

  int root_offs = fdt_path_offset(dtb, "/");
  if (root_offs < 0)
    return ZX_ERR_NO_RESOURCES;

  int device_offs = fdt_add_subnode(dtb, root_offs, "crossvm_dump");
  check_status(device_offs, "crossvm_dump");

  ret = fdt_setprop_string(dtb, device_offs, "compatible", "grt,crossvm_dump");
  check_status(ret, "compatible");

  if (!crossvm_times.empty()) {
    std::vector<uint32_t> times_data;
    for (auto time : crossvm_times) {
      times_data.push_back(cpu_to_fdt32(time));
    }

    ret = fdt_setprop(dtb, device_offs, "crossvm-times",
                      times_data.data(),
                      times_data.size() * sizeof(uint32_t));
    check_status(ret, "crossvm-times");

    FXL_LOG(INFO) << "Added crossvm-times property with "
                  << crossvm_times.size() << " values";
  }

  ret = fdt_setprop_cells_u64(dtb, device_offs, "reg", 2, gpaddr, gsize);
  check_status(ret, "reg");

  uint32_t cells_size = 3;
  GetGICInterruptCellSize(dtb, cells_size);

  std::vector<uint32_t> props;

  if (dump_irq_) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(dump_irq_ - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI));
    if (cells_size == 4) {
      props.push_back(0);
    }
  }

  ret = fdt_setprop(dtb, device_offs, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  check_status(ret, "interrupts");

  /* for monitor irq */
  props.clear();
  device_offs = fdt_add_subnode(dtb, root_offs, "nebula_cluster");
  check_status(device_offs, "nebula_cluster");

  ret = fdt_setprop_string(dtb, device_offs, "compatible", "nebula,cluster");
  check_status(ret, "compatible");

   if (monitor_irq_) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(monitor_irq_ - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI));
    if (cells_size == 4) {
      props.push_back(0);
    }
  }

  ret = fdt_setprop(dtb, device_offs, "interrupts", props.data(),
      props.size() * sizeof(uint32_t));
  check_status(ret, "interrupts");

  fdt_pack(dtb);
  cvmd_mem_pa_ = gpaddr;
  cvmd_mem_sz_ = gsize;
  return ZX_OK;
}

zx_status_t Guest::InitDump() {
  if (!sched_mem_pa_ || !sched_mem_sz_)
    return ZX_ERR_NOT_FOUND;

  zx::vmo vmo;
  zx_status_t status;
  zx::resource root_resource;
  status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource " << status;
    return status;
  }

  status = zx_vmo_create_physical(root_resource.get(), sched_mem_pa_,
                                  sched_mem_sz_, vmo.reset_and_get_address());
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
  uint32_t map_flags =
      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_CONTIGUOUS;
  status = zx_vmar_map(zx_vmar_root_self(), map_flags, 0, vmo.get(), 0,
                       sched_mem_sz_, &sched_mem_va_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map dump physical memory " << status;
    return status;
  }

  FXL_CHECK(vmo);
  sched_vmo_ = std::move(vmo);
  // init cross vm dump
  printf("InitDump cvmd_init:%lu,%lu.\n",sched_mem_va_, sched_mem_sz_);
  //cvmd_init(sched_mem_va_, sched_mem_sz_);

  return ZX_OK;
}

zx_status_t Guest::InitCvmdDump() {
  if (!cvmd_mem_pa_ || !cvmd_mem_sz_)
    return ZX_ERR_NOT_FOUND;

  zx::vmo vmo;
  zx_status_t status;
  zx::resource root_resource;
  status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource " << status;
    return status;
  }

  status = zx_vmo_create_physical(root_resource.get(), cvmd_mem_pa_,
      cvmd_mem_sz_, vmo.reset_and_get_address());
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
  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE
      | ZX_VM_FLAG_MAP_CONTIGUOUS;
  status = zx_vmar_map(zx_vmar_root_self(), map_flags, 0, vmo.get(),
      0, cvmd_mem_sz_, &cvmd_mem_va_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map cross vm dump physical memory " << status;
    return status;
  }

  FXL_CHECK(vmo);
  cvmd_vmo_ = std::move(vmo);

  // init cross vm dump
  cvmd_init(cvmd_mem_va_, cvmd_mem_sz_);
  benable_crossvmdump_ = true;

  return ZX_OK;
}

zx_status_t Guest::InitCDump(uintptr_t guest_phys_base, bool is_sos) {
  if (!cvmd_mem_pa_ || !cvmd_mem_sz_)
    return ZX_ERR_NOT_FOUND;

  zx_status_t status;
  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE
      | ZX_VM_FLAG_SPECIFIC | ZX_VM_FLAG_MAP_CONTIGUOUS;
  if (!is_sos) {
    zx_paddr_t pa;
    // stage1 mapping for UOS
    status = zx_vmar_map(vmar_, map_flags, cvmd_mem_pa_, cvmd_vmo_.get(), 0,
        cvmd_mem_sz_, &pa);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map cvmd physical memory " << status;
      return status;
    }
  }

  // patch dts
  uintptr_t dtb_offset = dtb_spec_.base - guest_phys_base;
  size_t dtb_size = dtb_spec_.size;

  // Validate device tree.
  void* dtb = phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (is_sos) {
    struct reg_cvmd_shm_req req;
    req.shm_base = cvmd_mem_pa_;
    req.shm_size = cvmd_mem_sz_;

    memset((void *)cvmd_mem_va_, 0, cvmd_mem_sz_);

    status = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_REG_CVMD_SHM,
        (void*) &req);

    if (status == ERR_NOT_SUPPORTED) {
      FXL_LOG(INFO) << "reg cvmd physical memory not supported.";
      return ZX_OK;
    }

    if (status != NO_ERROR) {
      FXL_LOG(ERROR) << "Failed to reg cvmd physical memory " << status;
      return status;
    }

    return ZX_OK;
  }

  int root_offs = fdt_path_offset(dtb, "/");
  if (root_offs < 0)
    return ZX_ERR_NO_RESOURCES;

  int device_offs = fdt_add_subnode(dtb, root_offs, "crossvm_dump");
  check_status(device_offs, "crossvm_dump");

  ret = fdt_setprop_string(dtb, device_offs, "compatible", "grt,crossvm_dump");
  check_status(ret, "compatible");
  ret = fdt_setprop_cells_u64(dtb, device_offs, "reg", 2, cvmd_mem_pa_,
      cvmd_mem_sz_);
  check_status(ret, "reg");

  uint32_t cells_size = 3;
  GetGICInterruptCellSize(dtb, cells_size);

  std::vector<uint32_t> props;

  ret = fdt_setprop(dtb, device_offs, "interrupts", props.data(),
      props.size() * sizeof(uint32_t));
  check_status(ret, "interrupts");

  fdt_pack(dtb);

  return ZX_OK;
}


zx_status_t Guest::InitSched(uintptr_t guest_phys_base, bool is_sos) {
  if (!sched_mem_pa_ || !sched_mem_sz_)
    return ZX_ERR_NOT_FOUND;

  zx_status_t status;
  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE |
                       ZX_VM_FLAG_SPECIFIC | ZX_VM_FLAG_MAP_CONTIGUOUS;
  if (!is_sos) {
    zx_paddr_t pa;
    // stage1 mapping for UOS
    status = zx_vmar_map(vmar_, map_flags, sched_mem_pa_, sched_vmo_.get(), 0,
                         sched_mem_sz_, &pa);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map sched physical memory " << status;
      return status;
    }
  }

  // patch dts
  uintptr_t dtb_offset = dtb_spec_.base - guest_phys_base;
  size_t dtb_size = dtb_spec_.size;

  // Validate device tree.
  void* dtb = phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (is_sos) {
    struct reg_sched_shm_req req;
    req.shm_base = sched_mem_pa_;
    req.shm_size = sched_mem_sz_;

    memset((void*)sched_mem_va_, 0, sched_mem_sz_);

    status =
        _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_REG_SCHED_SHM, (void*)&req);

    if (status == ERR_NOT_SUPPORTED) {
      FXL_LOG(INFO) << "reg sched physical memory not supported.";
      return ZX_OK;
    }

    if (status != NO_ERROR) {
      FXL_LOG(ERROR) << "Failed to reg sched physical memory " << status;
      return status;
    }

    int offs = fdt_path_offset(dtb, "/memory");
    if (offs < 0) {
      FXL_LOG(ERROR) << "can not find /memory";
      return ZX_ERR_IO_DATA_LOSS;
    }

    int len = 0;
    struct dt_dram_info* dram_info =
        (struct dt_dram_info*)fdt_getprop(dtb, offs, "reg", &len);
    if (len != sizeof(*dram_info)) {
      FXL_LOG(WARNING) << "dram info not found.";
      return ZX_OK;
    }

    struct sched_param_req dram_req;
    dram_req.type = SCHED_PARAM_TYPE_DRAM_INFO;

    uint64_t hi = fdt32_to_cpu(dram_info->start_hi);
    dram_req.dram_info.start = (hi << 32) | fdt32_to_cpu(dram_info->start_lo);
    hi = fdt32_to_cpu(dram_info->size_hi);
    dram_req.dram_info.size = (hi << 32) | fdt32_to_cpu(dram_info->size_lo);

    status = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM,
                           (void*)&dram_req);

    if (status == ERR_NOT_SUPPORTED) {
      FXL_LOG(INFO) << "set sched param not supported.";
      return ZX_OK;
    }

    return ZX_OK;
  }

  int root_offs = fdt_path_offset(dtb, "/");
  if (root_offs < 0)
    return ZX_ERR_NO_RESOURCES;

  int device_offs = fdt_add_subnode(dtb, root_offs, "nbl_sched");
  check_status(device_offs, "nbl_sched");

  ret = fdt_setprop_string(dtb, device_offs, "compatible", "grt,nbl_sched");
  check_status(ret, "compatible");
  ret = fdt_setprop_cells_u64(dtb, device_offs, "reg", 2, sched_mem_pa_,
                              sched_mem_sz_);
  check_status(ret, "reg");

  uint32_t cells_size = 3;
  GetGICInterruptCellSize(dtb, cells_size);

  std::vector<uint32_t> props;

  if (sched_irq_) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(sched_irq_ - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI));
    if (cells_size == 4) {
      props.push_back(0);
    }
  }

  ret = fdt_setprop(dtb, device_offs, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  check_status(ret, "interrupts");

  /* for monitor irq */
  props.clear();
  device_offs = fdt_add_subnode(dtb, root_offs, "nebula_cluster");
  check_status(device_offs, "nebula_cluster");

  ret = fdt_setprop_string(dtb, device_offs, "compatible", "nebula,cluster");
  check_status(ret, "compatible");

   if (monitor_irq_) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(monitor_irq_ - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI));
    if (cells_size == 4) {
      props.push_back(0);
    }
  }

  ret = fdt_setprop(dtb, device_offs, "interrupts", props.data(),
      props.size() * sizeof(uint32_t));
  check_status(ret, "interrupts");

  fdt_pack(dtb);

  return ZX_OK;
}

zx_status_t Guest::RegSchedIRQ() {
  zx_status_t status;
  struct reg_sched_irq_req req;
  req.guest_handle = guest_;
  req.sched_irq = sched_irq_;
  //req.dump_irq = dump_irq_;

  status =
      _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_REG_SCHED_IRQ, (void*)&req);

  if (status == ERR_NOT_SUPPORTED) {
    FXL_LOG(INFO) << "reg sched irq not supported.";
    return ZX_OK;
  }

  if (status != NO_ERROR) {
    FXL_LOG(ERROR) << "Failed to reg sched irq " << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t Guest::RegCVMDIRQ() {
  zx_status_t status;
  struct reg_cvmd_irq_req req;
  req.guest_handle = guest_;
  req.dump_irq = dump_irq_;

  status = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_REG_CVMD_IRQ,
      (void*) &req);

  if (status == ERR_NOT_SUPPORTED) {
    FXL_LOG(INFO) << "reg cvmd irq not supported.";
    return ZX_OK;
  }

  if (status != NO_ERROR) {
    FXL_LOG(ERROR) << "Failed to reg cvmd irq " << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t Guest::Init_Sec_Boot(uintptr_t guest_phys_base) {
  zx::vmo vmo;
  zx_status_t status;
  zx::resource root_resource;
  status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource " << status;
    return status;
  }
  status = zx_vmo_create_physical(root_resource.get(), guest_phys_base,
                                  0x200000, vmo.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create physical VMO " << status;
    return status;
  }

  status = vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set cache policy on VMO " << status;
    return status;
  }

  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_MAP_CONTIGUOUS;

  // mapping for nebula internal use

  status = zx_vmar_map(zx_vmar_root_self(), map_flags, 0, vmo.get(), 0,
                       0x200000, &va_sec_b);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map sce boot physical memory " << status;
    return status;
  }

  sbt_verify_mem = (void*)malloc(0x200000);

  if (sbt_verify_mem == NULL) {
    FXL_LOG(ERROR) << "Failed to allocate memory for sbt_verify_mem";
    return ZX_ERR_NO_MEMORY;
  }

  if (va_sec_b == 0) {
    FXL_LOG(ERROR) << "Mapped va_sec_b is fail";
    free(sbt_verify_mem);
    return ZX_ERR_BAD_STATE;
  }

  memcpy(sbt_verify_mem, (void*)va_sec_b, 0x200000);

  va_sec_boot = (zx_vaddr_t)sbt_verify_mem;

  FXL_CHECK(vmo);

  return ZX_OK;
}

zx_status_t Guest::Release_Init_Sec_Boot() {
  zx_status_t status;
  free(sbt_verify_mem);
  status = zx_vmar_unmap(zx_vmar_root_self(), va_sec_b, 0x200000);
  if (!status)
    return ZX_OK;
  else
    return status;
}

bool Guest::GpaValid(uint64_t gpa, uint32_t len) {
  return phys_mem_.contains_gpa(gpa, len);
}

zx_status_t Guest::ConfigureWatchdogPolicy(WatchdogPolicy policy) {
  if (watchdog_policy_) {
    return ZX_ERR_BAD_STATE;
  }
  watchdog_policy_.reset(new WatchdogPolicy(policy));
  return ZX_OK;
}

int Guest::create_wdt_fd() {
  if (wdt_fd_ >= 0) {
    return ZX_OK;
  }
  int fd;
  if (access(GRT_WDT_CONTROL_DEVICE, F_OK) != 0) {
    FXL_LOG(ERROR) << "vmid=" << vmid_
                   << " event=guest_watchdog_device_missing path="
                   << GRT_WDT_CONTROL_DEVICE;
    return ZX_ERR_NO_RESOURCES;
  }

  fd = open(GRT_WDT_CONTROL_DEVICE, O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR) << "vmid=" << vmid_
                   << " event=guest_watchdog_device_open_failed path="
                   << GRT_WDT_CONTROL_DEVICE;
    wdt_fd_ = -1;
    return ZX_ERR_NOT_FILE;
  }

  wdt_fd_ = fd;
  FXL_LOG(INFO) << "vmid=" << vmid_
                << " event=guest_watchdog_device_opened path="
                << GRT_WDT_CONTROL_DEVICE;

  return ZX_OK;
}

void Guest::set_stop_callback(std::function<void(zx_status_t)> stop_callback) {
  fbl::AutoLock lock(&mutex_);
  stop_callback_ = std::move(stop_callback);
}

void Guest::Stop(zx_status_t status) {
  fbl::AutoLock lock(&mutex_);
  if (stop_callback_) {
    auto callback = std::move(stop_callback_);
    stop_callback_ = nullptr;
    callback(status);
  }
}

}  // namespace machina
