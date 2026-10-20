// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/vcpu.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/auto_lock.h>
#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#include <trusty_std.h>
#include <uapi/err.h>

#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/io.h"
#include "lib/fxl/logging.h"

#ifdef __x86_64__
#include "garnet/lib/machina/arch/x86/decode.h"
#endif

struct vcpu_set_time_slice_req {
  zx_handle_t vcpu_handle;
  uint64_t time_slice;
};

struct vcpu_get_time_slice_rsp {
  uint64_t time_slice;
};
struct vcpu_get_time_slice_req {
  zx_handle_t vcpu_handle;
};
struct vcpu_get_time_slice_cmd {
  struct vcpu_get_time_slice_req req;
  struct vcpu_get_time_slice_rsp rsp;
};

enum {
  VCPU_GROUP_CMD_ADD = 0,
  VCPU_GROUP_CMD_REMOVE = 1,
  VCPU_GROUP_CMD_DUMP = 2,
};

enum {
  VCPU_CVMD_CMD_ADD = 0,
  VCPU_CVMD_CMD_REMOVE = 1,
  VCPU_CVMD_CMD_DUMP = 2,
};

struct config_vcpu_group_req {
  zx_handle_t vcpu_handle;
  uint32_t cmd;
  uint32_t group;
};

struct config_vcpu_cvmdump_req {
  zx_handle_t vcpu_handle;
  uint32_t cmd;
};

