// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio_irq.h"

#include <fbl/auto_lock.h>
#include <vector>

#include "garnet/lib/machina/guest.h"
#include "lib/fxl/logging.h"
#include "lib/zx/vmar.h"

namespace machina {

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
static constexpr uint32_t kPageSize = 0x1000;

static constexpr uintptr_t kNormalPriority = 16;
static constexpr uintptr_t kLooperPriority = kNormalPriority + 2;
static constexpr uintptr_t kIRQPriority = kLooperPriority + 2;

// static
void AudioIrq::Create(zx::resource& res,
                      const AudioIrqSpecEx& spec,
                      Guest* guest,
                      std::unique_ptr<AudioIrq>* out) {
  zx::interrupt interrupt;
  auto status = zx::interrupt::create(res, 0, &interrupt);
  FXL_CHECK(status == ZX_OK);

  status = interrupt.bind(/*slot=*/0, res, spec.vector,
                          ZX_INTERRUPT_MODE_LEVEL_HIGH);
  FXL_CHECK(status == ZX_OK);

  zx::vmo vmo;
  status = zx_vmo_create_physical(res.get(), spec.ctrl_reg_base, kPageSize,
                                  vmo.reset_and_get_address());
  FXL_CHECK(status == ZX_OK);

  status = vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE);
  FXL_CHECK(status == ZX_OK);

  auto audio_irq = std::make_unique<AudioIrq>(std::move(interrupt),
                                              std::move(vmo), spec, guest);
  FXL_CHECK(audio_irq != nullptr);

  *out = std::move(audio_irq);
}
// static
void AudioIrq::Create(uint16_t vector,
                      Guest* guest,
                      std::unique_ptr<AudioIrq>* out) {
  auto audio_irq = std::make_unique<AudioIrq>(vector, guest);
  FXL_CHECK(audio_irq != nullptr);

  *out = std::move(audio_irq);
}

AudioIrq::AudioIrq(zx::interrupt interrupt,
                   zx::vmo vmo,
                   const AudioIrqSpecEx& spec,
                   Guest* guest)
    : interrupt_(std::move(interrupt)),
      ctrl_regs_vmo_(std::move(vmo)),
      spec_(spec),
      guest_(guest) {
  zx_status_t status =
      zx::vmar::root_self().map(0, ctrl_regs_vmo_, 0, PAGE_SIZE, kMapFlags,
                                (uintptr_t*)&mapped_ctrl_regs_);
  FXL_CHECK(status == ZX_OK);

  thread_ = std::make_unique<std::thread>([this] {
    update_afe_irq_regs_map();
    zx_thread_set_priority(kIRQPriority);
    while (!should_stop_) {
      uint64_t slots;
      interrupt_.wait(&slots);
      // interrupt_.mask_interrupt(spec_.vector);
      {
        fbl::AutoLock lock(&mutex_);
        for (auto audio_reg : spec_.audio_regs) {
          auto irq_status = reg_read32(audio_reg.stats_offset);

          if (irq_status == 0) {
            continue;
          }

          for (auto audio_vm_pair : audio_reg.audio_vm_status) {
            if (irq_status & audio_vm_pair.second.interested_bitmask) {
              vm_irq_status_[audio_vm_pair.first] =
                  irq_status & audio_vm_pair.second.interested_bitmask;
              if (audio_vm_pair.first == 0xffffffff) {
                for (auto clear_reg : audio_vm_pair.second.clear_regs) {
                  if (clear_reg.clear_reg_mask > 0) {
                    // printf("======> %s %d clear mask:%x, status: %x\n",
                    // __func__, __LINE__, clear_reg.clear_reg_mask ,
                    // vm_irq_status_[audio_vm_pair.first]);
                    if (vm_irq_status_[audio_vm_pair.first] &
                        (0x1 << GetAfeIRQIndex(clear_reg.clear_offset))) {
                      // printf("======> %s %d clear offset: %x\n", __func__,
                      // __LINE__, clear_reg.clear_offset);
                      auto tmp_reg = reg_read32(clear_reg.clear_offset);
                      reg_write32(clear_reg.clear_offset,
                                  tmp_reg ^ clear_reg.clear_reg_mask);
                    }
                  } else {
                    reg_write32(clear_reg.clear_offset,
                                vm_irq_status_[audio_vm_pair.first]);
                  }
                }
                HostTriggerVirqLocked();
              } else {
                for (auto clear_reg : audio_vm_pair.second.clear_regs) {
                  if (clear_reg.clear_reg_mask > 0) {
                    // printf("======> %s %d clear mask:%x, status: %x\n",
                    // __func__, __LINE__, clear_reg.clear_reg_mask ,
                    // vm_irq_status_[audio_vm_pair.first]);
                    if (vm_irq_status_[audio_vm_pair.first] &
                        (0x1 << GetAfeIRQIndex(clear_reg.clear_offset))) {
                      // printf("======> %s %d clear offset: %x\n", __func__,
                      // __LINE__, clear_reg.clear_offset);
                      auto tmp_reg = reg_read32(clear_reg.clear_offset);
                      reg_write32(clear_reg.clear_offset,
                                  tmp_reg ^ clear_reg.clear_reg_mask);
                    }
                  } else {
                    reg_write32(clear_reg.clear_offset,
                                vm_irq_status_[audio_vm_pair.first]);
                  }
                }
                GuestTriggerVirqLocked(vm_irq_status_[audio_vm_pair.first],
                                       audio_vm_pair.first);
              }
            }
          }
        }
      }
    }
  });
}

