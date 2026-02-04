// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/guest.h"

#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <libfdt.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/string_buffer.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include "garnet/lib/machina/fdt_utils.h"
#include <trusty_std.h>
#include <uapi/err.h>

#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/shared_irq_service_impl.h"
#include "garnet/lib/machina/audio_irq_service_impl.h"
#include "lib/fxl/logging.h"
#include "ipc_service_impl.h"
#include "garnet/lib/machina/audio_irq_client.h"
#include <zircon/device/grt-wdt.h>

#include <nbl_fwk/nmt_tee.h>

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";

uint64_t machina::Guest::thread_id = 0;

// Number of threads reading from the async device port.
static constexpr size_t kNumAsyncWorkers = 2;

static zx_status_t guest_get_resource(zx_handle_t* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  ssize_t n = ioctl_sysinfo_get_hypervisor_resource(fd, resource);
  close(fd);
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
                    << " name " << spec.name
                    << " base " << std::hex << spec.hpa_base
                    << " size " << std::hex << spec.size
                    << " policy " << static_cast<uint32_t>(spec.policy);
    }
    if (index == (vmem.size() - 1)) {
        FXL_LOG(INFO) << "Creating last VMO:"
                    << " name " << spec.name
                    << " base " << std::hex << spec.hpa_base
                    << " size " << std::hex << spec.size
                    << " policy " << static_cast<uint32_t>(spec.policy)
                    << " vmo cnt " << index;
    }
    ++index;
    switch (spec.policy) {
      case machina::MemoryPolicy::kDemand:
        status = zx::vmo::create(spec.size, 0, &vmo);
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to create VMO " << status;
          FXL_LOG(ERROR) << "VMO info show:"
                    << " name " << spec.name
                    << " base " << std::hex << spec.hpa_base
                    << " size " << std::hex << spec.size
                    << " policy " << static_cast<uint32_t>(spec.policy)
                    << " vmo cnt " << index;
          return status;
        }
        break;
      case machina::MemoryPolicy::kDevice:
      case machina::MemoryPolicy::kCached:
        if (!root_resource) {
          status = get_root_resource(&root_resource);
          if (status != ZX_OK) {
            FXL_LOG(ERROR) << "Failed to get root resource " << status;
            FXL_LOG(ERROR) << "VMO info show:"
                    << " name " << spec.name
                    << " base " << std::hex << spec.hpa_base
                    << " size " << std::hex << spec.size
                    << " policy " << static_cast<uint32_t>(spec.policy)
                    << " vmo cnt " << index;
            return status;
          }
        }
        status = zx_vmo_create_physical(root_resource.get(), spec.hpa_base,
                                        spec.size, vmo.reset_and_get_address());
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to create physical VMO " << status;
          FXL_LOG(ERROR) << "VMO info show:"
                    << " name " << spec.name
                    << " base " << std::hex << spec.hpa_base
                    << " size " << std::hex << spec.size
                    << " policy " << static_cast<uint32_t>(spec.policy)
                    << " vmo cnt " << index;
          return status;
        }
        status = vmo.set_cache_policy(cache_policy(spec.policy));
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to set cache policy on VMO " << status;
          FXL_LOG(ERROR) << "VMO info show:"
                    << " name " << spec.name
                    << " base " << std::hex << spec.hpa_base
                    << " size " << std::hex << spec.size
                    << " policy " << static_cast<uint32_t>(spec.policy)
                    << " vmo cnt " << index;
          return status;
        }
        break;
      default:
        FXL_LOG(ERROR) << "Unknown memory policy "
                       << static_cast<uint32_t>(spec.policy);
        FXL_LOG(ERROR) << "VMO info show:"
                    << " name " << spec.name
                    << " base " << std::hex << spec.hpa_base
                    << " size " << std::hex << spec.size
                    << " policy " << static_cast<uint32_t>(spec.policy)
                    << " vmo cnt " << index;
        return ZX_ERR_INVALID_ARGS;
    }

    zx_paddr_t addr;
    status = zx_vmar_map(vmar_,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE |
                             ZX_VM_FLAG_PERM_EXECUTE | ZX_VM_FLAG_SPECIFIC |
                             ZX_VM_FLAG_MAP_CONTIGUOUS,
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
      FXL_LOG(INFO) << "Successfully initialized gpa_base " << std::hex
                    << spec.gpa_base << " hpa_base " << std::hex
                    << spec.hpa_base << " size " << std::hex << spec.size
                    << " policy " << static_cast<uint32_t>(spec.policy);
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
  return ZX_OK;
}

