// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mmio_bus.h"

#include "virtio_device.h"

#include "lib/fxl/logging.h"

namespace machina {
zx_status_t MmioBus::Connect(MmioDevice* device) {
  if (next_open_slot_ >= MMIO_MAX_DEVICES
      || next_open_slot_ >= mmio_irqs_.size ())
    return ZX_ERR_OUT_OF_RANGE;

  ZX_DEBUG_ASSERT(device_[next_open_slot_] == nullptr);
  device->global_irq_ = mmio_irqs_[next_open_slot_];
  printf("mmio dev irq: %u\n", device->global_irq_);

  device->mmio_base_ = next_mmio_addr_;
  next_mmio_addr_ += (kMmioSize * 2);

  size_t slot = next_open_slot_++;
  device_[slot] = device;

  device->bus_ = this;
  FXL_LOG(INFO) << "MmioBus connect slot: "<<slot;

  FXL_LOG(INFO) << "virtio_mmio_dev:" << device->virtio_device()->device_id()
            << " addr = 0x" << std::hex << device->base()
            << ", size = 0x" << VIRTIO_MMIO_QUEUE_NOTIFY;
  zx_status_t result = guest_->CreateMapping(machina::TrapType::MMIO_SYNC, device->base(),
                                  VIRTIO_MMIO_QUEUE_NOTIFY, 0, device);
  if (result != ZX_OK) {
    FXL_LOG(ERROR) << "failed to set trap for mmio_device@" << slot << " base cfg";
  }

  FXL_LOG(INFO) << "virtio_mmio_dev:" << device->virtio_device()->device_id()
            << " addr = 0x" << std::hex
            << device->base() + VIRTIO_MMIO_QUEUE_NOTIFY << ", size = 0x"
            << VIRTIO_MMIO_INTERRUPT_STATUS - VIRTIO_MMIO_QUEUE_NOTIFY;
  result = guest_->CreateMapping(
      machina::TrapType::MMIO_BELL, device->base() + VIRTIO_MMIO_QUEUE_NOTIFY,
      VIRTIO_MMIO_INTERRUPT_STATUS - VIRTIO_MMIO_QUEUE_NOTIFY,
      VIRTIO_MMIO_QUEUE_NOTIFY, device);
  if (result != ZX_OK) {
    FXL_LOG(ERROR) << "failed to set trap for mmio_device@" << slot << " queue notify";
  }

  FXL_LOG(INFO) << "virtio_mmio_dev:" << device->virtio_device()->device_id()
            << " addr = 0x" << std::hex
            << device->base() + VIRTIO_MMIO_INTERRUPT_STATUS << ", size = 0x"
            << VIRTIO_MMIO_CONFIG_END - VIRTIO_MMIO_INTERRUPT_STATUS;
  result = guest_->CreateMapping(
      machina::TrapType::MMIO_SYNC,
      device->base() + VIRTIO_MMIO_INTERRUPT_STATUS,
      VIRTIO_MMIO_CONFIG_END - VIRTIO_MMIO_INTERRUPT_STATUS,
      VIRTIO_MMIO_INTERRUPT_STATUS, device);
  if (result != ZX_OK) {
    FXL_LOG(ERROR) << "failed to set trap for mmio_device@" << slot << " irq status";
  }

  FXL_LOG(INFO) << "virtio_mmio_dev:" << device->virtio_device()->device_id()
            << " addr = 0x" << std::hex << device->base() + machina::kMmioSize
            << ", size = 0x" << machina::kMmioSize;
  result = guest_->CreateMapping(machina::TrapType::MMIO_BELL,
                                  device->base() + machina::kMmioSize,
                                  machina::kMmioSize, 0, device);
  if (result != ZX_OK) {
    FXL_LOG(ERROR) << "failed to set trap for mmio_device@" << slot << " rest";
  }
  return ZX_OK;
}

zx_status_t MmioBus::Interrupt(MmioDevice& device) {
  return interrupt_controller_->Interrupt(device.global_irq_);
}
}  // namespace machina