AudioIrq::AudioIrq(uint16_t vector, Guest* guest) : guest_(guest) {
  spec_.vector = vector;
}

AudioIrq::~AudioIrq() {
  if (mapped_ctrl_regs_)
    zx::vmar::root_self().unmap((uintptr_t)mapped_ctrl_regs_, PAGE_SIZE);
}

uint32_t AudioIrq::GetIrqStatus(uint32_t vmid) {
  fbl::AutoLock lock(&mutex_);
  uint32_t status = vm_irq_status_[vmid];
  vm_irq_status_[vmid] = 0;
  return status;
}

uint32_t AudioIrq::GetAfeIRQIndex(uint32_t clear_reg) {
  uint32_t result = 0;
  for (auto reg : spec_.regs) {
    if (reg.irq_clear_offset == clear_reg) {
      break;
    }
    result++;
  }
  return result;
}

void AudioIrq::AppendIrqStatus(uint32_t vmid, uint64_t bitmask) {
  fbl::AutoLock lock(&mutex_);
  vm_irq_status_[vmid] |= bitmask;
  uint8_t next_vcpu = 3;
  next_vcpu = next_vcpu % guest_->cpu_nums();
  guest_->SignalInterrupt(1 << next_vcpu, spec_.vector);
}

void AudioIrq::ClearIrqStatus(uint32_t vmid, uint32_t bitmask) {
  fbl::AutoLock lock(&mutex_);
  vm_irq_status_[vmid] &= ~bitmask;
  // interrupt_.unmask_interrupt(spec_.vector);
  // MaybeTriggerVirqLocked(false);
}

void AudioIrq::HostTriggerVirqLocked() {
  uint8_t next_vcpu = 0;
  next_vcpu = next_vcpu % guest_->cpu_nums();
  guest_->SignalInterrupt(1 << next_vcpu, spec_.vector);
}

void AudioIrq::GuestTriggerVirqLocked(uint32_t bitmask, uint32_t vmid) {
  guest_->ForwardAudioIrqEvent(spec_.vector, bitmask, vmid);
}

uint32_t AudioIrq::reg_read32(uint32_t offset) {
  return *(uint32_t volatile*)(mapped_ctrl_regs_ + offset);
}

void AudioIrq::reg_write32(uint32_t offset, uint32_t bitmask) {
  *(uint32_t volatile*)(mapped_ctrl_regs_ + offset) = bitmask;
}

void AudioIrq::set_afe_irq_spec(uint32_t irq_status_offset,
                                uint32_t irq_clear_offset,
                                uint64_t bitmask,
                                uint32_t vmid,
                                uint32_t index) {
  fbl::AutoLock lock(&mutex_);
  spec_.regs[index].irq_status_offset = irq_status_offset;
  spec_.regs[index].irq_clear_offset = irq_clear_offset;
  spec_.regs[index].irq_regs_bit = bitmask;
  spec_.regs[index].interested_bitmask =
      (0x1 << (0x000000000000001F & bitmask));
  spec_.regs[index].vmid = vmid;
}