zx_status_t Guest::SetCpuAffinity(uint32_t* bind_pcpus, int size) {
  return zx_guest_set_cpu_affinity(guest_, bind_pcpus, size);
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

  status = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_ID,
      (void*) &req);

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

Guest::~Guest() {
  zx_handle_close(guest_);
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

void Guest::UnmapPhysicalMemory(uintptr_t gpaddr, size_t length) {
  zx_vmar_unmap(vmar_, gpaddr, length);
}

void Guest::RegisterVcpuFactory(VcpuFactory factory) {
  vcpu_factory_ = fbl::move(factory);
}

void Guest::RegisterVhmRequestHandler(VhmRequestHandler* handler) {
  vhm_req_handler_ = handler;
}

#define HC_ID(x) ((x >> 24) & 0xff)
#define HC_ID_SHARED_IRQ 0x90UL
#define HC_GET_AUDIO_IRQ_STATUS 0x90000000
#define HC_CLEAR_AUDIO_IRQ_STATUS 0x90000001
#define HC_GET_SHARED_IRQ_STATUS 0x90000002
#define HC_CLEAR_SHARED_IRQ_STATUS 0x90000003

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
#define HC_GUEST_KTRACE_SNAPSHORT_CTL 0xB0000010
#define HC_GUEST_SET_RTC 0xB0000011
#define HC_GUEST_GET_RTC 0xB0000012
#define GUEST_IPC_REQUEST     0xFFFF0000

zx_status_t Guest::HandleNebulaHyperCall(zx_vcpu_state_t* state) {
  uint64_t id = state->x[1];
  if (HC_ID(id) == HC_ID_SHARED_IRQ) {
    return HandleIrqRequest(state);
  }

  if (HC_ID(id) == HC_ID_IPC) {
    return HandleIpcRequest(state);
  }

  if (HC_ID(id) == HC_ID_GUEST_CALL) {
    return HandleGuestCall(state);
  }

  FXL_CHECK(vhm_req_handler_ != nullptr);
  return vhm_req_handler_->HandleVhmRequest(state);
}

zx_status_t Guest::HandleIrqRequest(zx_vcpu_state_t* state) {
  uint64_t id = state->x[1];
  uint16_t vector = state->x[2];
  uint64_t bitmask = state->x[3];
  uint32_t status_reg = 0;
  uint32_t clr_reg = 0;
  uint32_t result = 0;

  switch (id) {
    case HC_GET_AUDIO_IRQ_STATUS:
      result = GetAudioIrqStatus(vector, vmid());
      break;
    case HC_CLEAR_AUDIO_IRQ_STATUS:
      ClearAudioIrqStatus(vector, vmid(), bitmask);
      break;
    case HC_GET_SHARED_IRQ_STATUS:
      result = GetSharedIrqStatus(vector);
      break;
    case HC_CLEAR_SHARED_IRQ_STATUS:
      ClearSharedIrqStatus(vector, bitmask);
      break;
    default:
      FXL_LOG(ERROR) << "Unhandled audio irq command " << std::hex << id;
      result = -1;
  }

  state->x[0] = result;
  state->x[1] = status_reg;
  state->x[2] = clr_reg;
  return ZX_OK;
}

zx_status_t Guest::HandleIpcRequest(zx_vcpu_state_t* state) {
  uint64_t cmd = state->x[1];
  uint32_t arg = state->x[2];
  uint64_t way = state->x[6];
  uint64_t result = 0;
  IpcServiceImpl *ipc_service;

  machina::IpcRequestHandler *ipc_req_handler = NULL;
  FXL_CHECK(ipc_req_handler_ != nullptr);

  if (way == GUEST_IPC_REQUEST)
    ipc_req_handler = ipc_req_handler_;
  else {
    ipc_service = (IpcServiceImpl *)ipc_req_handler_;
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
      ipc_req_handler->SetMailboxSuspend(state->x[2], state->x[3]);
      break;
    case HC_IPC_NOTIFY_MBOX_RESUME:
      ipc_req_handler->SetMailboxResume(state->x[2], state->x[3]);
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
      FXL_CHECK(device_tree_patcher_);
      device_tree_patcher_(arg1, arg2);
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
          FXL_LOG(INFO) << "found framebuffer base " << std::hex
                        << state->x[1] << " size " << std::hex
                        << state->x[2];
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
      result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_REALTIME,
        (void*) &arg1);
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
        FXL_LOG(ERROR) << "License feature [trace_tool] not available, rc=" << rc;
        result = ZX_ERR_ACCESS_DENIED;
      } else {
        result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_START_KTRACE,
          (void*) &arg1);
      }
      break;
    }
    case HC_GUEST_KTRACE_SNAPSHORT_CTL: {
      result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_KTRACE_CTRL,
        (void*) &arg1);
      break;
    }
    case HC_GUEST_START_TEST_WDT: {
      printf("HC_GUEST_START_TEST_WDT arg:%d \n", (int)arg1);
      result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_TEST_WDT,
        (void*) &arg1);
      break;
    }
    case HC_GUEST_VCPU_GET_REALTIME: {
      int64_t out;
      result = _trusty_ioctl(SYS_PLATFORM_FD,
        SYS_PLATFORM_GET_CUR_THREAD_REALTIME, (void*) &out);
      if (result == 0) {
        result = out;
      }
      break;
    }
    case HC_GUEST_WDTK_KICK: {
      if (wdt_fd_ > 0) {
        //FXL_LOG(INFO) << "handled yocto call kick nebula wdt. ";
        ioctl_grt_wdt_ping(wdt_fd_);
      }
      break;
    }
    case HC_GUEST_WDTK_SET_RST_STATUS: {
      if (wdt_fd_ > 0) {
        FXL_LOG(INFO) << "handled yocto call set wdt rst status. ";
        ioctl_grt_wdt_set_rst_status(wdt_fd_);
      }
      break;
    }
    case HC_GUEST_WDTK_SUSPEND: {
      if (wdt_fd_ > 0) {
        FXL_LOG(INFO) << "handled yocto call set wdt suspend. ";
        ioctl_grt_wdt_set_suspend(wdt_fd_);
      }
      break;
    }
    case HC_GUEST_WDTK_RESUME: {
      if (wdt_fd_ > 0) {
        FXL_LOG(INFO) << "handled yocto call set wdt resume. ";
        ioctl_grt_wdt_set_resume(wdt_fd_);
      }
      break;
    }
    case HC_GUEST_SET_SCHED_PARAM: {
      // using struct sched_param_req defined in kernel.
      result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM,
              (void*) &state->x[2]);
      break;
    }
    case HC_GUEST_GET_SCHED_PARAM: {
      // using struct sched_param_req defined in kernel.
      _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_SCHED_PARAM,
        (void*) &state->x[2]);
      result = state->x[2];
      break;
    }
    case HC_GUEST_START_SMMU: {
      result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_START_SMMU, (void*) &arg1);
      break;
    }
    case HC_GUEST_SET_RTC: {
      if (this->vmid() == SOS_VMID) {
        zx::resource root_resource;
        zx_status_t status = get_root_resource(&root_resource);
        FXL_CHECK(status == ZX_OK);

        uint64_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
        int64_t offset = ZX_NSEC(arg1) - now;
        FXL_LOG(INFO) << "set RTC from sos (tv_nsec): " << arg1
                      << " (tz_min_west): " << arg2;
        result = zx_clock_adjust(root_resource.get(), ZX_CLOCK_RTC, offset);
        FXL_CHECK(result == ZX_OK);
        result = zx_clock_adjust(root_resource.get(), ZX_CLOCK_TZ_MIN_WEST, arg2);
        FXL_CHECK(result == ZX_OK);
      }
      break;
    }
    case HC_GUEST_GET_RTC: {
        uint64_t now_rtc = zx_clock_get(ZX_CLOCK_RTC);
        uint64_t tz = zx_clock_get(ZX_CLOCK_TZ_MIN_WEST);

        FXL_LOG(INFO) << "get RTC from vmid " << (int32_t)this->vmid() << " (tv_nsec): " << now_rtc
                      << " (tz_min_west): " << tz;
        state->x[1] = now_rtc;
        state->x[2] = tz;
        result = 0;
        break;
    }
    default:
      FXL_LOG(ERROR) << "Unhandled guest call " << std::hex << cmd;
      result = -1;
  }

  state->x[0] = result;
  return ZX_OK;
}

