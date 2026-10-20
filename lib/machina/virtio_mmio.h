// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include <functional>
#include <memory>

#include <virtio/virtio.h>

#include "interrupt_controller.h"
#include "io.h"

/* Magic value ("virt" string) - Read Only */
#define VIRTIO_MMIO_MAGIC_VALUE 0x000

/* Virtio device version - Read Only */
#define VIRTIO_MMIO_VERSION 0x004

/* Virtio device ID - Read Only */
#define VIRTIO_MMIO_DEVICE_ID 0x008

/* Virtio vendor ID - Read Only */
#define VIRTIO_MMIO_VENDOR_ID 0x00c

/* Bitmask of the features supported by the device (host)
 * (32 bits per set) - Read Only */
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010

/* Device (host) features set selector - Write Only */
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014

/* Bitmask of features activated by the driver (guest)
 * (32 bits per set) - Write Only */
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020

/* Activated features set selector - Write Only */
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024

/* Legacy - Write Only */
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028

/* Queue selector - Write Only */
#define VIRTIO_MMIO_QUEUE_SEL 0x030

/* Maximum size of the currently selected queue - Read Only */
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034

/* Queue size for the currently selected queue - Write Only */
#define VIRTIO_MMIO_QUEUE_NUM 0x038

/* Legacy - Write Only */
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c

/* Legacy - Write Only */
#define VIRTIO_MMIO_QUEUE_PFN 0x040

/* Ready bit for the currently selected queue - Read Write */
#define VIRTIO_MMIO_QUEUE_READY 0x044

/* Queue notifier - Write Only */
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050

/* Interrupt status - Read Only */
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060

/* Interrupt acknowledge - Write Only */
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064

/* Device status register - Read Write */
#define VIRTIO_MMIO_STATUS 0x070

/* Selected queue's Descriptor Table address, 64 bits in two halves */
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084

/* Selected queue's Available Ring address, 64 bits in two halves */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094

/* Selected queue's Used Ring address, 64 bits in two halves */
#define VIRTIO_MMIO_QUEUE_USED_LOW 0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4

/* Configuration atomicity value */
#define VIRTIO_MMIO_CONFIG_GENERATION 0x0fc

/* The config space is defined by each driver as
 * the per-driver configuration space - Read Write */
#define VIRTIO_MMIO_CONFIG_START 0x100
#define VIRTIO_MMIO_CONFIG_END 0x1000

/*
 * Interrupt flags (re: interrupt status & acknowledge registers)
 */
#define VIRTIO_MMIO_INT_VRING (1 << 0)
#define VIRTIO_MMIO_INT_CONFIG (1 << 1)

namespace machina {

class VirtioDevice;
class VirtioQueue;
class MmioBus;

class MmioDevice : public IoHandler {
 public:
  using DriverReadyHandler = std::function<void()>;
  using DriverResetHandler = std::function<void()>;
  MmioDevice(VirtioDevice* device);

  void RegisterDriverReadyHandler(DriverReadyHandler handler);
  void RegisterDriverResetHandler(DriverResetHandler handler);
  uint64_t base() const { return mmio_base_; }
  uint32_t global_irq() const { return global_irq_; };
  zx_status_t Interrupt();
  VirtioDevice* virtio_device() const { return device_; }

 private:
  friend class MmioBus;
  zx_status_t Read(uint64_t addr, IoValue* value) const;
  zx_status_t Write(uint64_t addr, const IoValue& value);

  VirtioQueue* selected_queue() const;

  bool is_driver_ready() const;
  bool are_queues_valid() const;
  bool is_selected_queues_valid(uint16_t queue_sel_) const;


  DriverReadyHandler driver_ready_handler_;
  DriverResetHandler driver_reset_handler_;
  VirtioDevice* device_;
  MmioBus *bus_;

  bool is_drv_ready_ = false;
  // bool is_vhost_enabled_ = false;

  uint64_t mmio_base_;
  uint32_t global_irq_;
};

}  // namespace machina