void AudioIrq::update_afe_irq_regs_map() {
  fbl::AutoLock lock(&mutex_);
  spec_.audio_regs.clear();
  for (auto reg : spec_.regs) {
    std::vector<AudioAfeRegSpec>::iterator it = spec_.audio_regs.begin();
    while (it != spec_.audio_regs.end()) {
      if (it->stats_offset == reg.irq_status_offset) {
        break;
      }
      ++it;
    }

    if (it == spec_.audio_regs.end()) {
      machina::AudioAfeRegSpec audio_afe_reg_spec;
      audio_afe_reg_spec.stats_offset = reg.irq_status_offset;
      machina::AudioVMStatusBitmaskSpec audio_vm_status_bitmask_spec;
      audio_vm_status_bitmask_spec.interested_bitmask =
          (0x1 << (0x000000000000001F & reg.irq_regs_bit));
      audio_vm_status_bitmask_spec.status = 0;
      machina::AudioAfeClearRegSpec audio_afe_clear_reg_spec;
      audio_afe_clear_reg_spec.clear_offset = reg.irq_clear_offset;
      audio_afe_clear_reg_spec.clear_bitmask =
          (0x1 << (((0x00000000000003E0 & reg.irq_regs_bit) >> 5)));
      audio_afe_clear_reg_spec.clear_reg_mask =
          (0xFF00000000000000 & reg.irq_regs_bit) >> 32;
      audio_vm_status_bitmask_spec.clear_regs.push_back(
          audio_afe_clear_reg_spec);
      audio_afe_reg_spec.audio_vm_status.emplace(reg.vmid,
                                                 audio_vm_status_bitmask_spec);
      spec_.audio_regs.push_back(audio_afe_reg_spec);
    } else {
      auto it_vmid = it->audio_vm_status.find(reg.vmid);
      if (it_vmid != it->audio_vm_status.end()) {
        it_vmid->second.interested_bitmask |=
            (0x1 << (0x000000000000001F & reg.irq_regs_bit));
        auto it_clear = it_vmid->second.clear_regs.begin();
        while (it_clear != it_vmid->second.clear_regs.end()) {
          if (it_clear->clear_offset == reg.irq_clear_offset) {
            break;
          }
          ++it_clear;
        }

        if (it_clear != it_vmid->second.clear_regs.end()) {
          it_clear->clear_bitmask |=
              (0x1 << (((0x00000000000003E0 & reg.irq_regs_bit) >> 5)));
        } else {
          machina::AudioAfeClearRegSpec audio_afe_clear_reg_spec;
          audio_afe_clear_reg_spec.clear_offset = reg.irq_clear_offset;
          audio_afe_clear_reg_spec.clear_bitmask =
              (0x1 << (((0x00000000000003E0 & reg.irq_regs_bit) >> 5)));
          audio_afe_clear_reg_spec.clear_reg_mask =
              (0xFF00000000000000 & reg.irq_regs_bit) >> 32;
          it_vmid->second.clear_regs.push_back(audio_afe_clear_reg_spec);
        }
      } else {
        machina::AudioVMStatusBitmaskSpec audio_vm_status_bitmask_spec;
        audio_vm_status_bitmask_spec.interested_bitmask =
            (0x1 << (0x000000000000001F & reg.irq_regs_bit));
        audio_vm_status_bitmask_spec.status = 0;
        machina::AudioAfeClearRegSpec audio_afe_clear_reg_spec;
        audio_afe_clear_reg_spec.clear_offset = reg.irq_clear_offset;
        audio_afe_clear_reg_spec.clear_bitmask =
            (0x1 << (((0x00000000000003E0 & reg.irq_regs_bit) >> 5)));
        audio_afe_clear_reg_spec.clear_reg_mask =
            (0xFF00000000000000 & reg.irq_regs_bit) >> 32;
        audio_vm_status_bitmask_spec.clear_regs.push_back(
            audio_afe_clear_reg_spec);
        it->audio_vm_status.emplace(reg.vmid, audio_vm_status_bitmask_spec);
      }
    }
  }

  // for (auto audio_reg : spec_.audio_regs){
  //   for (auto audio_vm_pair : audio_reg.audio_vm_status) {
  //     for (auto clear_reg : audio_vm_pair.second.clear_regs) {
  //       printf("======> %s line %d status: %x, vm: %d, status bitmask: %x,
  //       clear: %x, clear bitmask:%x \n", __func__, __LINE__,
  //       audio_reg.stats_offset, audio_vm_pair.first,
  //       audio_vm_pair.second.interested_bitmask, clear_reg.clear_offset,
  //       clear_reg.clear_bitmask);
  //     }
  //   }
  // }
}
}  // namespace machina