zx_status_t Guest::AddVmemDeviceTreeNodes(
    const DeviceTreeSpec& dtb_spec,
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
    if (spec.name.empty() == true || spec.name.compare("devices") == 0 || spec.is_reservedmem == 0) {
      continue;
    }

    // skip add device tree nbl_trace because it allready done
    if (0 == spec.name.compare("nbl_trace")) {
      continue;
    }

    const char *vmem_name = spec.name.c_str();
    int vmem_offset = fdt_add_subnode(dtb, reserved_mem_offset, vmem_name);
    check_status(vmem_offset, vmem_name);

    snprintf(compatible, sizeof(compatible), "vmem,%s", vmem_name);
    int ret = fdt_setprop_string(dtb, vmem_offset, "compatible", compatible);
    check_status(ret, "compatible");

    ret = fdt_setprop(dtb, vmem_offset, "no-map", NULL, 0);
    check_status(ret, "no-map");

    auto vmem_base = spec.hpa_base;
    auto vmem_size = spec.size;
    ret = fdt_setprop_cells_u64(dtb, vmem_offset, "reg", 2, vmem_base, vmem_size);
    check_status(ret, "reg");

    FXL_LOG(INFO) << "Adding reserved memory to device tree: " << compatible;
  }

  fdt_pack(dtb);
  return ZX_OK;
}

