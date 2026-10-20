// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared_irq.h"

#include <fbl/auto_lock.h>

#include "garnet/lib/machina/guest.h"
#include "lib/fxl/logging.h"
#include "lib/zx/vmar.h"

namespace machina {

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
static constexpr uint32_t kPageSize = 0x1000;

// static
void SharedIrq::Create(zx::resource& res,
                       const SharedIrqSpec& spec,
                       Guest* guest,
                       std::unique_ptr<SharedIrq>* out) {
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

  auto shared_irq = std::make_unique<SharedIrq>(std::move(interrupt),
                                                std::move(vmo), spec, guest);
  FXL_CHECK(shared_irq != nullptr);

  *out = std::move(shared_irq);
}

// static
void SharedIrq::Create(uint16_t vector,
                       uint32_t interested_bitmask,
                       Guest* guest,
                       std::unique_ptr<SharedIrq>* out) {
  auto shared_irq =
      std::make_unique<SharedIrq>(vector, interested_bitmask, guest);
  FXL_CHECK(shared_irq != nullptr);

  *out = std::move(shared_irq);
}

SharedIrq::SharedIrq(zx::interrupt interrupt,
                     zx::vmo vmo,
                     const SharedIrqSpec& spec,
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
    while (!should_stop_) {
      uint64_t slots;
      interrupt_.wait(&slots);

      {
        fbl::AutoLock lock(&mutex_);
        auto irq_status = reg_read32(spec_.irq_status_offset);
        irq_status_ |= irq_status;
        reg_write32(spec_.irq_clear_offset, irq_status);

        MaybeTriggerVirqLocked();
        MaybeForwardIrqEventLocked();
      }
    }
  });
}

SharedIrq::SharedIrq(uint16_t vector, uint32_t interested_bitmask, Guest* guest)
    : guest_(guest) {
  spec_.vector = vector;
  spec_.interested_bitmask = interested_bitmask;
}

SharedIrq::~SharedIrq() {
  if (mapped_ctrl_regs_)
    zx::vmar::root_self().unmap((uintptr_t)mapped_ctrl_regs_, PAGE_SIZE);
}

uint32_t SharedIrq::GetIrqStatus() {
  fbl::AutoLock lock(&mutex_);
  return irq_status_;
}

void SharedIrq::AppendIrqStatus(uint32_t bitmask) {
  fbl::AutoLock lock(&mutex_);
  irq_status_ |= bitmask;
  MaybeTriggerVirqLocked(false);
}

void SharedIrq::ClearIrqStatus(uint32_t bitmask) {
  fbl::AutoLock lock(&mutex_);
  irq_status_ &= ~bitmask;
  MaybeTriggerVirqLocked(false);
}

void SharedIrq::MaybeTriggerVirqLocked(bool bHost) {
  if (irq_status_ & spec_.interested_bitmask) {
    // TODO: currently sent virq to vcpu0 only,
    // should be balanced to all vcpus
    if (bHost) {
      guest_->SignalInterrupt(/*cpumask=*/0x10, spec_.vector);
    } else {
      guest_->SignalInterrupt(/*cpumask=*/0x20, spec_.vector);
    }
  }
}

void SharedIrq::MaybeForwardIrqEventLocked() {
  // For SOS non-interested irq events, which is assumed to be interested by
  // UOS, thus should be forwarded to UOS
  auto non_interested = ~spec_.interested_bitmask;
  if (irq_status_ & non_interested) {
    guest_->ForwardIrqEvent(spec_.vector, irq_status_ & non_interested);
    irq_status_ &= ~non_interested;
  }
}

uint32_t SharedIrq::reg_read32(uint32_t offset) {
  return *(uint32_t volatile*)(mapped_ctrl_regs_ + offset);
}

void SharedIrq::reg_write32(uint32_t offset, uint32_t bitmask) {
  *(uint32_t volatile*)(mapped_ctrl_regs_ + offset) = bitmask;
}

void SharedIrq::set_interest_bitmap(uint32_t bitmask) {
  fbl::AutoLock lock(&mutex_);
  spec_.interested_bitmask = bitmask;
  FXL_LOG(INFO) << "shared_irq " << spec_.vector << ": interested bitmask=0x"
                << std::hex << spec_.interested_bitmask;
}

}  // namespace machina
