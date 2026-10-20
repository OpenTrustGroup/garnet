// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_mmio.h"

#include <virtio/virtio_ring.h>

#include <fbl/auto_lock.h>

#include "garnet/lib/machina/mmio_bus.h"
#include "garnet/lib/machina/virtio_device.h"
#include "lib/fxl/logging.h"

namespace machina {

constexpr uint32_t kVirtioMmioVersion = 2;

MmioDevice::MmioDevice(VirtioDevice* device) : device_(device) {}

zx_status_t MmioDevice::Read(uint64_t addr, IoValue* value) const {
  switch (addr) {
    case VIRTIO_MMIO_MAGIC_VALUE: {
      value->u32 = ('v' | 'i' << 8 | 'r' << 16 | 't' << 24);
      return ZX_OK;
    }

    case VIRTIO_MMIO_VERSION: {
      value->u32 = kVirtioMmioVersion;
      return ZX_OK;
    }

    case VIRTIO_MMIO_DEVICE_ID: {
      fbl::AutoLock lock(&device_->mutex_);
      value->u32 = device_->device_id_;
      return ZX_OK;
    }

    case VIRTIO_MMIO_VENDOR_ID: {
      value->u32 = 0x1fa4;
      return ZX_OK;
    }

    case VIRTIO_MMIO_DEVICE_FEATURES: {
      fbl::AutoLock lock(&device_->mutex_);
      if (device_->features_sel_ == 1) {
        value->u32 = 1;
        return ZX_OK;
      }
      value->u32 = device_->features_sel_ > 0 ? 0 : device_->features_;
      return ZX_OK;
    }

    case VIRTIO_MMIO_INTERRUPT_STATUS: {
      fbl::AutoLock lock(&device_->mutex_);
      value->u32 = device_->isr_status_;

      // This is a workaround. For vhost devices, virtio irq is injected directly from host kernel
      // via irqfd, and nbl_vmm has no chance to update isr_status_. For pci devices, this issue is
      // addressed by pcie msi-X capability which provides per-queue interrupts, thus isr_status_ is
      // not needed anymore. But mmio devices don't provide this feature. Thus, we should manually
      // update isr_status_ here.
      value->u32 |= VirtioDevice::VIRTIO_ISR_QUEUE;
      return ZX_OK;
    }

    case VIRTIO_MMIO_STATUS: {
      fbl::AutoLock lock(&device_->mutex_);
      value->u32 = device_->status_;
      FXL_LOG(WARNING) << "read status " << std::hex << value->u32;
      return ZX_OK;
    }

    case VIRTIO_MMIO_QUEUE_NUM_MAX: {
      VirtioQueue* queue = selected_queue();
      if (queue == nullptr) {
        return ZX_ERR_BAD_STATE;
      }

      value->u32 = queue->size();
      FXL_LOG(WARNING) << "read queue num max " << value->u32;
      return ZX_OK;
    }

    case VIRTIO_MMIO_CONFIG_GENERATION: {
      return ZX_OK;
    }

    case VIRTIO_MMIO_QUEUE_READY: {
      VirtioQueue* queue = selected_queue();
      if (queue == nullptr) {
        return ZX_ERR_BAD_STATE;
      }

      value->u32 = queue->is_ready();
      FXL_LOG(WARNING) << "read queue ready " << value->u32;
      return ZX_OK;
    }

    case VIRTIO_MMIO_CONFIG_START ... VIRTIO_MMIO_CONFIG_END: {
      size_t device_config_top = device_->device_config_size_;
      addr -= VIRTIO_MMIO_CONFIG_START;
      if (addr < device_config_top) {
        return device_->ReadConfig(addr, value);
      }
    }
  }

  return ZX_ERR_NOT_SUPPORTED;
}

VirtioQueue* MmioDevice::selected_queue() const {
  fbl::AutoLock lock(&device_->mutex_);
  if (device_->queue_sel_ >= device_->num_queues_) {
    return nullptr;
  }
  return &device_->queues_[device_->queue_sel_];
}

static void virtio_queue_update_addr(VirtioQueue* queue) {
  queue->set_desc_addr(queue->desc_addr());
  queue->set_avail_addr(queue->avail_addr());
  queue->set_used_addr(queue->used_addr());
  queue->set_ring_idx();
}

zx_status_t MmioDevice::Write(uint64_t addr, const IoValue& value) {
  if (value.access_size != 4) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  switch (addr) {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL: {
      fbl::AutoLock lock(&device_->mutex_);
      device_->features_sel_ = value.u32;
      return ZX_OK;
    }

    case VIRTIO_MMIO_DRIVER_FEATURES: {
      FXL_LOG(WARNING) << "driver feature " << std::hex << value.u32;
      fbl::AutoLock lock(&device_->mutex_);
      if (device_->driver_features_sel_ == 0)
        device_->driver_features_ = value.u32;
      return ZX_OK;
    }

    case VIRTIO_MMIO_DRIVER_FEATURES_SEL: {
      FXL_LOG(WARNING) << "driver feature sel " << value.u32;
      fbl::AutoLock lock(&device_->mutex_);
      device_->driver_features_sel_ = value.u32;
      return ZX_OK;
    }

    case VIRTIO_MMIO_QUEUE_SEL: {
      FXL_LOG(WARNING) << "queue sel " << value.u32;
      fbl::AutoLock lock(&device_->mutex_);
      device_->queue_sel_ = value.u32;
      return ZX_OK;
    }

    case VIRTIO_MMIO_QUEUE_NUM: {
      FXL_LOG(WARNING) << "queue num " << value.u32;
      VirtioQueue* queue = selected_queue();
      if (queue == nullptr) {
        return ZX_ERR_BAD_STATE;
      }

      queue->set_size(value.u32);
      virtio_queue_update_addr(queue);
      return ZX_OK;
    }

    case VIRTIO_MMIO_QUEUE_READY: {
      FXL_LOG(WARNING) << "queue ready " << value.u32;
      VirtioQueue* queue = selected_queue();
      if (queue == nullptr) {
        return ZX_ERR_BAD_STATE;
      }

      queue->set_ready(value.u32 == 1);
      return ZX_OK;
    }

    case VIRTIO_MMIO_QUEUE_NOTIFY: {
      return device_->Kick(static_cast<uint16_t>(value.u32));
    }

    case VIRTIO_MMIO_INTERRUPT_ACK: {
      fbl::AutoLock lock(&device_->mutex_);
      device_->isr_status_ &= ~value.u32;
      return ZX_OK;
    }

    case VIRTIO_MMIO_STATUS: {
      uint8_t status;
      FXL_LOG(WARNING) << "write status " << std::hex << value.u32;
      {
        fbl::AutoLock lock(&device_->mutex_);
        status = device_->status_ = value.u32;
      }
      if (is_driver_ready() && are_queues_valid()) {
        if (driver_ready_handler_ != nullptr) {
          if (!is_drv_ready_) {
            driver_ready_handler_();
            is_drv_ready_ = true;
            FXL_LOG(INFO) << "virtio-mmio device ready, id=" << (int)device_->device_id();
          }
        }
      }
      if (status == 0) {
        if (driver_reset_handler_ != nullptr) {
          if (is_drv_ready_) {
            driver_reset_handler_();
            is_drv_ready_ = false;
            FXL_LOG(INFO) << "virtio-mmio device reset, id=" << (int)device_->device_id();
          }
        }
      }
      return ZX_OK;
    }

    case VIRTIO_MMIO_QUEUE_DESC_LOW ... VIRTIO_MMIO_QUEUE_USED_HIGH: {
      VirtioQueue* queue = selected_queue();
      if (queue == nullptr) {
        return ZX_ERR_BAD_STATE;
      }

      size_t word;
      if (addr >= VIRTIO_MMIO_QUEUE_USED_LOW) {
        word = (addr - VIRTIO_MMIO_QUEUE_USED_LOW) / sizeof(uint32_t);
        word += 4;
      } else if (addr >= VIRTIO_MMIO_QUEUE_AVAIL_LOW) {
        word = (addr - VIRTIO_MMIO_QUEUE_AVAIL_LOW) / sizeof(uint32_t);
        word += 2;
      } else {
        word = (addr - VIRTIO_MMIO_QUEUE_DESC_LOW) / sizeof(uint32_t);
      }

      queue->UpdateRing<void>(
          [&value, word](virtio_queue_t* ring) { ring->addr.words[word] = value.u32; });
      virtio_queue_update_addr(queue);
      return ZX_OK;
    }

    case VIRTIO_MMIO_CONFIG_START ... VIRTIO_MMIO_CONFIG_END: {
      size_t device_config_top = device_->device_config_size_;
      addr -= VIRTIO_MMIO_CONFIG_START;
      if (addr < device_config_top) {
        return device_->WriteConfig(addr, value);
      }
    }

    // For LK2 compatibility
    case VIRTIO_MMIO_GUEST_PAGE_SIZE: {
      FXL_LOG(WARNING) << "guest page size " << std::hex << value.u32;
      return ZX_OK;
    }
    case VIRTIO_MMIO_QUEUE_ALIGN: {
      FXL_LOG(WARNING) << "queue align " << std::hex << value.u32;
      return ZX_OK;
    }
    case VIRTIO_MMIO_QUEUE_PFN: {
      VirtioQueue* queue = selected_queue();
      if (queue == nullptr) {
        return ZX_ERR_BAD_STATE;
      }

      uint64_t guest_pa = (uint64_t)value.u32 * PAGE_SIZE;
      auto queue_size = queue->size();
      queue->UpdateRing<void>([queue_size, &guest_pa](virtio_queue_t* ring) {
        vring vring;
        vring_init(&vring, queue_size, (void*)guest_pa, PAGE_SIZE);

        ring->addr.desc = (uint64_t)vring.desc;
        ring->addr.avail = (uint64_t)vring.avail;
        ring->addr.used = (uint64_t)vring.used;
        FXL_LOG(WARNING) << std::hex << "queue desc " << ring->addr.desc << ", queue avail "
                   << ring->addr.avail << ", queue used " << ring->addr.used;
      });
      virtio_queue_update_addr(queue);
      queue->set_ready(true);
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_SUPPORTED;
}

void MmioDevice::RegisterDriverReadyHandler(DriverReadyHandler handler) {
  driver_ready_handler_ = std::move(handler);
}

void MmioDevice::RegisterDriverResetHandler(DriverResetHandler handler) {
  driver_reset_handler_ = std::move(handler);
}

bool MmioDevice::is_driver_ready() const {
  fbl::AutoLock lock(&device_->mutex_);
  auto expected_status =
      kDeviceStatusAcked | kDeviceStatusDriver | kDeviceStatusDriverOk | kDeviceStatusFeatureOk;
  return expected_status == device_->status_;
}

bool MmioDevice::are_queues_valid() const {
  bool valid = false;
  for (uint8_t i = 0; i < device_->num_queues(); i++) {
    valid = device_->queues_[i].valid();
  }
  return valid;
}

bool MmioDevice::is_selected_queues_valid(uint16_t queue_sel_) const {
  fbl::AutoLock lock(&device_->mutex_);
  if (device_->queue_sel_ >= device_->num_queues()) {
    return false;
  }
  bool valid = device_->queues_[queue_sel_].valid();
  return valid;
}

zx_status_t MmioDevice::Interrupt() { return bus_->Interrupt(*this); }
}  // namespace machina