void Guest::InitializeSharedIrqService(SharedIrqServiceImpl* svc) {
  shared_irq_svc_ = svc;

  zx::resource resource;
  zx_status_t status = get_root_resource(&resource);
  FXL_CHECK(status == ZX_OK);

  fbl::AutoLock lock(&shared_irq_mutex_);
  for (auto& irq_spec : svc->shared_irq_specs()) {
    std::unique_ptr<SharedIrq> shared_irq;
    SharedIrq::Create(resource, irq_spec, this, &shared_irq);
    shared_irq_map_.emplace(shared_irq->vector(), std::move(shared_irq));
  }
}

void Guest::AddNewSharedIrq(std::unique_ptr<SharedIrq> shared_irq) {
  fbl::AutoLock lock(&shared_irq_mutex_);
  shared_irq_map_.emplace(shared_irq->vector(), std::move(shared_irq));
}

void Guest::ForwardIrqEvent(uint16_t vector, uint32_t pending_bitmask) {
  FXL_CHECK(shared_irq_svc_ != nullptr);
  shared_irq_svc_->ForwardIrqEvent(vector, pending_bitmask);
}

void Guest::AppendSharedIrqStatus(uint16_t vector, uint32_t pending_bitmask) {
  fbl::AutoLock lock(&shared_irq_mutex_);
  auto it = shared_irq_map_.find(vector);
  FXL_CHECK(it != shared_irq_map_.end());

  it->second->AppendIrqStatus(pending_bitmask);
}

void Guest::UpdateSharedIrqPreference(uint16_t vector,
                                      uint32_t interest_bitmask) {
  fbl::AutoLock lock(&shared_irq_mutex_);
  auto it = shared_irq_map_.find(vector);
  if (it != shared_irq_map_.end())
    it->second->set_interest_bitmap(interest_bitmask);
  else
    FXL_LOG(WARNING) << "shared irq " << vector << " is not existed";
}

uint32_t Guest::GetSharedIrqStatus(uint16_t vector) {
  fbl::AutoLock lock(&shared_irq_mutex_);
  auto it = shared_irq_map_.find(vector);
  FXL_CHECK(it != shared_irq_map_.end());

  return it->second->GetIrqStatus();
}

void Guest::ClearSharedIrqStatus(uint16_t vector, uint32_t bitmask) {
  fbl::AutoLock lock(&shared_irq_mutex_);
  auto it = shared_irq_map_.find(vector);
  FXL_CHECK(it != shared_irq_map_.end());
  it->second->ClearIrqStatus(bitmask);
}

void Guest::InitializeAudioIrqService(AudioIrqServiceImpl* svc) {
  audio_irq_svc_ = svc;

  zx::resource resource;
  zx_status_t status = get_root_resource(&resource);
  FXL_CHECK(status == ZX_OK);

  fbl::AutoLock lock(&audio_irq_mutex_);
  for (auto& irq_spec : svc->audio_irq_specs()) {
    std::unique_ptr<AudioIrq> audio_irq;
    AudioIrq::Create(resource, irq_spec, this, &audio_irq);
    audio_irq_map_.emplace(audio_irq->vector(), std::move(audio_irq));
  }
}

void Guest::InitializeAudioIrqClient(AudioIrqClient* audio_irq_client) {
  audio_irq_client_ = audio_irq_client;
}

void Guest::AddNewAudioIrq(std::unique_ptr<AudioIrq> audio_irq) {
  fbl::AutoLock lock(&audio_irq_mutex_);
  audio_irq_map_.emplace(audio_irq->vector(), std::move(audio_irq));
}