namespace machina {

thread_local Vcpu* thread_vcpu = nullptr;

Vcpu::~Vcpu() {
  if (vcpu_ != ZX_HANDLE_INVALID) {
    zx_handle_close(vcpu_);
    vcpu_ = ZX_HANDLE_INVALID;
  }
  cnd_destroy(&state_cnd_);
}

#if __aarch64__
static zx_status_t HandleMmioArm(Guest *guest,
                                 const zx_packet_guest_mem_t& mem,
                                 uint64_t trap_key,
                                 uint64_t* reg) {
  machina::IoValue mmio = {mem.access_size, {.u64 = mem.data}, 0, 0, 0, 0, nullptr};
  IoMapping* mapping = IoMapping::FromPortKey(trap_key);
  if (!mem.read) {
    auto ret = mapping->Write(mem.addr, mmio);
    if (ret == ZX_ERR_PCI_BAR_REALLOC) {
      FXL_LOG(INFO) << "change pci bar address, old:" << std::hex
                  << mmio.old << ", new:" << std::hex
                  << mmio._new;
      zx_guest_remove_trap(guest->handle(), ZX_GUEST_TRAP_MEM,
                         mmio.old);
      zx_guest_remove_trap(guest->handle(), ZX_GUEST_TRAP_MEM,
                         mmio._new);
      ret = guest->CreateMapping(
          (machina::TrapType)mmio.type, mmio._new, mmio.size, 0, (machina::IoHandler *)mmio.handler);
    }
    return ret;
  }

  zx_status_t status = mapping->Read(mem.addr, &mmio);
  if (status != ZX_OK) {
    return status;
  }
  *reg = mmio.u64;
  if (mem.sign_extend && *reg & (1ul << (mmio.access_size * CHAR_BIT - 1))) {
    *reg |= UINT64_MAX << mmio.access_size;
  }
  return ZX_OK;
}
#elif __x86_64__
static zx_status_t HandleMmioX86(const zx_packet_guest_mem_t& mem,
                                 uint64_t trap_key,
                                 const machina::Instruction* inst) {
  zx_status_t status;
  IoValue mmio = {inst->access_size, {.u64 = 0}};
  switch (inst->type) {
    case INST_MOV_WRITE:
      switch (inst->access_size) {
        case 1:
          status = inst_write8(inst, &mmio.u8);
          break;
        case 2:
          status = inst_write16(inst, &mmio.u16);
          break;
        case 4:
          status = inst_write32(inst, &mmio.u32);
          break;
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }
      if (status != ZX_OK) {
        return status;
      }
      return IoMapping::FromPortKey(trap_key)->Write(mem.addr, mmio);

    case INST_MOV_READ:
      status = IoMapping::FromPortKey(trap_key)->Read(mem.addr, &mmio);
      if (status != ZX_OK) {
        return status;
      }
      switch (inst->access_size) {
        case 1:
          return inst_read8(inst, mmio.u8);
        case 2:
          return inst_read16(inst, mmio.u16);
        case 4:
          return inst_read32(inst, mmio.u32);
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }

    case INST_TEST:
      status = IoMapping::FromPortKey(trap_key)->Read(mem.addr, &mmio);
      if (status != ZX_OK) {
        return status;
      }
      switch (inst->access_size) {
        case 1:
          return inst_test8(inst, static_cast<uint8_t>(inst->imm), mmio.u8);
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }

    default:
      return ZX_ERR_INVALID_ARGS;
  }
}
#endif

struct Vcpu::ThreadEntryArgs {
  Guest* guest;
  Vcpu* vcpu;
  zx_vaddr_t entry;
  int32_t prio;
  uint64_t time_slice;
};

zx_status_t Vcpu::Create(Guest* guest,
                         zx_vaddr_t entry,
                         uint64_t id,
                         int32_t prio,
                         uint64_t time_slice) {
  guest_ = guest;
  id_ = id;
  ThreadEntryArgs args = {
      .guest = guest,
      .vcpu = this,
      .entry = entry,
      .prio = prio,
      .time_slice = time_slice,
  };
  fbl::StringBuffer<ZX_MAX_NAME_LEN> name_buffer;
  auto vmid = guest->vmid();
  // vmid should be a reasonable number
  if (vmid >= 16)
    name_buffer.AppendPrintf("vcpu-%lu", id);
  else
    // distinguish multiple VMs by name
    name_buffer.AppendPrintf("%u.vcpu-%lu", guest->vmid(), id);
  auto thread_entry = [](void* arg) {
    ThreadEntryArgs* thread_args = reinterpret_cast<ThreadEntryArgs*>(arg);
    return thread_args->vcpu->ThreadEntry(thread_args);
  };
  int ret =
      thrd_create_with_name(&thread_, thread_entry, &args, name_buffer.c_str());
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  fbl::AutoLock lock(&mutex_);
  WaitForStateChangeLocked(State::UNINITIALIZED);
  if (state_ != State::WAITING_TO_START) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

Vcpu* Vcpu::GetCurrent() {
  return thread_vcpu;
}

zx_status_t Vcpu::ThreadEntry(const ThreadEntryArgs* args) {
  {
    if (args->prio)
      zx_thread_set_priority(args->prio);

    fbl::AutoLock lock(&mutex_);
    if (state_ != State::UNINITIALIZED) {
      return ZX_ERR_BAD_STATE;
    }

    zx_status_t status = zx_vcpu_create(args->guest->handle(), /*unused_hcr=*/0,
                                        args->entry, &vcpu_);
    if (status != ZX_OK) {
      SetStateLocked(State::ERROR_FAILED_TO_CREATE);
      return status;
    }

    if (args->time_slice)
      SetTimeSlice(args->time_slice);

    SetStateLocked(State::WAITING_TO_START);
    WaitForStateChangeLocked(State::WAITING_TO_START);
    if (state_ != State::STARTING) {
      return ZX_ERR_BAD_STATE;
    }

    if (initial_vcpu_state_ != nullptr) {
      status = WriteState(ZX_VCPU_STATE, initial_vcpu_state_,
                          sizeof(*initial_vcpu_state_));
      if (status != ZX_OK) {
        SetStateLocked(State::ERROR_FAILED_TO_START);
        return status;
      }
    }

    if (start_callback_)
      start_callback_();

    SetStateLocked(State::STARTED);
  }

  zx_handle_t current_thread = zx_thread_self();
  zx_handle_t current_process = zx_process_self();

  char process_name[ZX_MAX_NAME_LEN];
  zx_info_handle_basic_t thread_info;

  zx_object_get_property(current_process, ZX_PROP_NAME, process_name,
                         sizeof(process_name));
  zx_object_get_info(current_thread, ZX_INFO_HANDLE_BASIC, &thread_info,
                     sizeof(thread_info), nullptr, nullptr);
#if 0
  uintptr_t pa = args->guest->GetNblTraceBuffer();

  if(pa != 0){
    struct nbl_trace_buf * pbuffer = reinterpret_cast<struct nbl_trace_buf*>(pa);
    if(!strncmp(process_name, "sos", 3)){
      pbuffer->vcpu_thread.delay[0][id_].thread_id = thread_info.koid;
    }else{
      pbuffer->vcpu_thread.delay[1][id_].thread_id = thread_info.koid;
      pbuffer->vcpu_thread.delay[1][0].thread_id = machina::Guest::thread_id;
    }
  }else{
    machina::Guest::thread_id = thread_info.koid;
  }
#endif
  return Loop();
}

void Vcpu::SetStateLocked(State new_state) {
  state_ = new_state;
  cnd_signal(&state_cnd_);
}

void Vcpu::WaitForStateChangeLocked(State initial_state) {
  while (state_ == initial_state) {
    cnd_wait(&state_cnd_, mutex_.GetInternal());
  }
}

void Vcpu::SetState(State new_state) {
  fbl::AutoLock lock(&mutex_);
  SetStateLocked(new_state);
}

zx_status_t Vcpu::Loop() {
  FXL_DCHECK(thread_vcpu == nullptr) << "Thread has multiple VCPUs";
  thread_vcpu = this;
  FXL_LOG(INFO) << "event=vcpu_loop_begin vcpu=" << id_;
  zx_port_packet_t packet;

  zx_status_t status;
  // Invoke the stop callback if this function returns. This callback will
  // ultimately result in the VMM being destroyed.
  auto deferred =
      fbl::MakeAutoCall([this, &status] { this->guest_->Stop(status); });
  while (true) {
    status = zx_vcpu_resume(vcpu_, &packet);
    if (unlikely(status != ZX_OK)) {
      SetState(State::TERMINATED);
      if (status == ZX_ERR_STOP || status == ZX_ERR_CANCELED || status == ZX_ERR_UNAVAILABLE) {
        FXL_LOG(INFO) << "event=vcpu_resume_stopped vcpu=" << id_
                      << " zx_status=" << status;
        return ZX_OK;
      } else {
        FXL_LOG(ERROR) << "Failed to resume VCPU-" << id_ << ": " << status;
        return status;
      }
    }

    status = HandlePacket(packet);
    if (unlikely(status != ZX_OK)) {
      if (status == ZX_ERR_STOP) {
        SetState(State::TERMINATED);
        FXL_LOG(INFO) << "VCPU-" << id_ << " stopped";
        return ZX_OK;
      }
      FXL_LOG(ERROR) << "Failed to handle packet " << packet.type << ": "
                     << status;
      // NOTE(GR-0002): Don't exit the Vcpu thread if failed to handle packet.
      // exit(status);

      // for unhandled guest mmio trap, inject data abort to vcpu
      if (packet.type == ZX_PKT_TYPE_GUEST_MEM) {
        FXL_LOG(INFO) << "inject data abort to guest, fault addr=0x" << std::hex
                      << packet.guest_mem.addr;
        zx_vcpu_inject_exception(vcpu_, packet.guest_mem.addr);
      }
    }
  }
}

zx_status_t Vcpu::Start(zx_vcpu_state_t* initial_vcpu_state) {
  fbl::AutoLock lock(&mutex_);
  if (state_ != State::WAITING_TO_START) {
    return ZX_ERR_BAD_STATE;
  }

  // Place the VCPU in the |STARTING| state which will cause the VCPU to
  // write the initial state and begin VCPU execution.
  initial_vcpu_state_ = initial_vcpu_state;
  SetStateLocked(State::STARTING);
  WaitForStateChangeLocked(State::STARTING);
  if (state_ != State::STARTED) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t Vcpu::Join() {
  zx_status_t vcpu_result = ZX_ERR_INTERNAL;
  int ret = thrd_join(thread_, &vcpu_result);
  return ret == thrd_success ? vcpu_result : ZX_ERR_INTERNAL;
}

zx_status_t Vcpu::Interrupt(uint32_t vector) {
  return zx_vcpu_interrupt(vcpu_, vector);
}

zx_status_t Vcpu::ReadState(uint32_t kind, void* buffer, uint32_t len) const {
  return zx_vcpu_read_state(vcpu_, kind, buffer, len);
}

zx_status_t Vcpu::WriteState(uint32_t kind, const void* buffer, uint32_t len) {
  return zx_vcpu_write_state(vcpu_, kind, buffer, len);
}

zx_status_t Vcpu::HandlePacket(const zx_port_packet_t& packet) {
  switch (packet.type) {
    case ZX_PKT_TYPE_GUEST_VCPU:
      return HandleVcpu(packet.guest_vcpu, packet.key);
    case ZX_PKT_TYPE_GUEST_MEM:
      return HandleMem(packet.guest_mem, packet.key);
#if __x86_64__
    case ZX_PKT_TYPE_GUEST_IO:
      return HandleIo(packet.guest_io, packet.key);
#endif  // __x86_64__
    default:
      FXL_LOG(ERROR) << "Unhandled guest packet " << packet.type;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t Vcpu::HandleMem(const zx_packet_guest_mem_t& mem,
                            uint64_t trap_key) {
  zx_vcpu_state_t vcpu_state;
  zx_status_t status;
#if __aarch64__
  if (mem.read)
#endif
  {
    status = ReadState(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    if (status != ZX_OK) {
      return status;
    }
  }

  bool do_write = false;
#if __aarch64__
  do_write = mem.read;
  status = HandleMmioArm(guest_, mem, trap_key, &vcpu_state.x[mem.xt]);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unhandled Mmio addr=" << std::hex << mem.addr
                   << " reg=" << std::hex << mem.data;
  }
#elif __x86_64__
  Instruction inst;
  status = inst_decode(mem.inst_buf, mem.inst_len, &vcpu_state, &inst);
  if (status != ZX_OK) {
    fbl::StringBuffer<LINE_MAX> buffer;
    for (uint8_t i = 0; i < mem.inst_len; i++)
      buffer.AppendPrintf(" %x", mem.inst_buf[i]);
    FXL_LOG(ERROR) << "Unsupported instruction:" << buffer.c_str();
  } else {
    status = HandleMmioX86(mem, trap_key, &inst);
    // If there was an attempt to read or test memory, update the GPRs.
    do_write = inst.type == INST_MOV_READ || inst.type == INST_TEST;
  }
#endif  // __x86_64__

  if (status == ZX_OK && do_write) {
    return WriteState(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
  }

  return status;
}

#if __x86_64__
zx_status_t Vcpu::HandleInput(const zx_packet_guest_io_t& io,
                              uint64_t trap_key) {
  IoValue value = {};
  value.access_size = io.access_size;
  zx_status_t status = IoMapping::FromPortKey(trap_key)->Read(io.port, &value);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to handle port in 0x" << std::hex << io.port
                   << ": " << std::dec << status;
    return status;
  }

  zx_vcpu_io_t vcpu_io;
  memset(&vcpu_io, 0, sizeof(vcpu_io));
  vcpu_io.access_size = value.access_size;
  vcpu_io.u32 = value.u32;
  if (vcpu_io.access_size != io.access_size) {
    FXL_LOG(ERROR) << "Unexpected size (" << vcpu_io.access_size
                   << " != " << io.access_size << ") for port in 0x" << std::hex
                   << io.port;
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return WriteState(ZX_VCPU_IO, &vcpu_io, sizeof(vcpu_io));
}

zx_status_t Vcpu::HandleOutput(const zx_packet_guest_io_t& io,
                               uint64_t trap_key) {
  IoValue value;
  value.access_size = io.access_size;
  value.u32 = io.u32;
  zx_status_t status = IoMapping::FromPortKey(trap_key)->Write(io.port, value);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to handle port out 0x" << std::hex << io.port
                   << ": " << std::dec << status;
  }
  return status;
}

zx_status_t Vcpu::HandleIo(const zx_packet_guest_io_t& io, uint64_t trap_key) {
  return io.input ? HandleInput(io, trap_key) : HandleOutput(io, trap_key);
}
#endif  // __x86_64__

zx_status_t Vcpu::HandleVcpu(const zx_packet_guest_vcpu_t& packet,
                             uint64_t trap_key) {
  switch (packet.type) {
    case ZX_PKT_GUEST_VCPU_VHM_REQ: {
      zx_vcpu_state_t vcpu_state;
      zx_status_t status =
          ReadState(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
      if (unlikely(status != ZX_OK)) {
        return status;
      }

      status = guest_->HandleNebulaHyperCall(&vcpu_state);
      if (unlikely(status != ZX_OK)) {
        return status;
      }

      return WriteState(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    }

    case ZX_PKT_GUEST_VCPU_INTERRUPT:
      return guest_->SignalInterrupt(packet.interrupt.mask,
                                     packet.interrupt.vector);
    case ZX_PKT_GUEST_VCPU_STARTUP:
      if (id_ != 0) {
        FXL_LOG(ERROR)
            << "Secondary processors must be started by the primary processor";
        return ZX_ERR_BAD_STATE;
      }
      FXL_LOG(ERROR) << "HandleVcpu VCPU STARTUP startup.id is " << packet.startup.id;
      return guest_->StartVcpu(packet.startup.entry, packet.startup.id);
    case ZX_PKT_GUEST_VCPU_STOP:
      FXL_LOG(WARNING) << "event=guest_vcpu_stop_packet vcpu=" << id_
                       << " stop_reason=guest_reboot_or_stop";
      if (guest_->get_watchdog())
        guest_->get_watchdog()->DumpState(1);
      guest_->Stop(ZX_ERR_CANCELED);
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

void Vcpu::Dump(std::string& state) {
  zx_vcpu_dump_state(vcpu_, const_cast<char*>(state.data()), state.size());
}

zx_status_t Vcpu::SetTimeSlice(uint64_t time_slice) {
  int rc = NO_ERROR;
  zx_status_t result = ZX_OK;
  struct vcpu_set_time_slice_req req;
  req.vcpu_handle = vcpu_;
  req.time_slice = time_slice;

  rc = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_VCPU_TIMESLICE,
                     (void*)&req);
  if (rc) {
    result = ZX_ERR_BAD_HANDLE;
  }

  return result;
}

zx_status_t Vcpu::Stop() {
  zx_vcpu_stop(vcpu_);
  return ZX_OK;
}

uint64_t Vcpu::GetTimeSlice() {
  int rc = NO_ERROR;
  uint64_t result = 0;
  struct vcpu_get_time_slice_cmd cmd;
  cmd.req.vcpu_handle = vcpu_;

  rc = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_VCPU_TIMESLICE,
                     (void*)&cmd);
  if (rc) {
    result = 0;
  } else {
    result = cmd.rsp.time_slice;
  }

  return result;
}

zx_status_t Vcpu::SetCvmDumpEnable(uint64_t value) {
  uint64_t result = NO_ERROR;
  struct config_vcpu_cvmdump_req req;
  req.vcpu_handle = vcpu_;
  req.cmd = value;

  result = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_CONFIG_VCPU_DUMP,
      (void*) &req);

  return result;
}

zx_status_t Vcpu::SetGroup(int group) {
  int rc = NO_ERROR;
  zx_status_t result = ZX_OK;
  struct config_vcpu_group_req req;

  if (group >= 0) {
    req.vcpu_handle = vcpu_;
    req.cmd = VCPU_GROUP_CMD_ADD;
    req.group = group;
  } else {
    req.vcpu_handle = vcpu_;
    req.cmd = VCPU_GROUP_CMD_REMOVE;
    req.group = group;
  }

  rc = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_CONFIG_VCPU_GROUP,
                     (void*)&req);
  if (rc) {
    result = ZX_ERR_BAD_HANDLE;
  }

  return result;
}

zx_status_t Vcpu::SetCvmDump() {
  int rc = NO_ERROR;
  zx_status_t result = ZX_OK;
  struct config_vcpu_cvmdump_req req;

  req.vcpu_handle = vcpu_;
  req.cmd = VCPU_CVMD_CMD_ADD;

  rc = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_CONFIG_VCPU_DUMP,
      (void*) &req);
  if (rc) {
    result = ZX_ERR_BAD_HANDLE;
  }

  return result;
}

zx_status_t Vcpu::SetBudget(uint64_t budget) {
  int rc = NO_ERROR;
  zx_status_t result = ZX_OK;
  struct sched_param_req req;

  req.type = SCHED_PARAM_TYPE_BUDGET;
  req.budget_info.vmid = 0;
  req.budget_info.vcpuid = 0;
  req.budget_info.vcpu_handle = vcpu_;
  req.budget_info.budget = budget;

  rc =
      _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM, (void*)&req);
  if (rc) {
    result = ZX_ERR_BAD_HANDLE;
  }

  return result;
}

zx_status_t Vcpu::SetPriority(uint32_t priority) {
  int rc = NO_ERROR;
  zx_status_t result = ZX_OK;
  struct sched_param_req req;

  req.type = SCHED_PARAM_TYPE_VCPU_PRIORITY;
  req.vcpu_priority_info.vcpu_handle = vcpu_;
  req.vcpu_priority_info.priority = priority;
  rc = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM, (void*)&req);
  if (rc) {
    result = ZX_ERR_BAD_HANDLE;
  }

  return result;
}

void Vcpu::DumpGroups() {
  struct config_vcpu_group_req req;
  req.vcpu_handle = ZX_HANDLE_INVALID;
  req.cmd = VCPU_GROUP_CMD_DUMP;
  req.group = 0;

  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_CONFIG_VCPU_GROUP, (void*)&req);
}

}  // namespace machina