void Guest::ForwardAudioIrqEvent(uint16_t vector, uint64_t pending_bitmask, uint32_t vmid) {
  FXL_CHECK(audio_irq_svc_ != nullptr);
  audio_irq_svc_->ForwardIrqEvent(vector, pending_bitmask, vmid);
}

void Guest::AppendAudioIrqStatus(uint16_t vector, uint32_t pending_bitmask) {
  fbl::AutoLock lock(&audio_irq_mutex_);
  auto it = audio_irq_map_.find(vector);
  FXL_CHECK(it != audio_irq_map_.end());

  it->second->AppendIrqStatus(vmid(), pending_bitmask);
}


void Guest::UpdateAudioIrqSpec(uint16_t vector,
                                uint32_t irq_status_offset,
                                uint32_t irq_clear_offset,
                                uint64_t interest_bitmask,
                                uint32_t vmid,
                                uint32_t index) {
  fbl::AutoLock lock(&audio_irq_mutex_);
  auto it = audio_irq_map_.find(vector);
  if (it != audio_irq_map_.end()) {
    it->second->set_afe_irq_spec(irq_status_offset, irq_clear_offset, interest_bitmask, vmid, index);
  }
  else
    FXL_LOG(WARNING) << "audio irq " << vector << " is not existed";
}

void Guest::UpdateAfeIrq(uint16_t vector, uint32_t irq_status_offset, uint32_t irq_clear_offset, uint64_t interest_mask, uint32_t vmid, uint32_t index) {
  FXL_CHECK(audio_irq_client_ != nullptr);
  audio_irq_client_->UpdateAfeIrq(vector, irq_status_offset, irq_clear_offset, interest_mask, vmid, index);
}

void Guest::UpdateAfeIrqRegs(uint32_t vector) {
  FXL_CHECK(audio_irq_client_ != nullptr);
  audio_irq_client_->UpdateAfeIrqRegs(vector);
}

void Guest::UpdateAfeIrqRegsMap(uint32_t vector) {
  fbl::AutoLock lock(&audio_irq_mutex_);
  auto it = audio_irq_map_.find(vector);
  if (it != audio_irq_map_.end()) {
    it->second->update_afe_irq_regs_map();
  }
  else
    FXL_LOG(WARNING) << "audio irq " << vector << " is not existed";
}

uint32_t Guest::GetAudioIrqStatus(uint16_t vector, uint32_t vmid) {
  fbl::AutoLock lock(&audio_irq_mutex_);
  auto it = audio_irq_map_.find(vector);
  FXL_CHECK(it != audio_irq_map_.end());

  return it->second->GetIrqStatus(vmid);
}

void Guest::ClearAudioIrqStatus(uint16_t vector, uint32_t vmid, uint64_t bitmask) {
  fbl::AutoLock lock(&audio_irq_mutex_);
  auto it = audio_irq_map_.find(vector);
  FXL_CHECK(it != audio_irq_map_.end());
  it->second->ClearIrqStatus(vmid, bitmask);
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
  // We assume that the VCPU-0 thread will be started first, and that no
  // additional VCPUs will be brought up after it terminates.
  zx_status_t status = vcpus_[0]->Join();

  // Once the initial VCPU has terminated, wait for any additional VCPUs.
  for (size_t id = 1; id != kMaxVcpus; ++id) {
    if (vcpus_[id] != nullptr) {
      zx_status_t vcpu_status = vcpus_[id]->Join();
      if (vcpu_status != ZX_OK) {
        status = vcpu_status;
      }
    }
  }

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

zx_status_t Guest::GetGICInterruptCellSize(void *fdt, uint32_t &cells_size) {
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

  fdt_pack(dtb);
  sched_mem_pa_ = gpaddr;
  sched_mem_sz_ = gsize;
  has_sched_mem_ = true;
  return ZX_OK;
}

zx_status_t Guest::InitSched(uintptr_t guest_phys_base, bool is_sos) {
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

  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE
      | ZX_VM_FLAG_SPECIFIC | ZX_VM_FLAG_MAP_CONTIGUOUS;
  if (!is_sos) {
    zx_paddr_t pa;
    // stage1 mapping for UOS
    status = zx_vmar_map(vmar_, map_flags, sched_mem_pa_, vmo.get(), 0,
        sched_mem_sz_, &pa);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map sched physical memory " << status;
      return status;
    }
  }

  // mapping for nebula internal use
  status = zx_vmar_map(zx_vmar_root_self(), map_flags, sched_mem_pa_, vmo.get(),
      0, sched_mem_sz_, &sched_mem_va_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map sched physical memory " << status;
    return status;
  }


  FXL_CHECK(vmo);
  sched_vmo_ = std::move(vmo);

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

    memset((void *)sched_mem_va_, 0, sched_mem_sz_);

    status = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_REG_SCHED_SHM,
        (void*) &req);

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
    struct dt_dram_info *dram_info = (struct dt_dram_info*) fdt_getprop(dtb,
        offs, "reg", &len);
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
        (void*) &dram_req);

    if (status == ERR_NOT_SUPPORTED) {
      FXL_LOG(INFO) << "set sched param not supported.";
      return ZX_OK;
    }

    return ZX_OK;
  }

  int offs = fdt_path_offset(dtb, "/reserved-memory");
  if (offs < 0) {
    FXL_LOG(ERROR) << "can not find /reserved-memory";
    return ZX_ERR_IO_DATA_LOSS;
  }

  offs = fdt_add_subnode(dtb, offs, "nbl_sched");
  check_status(offs, "nbl_sched");
  ret = fdt_setprop_string(dtb, offs, "compatible", "mediatek,nbl_sched");
  check_status(ret, "compatible");
  ret = fdt_setprop_cells_u64(dtb, offs, "reg", 2, sched_mem_pa_,
      sched_mem_sz_);
  check_status(ret, "reg");
  ret = fdt_setprop(dtb, offs, "no_map", nullptr, 0);
  check_status(ret, "no-map");

  int root_offs = fdt_path_offset(dtb, "/");
  if (root_offs < 0)
    return ZX_ERR_NO_RESOURCES;

  int device_offs = fdt_add_subnode(dtb, root_offs, "nbl_sched");
  check_status(device_offs, "nbl_sched");

  ret = fdt_setprop_string(dtb, device_offs, "compatible", "grt,nbl_sched");
  check_status(ret, "compatible");

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

  fdt_pack(dtb);

  return ZX_OK;
}

zx_status_t Guest::RegSchedIRQ() {
  zx_status_t status;
  struct reg_sched_irq_req req;
  req.guest_handle = guest_;
  req.sched_irq = sched_irq_;
  req.dump_irq = dump_irq_;

  status = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_REG_SCHED_IRQ,
      (void*) &req);

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

  uint32_t map_flags = ZX_VM_FLAG_PERM_READ |
                         ZX_VM_FLAG_MAP_CONTIGUOUS;

  // mapping for nebula internal use

  status = zx_vmar_map(zx_vmar_root_self(), map_flags, 0,
                       vmo.get(), 0, 0x200000, &va_sec_b);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map sce boot physical memory " << status;
    return status;
  }

  sbt_verify_mem = (void *)malloc(0x200000);

  if (sbt_verify_mem == NULL) {
      FXL_LOG(ERROR) << "Failed to allocate memory for sbt_verify_mem";
      return ZX_ERR_NO_MEMORY;
  }

  if (va_sec_b == 0) {
      FXL_LOG(ERROR) << "Mapped va_sec_b is fail";
      free(sbt_verify_mem);
      return ZX_ERR_BAD_STATE;
  }

  memcpy(sbt_verify_mem, (void *)va_sec_b, 0x200000);

  va_sec_boot = (zx_vaddr_t)sbt_verify_mem;

  FXL_CHECK(vmo);

  return ZX_OK;
}

zx_status_t Guest::Release_Init_Sec_Boot(){
  zx_status_t status;
  free(sbt_verify_mem);
  status = zx_vmar_unmap(zx_vmar_root_self(), va_sec_b, 0x200000);
  if(!status)
    return ZX_OK;
  else
    return status;
}

bool Guest::GpaValid(uint64_t gpa, uint32_t len) {
  return phys_mem_.contains_gpa(gpa, len);
}

int Guest::create_wdt_fd() {
  int fd;
  if (access(GRT_WDT_CONTROL_DEVICE, F_OK) != 0) {
      FXL_LOG(ERROR) << GRT_WDT_CONTROL_DEVICE << " does not exist!";
      return ZX_ERR_NO_RESOURCES;
  }

  fd = open(GRT_WDT_CONTROL_DEVICE, O_RDWR);
  if (fd < 0) {
      FXL_LOG(ERROR) << GRT_WDT_CONTROL_DEVICE << " open filed!";
      return ZX_ERR_NOT_FILE;
  }

  wdt_fd_ = fd;

  return ZX_OK;
}

}  // namespace machina
