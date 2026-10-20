// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_
#define GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <virtio/virtio.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "virtio_pci.h"
#include "virtio_mmio.h"
#include "virtio_queue.h"

namespace machina {

static constexpr size_t kDeviceStatusReset = 0x0;
static constexpr size_t kDeviceStatusAcked = 0x01;
static constexpr size_t kDeviceStatusDriver = 0x02;
static constexpr size_t kDeviceStatusDriverOk = 0x04;
static constexpr size_t kDeviceStatusFeatureOk = 0x08;
static constexpr size_t kDeviceStatusFailed = 0x80;

// Set of features that are supported by the bus transparently for all devices.
static constexpr uint32_t kVirtioBusFeatures = 1u << VIRTIO_F_RING_EVENT_IDX;

class VirtioDevice;

// Interface for all virtio devices.
class VirtioDevice {
 public:
  enum Transport { PCI, MMIO };
  virtual ~VirtioDevice(){}

  // Read a device-specific configuration field.
  virtual zx_status_t ReadConfig(uint64_t addr, IoValue* value) = 0;

  // Write a device-specific configuration field.
  virtual zx_status_t WriteConfig(uint64_t addr, const IoValue& value) = 0;

  // Handle notify events for one of this devices queues.
  virtual zx_status_t HandleQueueNotify(uint16_t queue_sel) { return ZX_OK; }

  // Send a notification back to the guest that there are new descriptors in
  // then used ring.
  //
  // The method for how this notification is delievered is transport
  // specific.
  zx_status_t NotifyGuest();
  zx_status_t NotifyGuest(uint32_t queue_index);

  // Alloc msix interrupt for virtio queue and configuration.
  virtual zx_status_t AllocMsixInterrupt(uint16_t index, uint32_t irq) { return ZX_OK; }

  virtual void TriggerMsixInterrupt(uint16_t index) {
    pci_->Interrupt(index);
  }

  virtual void ControlNotify(MsixStatus behavior, uint16_t index = 0) {}

  const PhysMem& phys_mem() { return phys_mem_; }
  uint16_t num_queues() const { return num_queues_; }

  // ISR flag values.
  enum IsrFlags : uint8_t {
    // Interrupt is caused by a queue.
    VIRTIO_ISR_QUEUE = 0x1,
    // Interrupt is caused by a device config change.
    VIRTIO_ISR_DEVICE = 0x2,
  };

  // Sets the given flags in the ISR register.
  void add_isr_flags(uint8_t flags) {
    fbl::AutoLock lock(&mutex_);
    isr_status_ |= flags;
  }

  // Device features.
  //
  // These are feature bits that are supported by the device. They may or
  // may not correspond to the set of feature flags that have been negotiated
  // at runtime. For negotiated features, see |has_enabled_features|.
  void add_device_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    features_ |= features;
  }
  bool has_device_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    return (features_ & features) == features;
  }

  // Returns true if the set of features have been negotiated to be enabled.
  bool has_enabled_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    return (features_ & driver_features_ & features) == features;
  }

  PciDevice* pci_device() {
    if (transport_ == Transport::PCI)
      return pci_.get();
    return NULL;
  }

  MmioDevice* mmio_device() {
    if (transport_ == Transport::PCI)
      return NULL;
    return mmio_.get();
  }

  uint16_t device_id() { return device_id_; }

  bool use_msix() const { return use_msix_; }

  virtual VirtioQueue* queue(uint16_t sel) {
    return NULL;
  }

 protected:
  VirtioDevice(uint8_t device_id,
               size_t config_size,
               VirtioQueue* queues,
               uint16_t num_queues,
               const PhysMem& phys_mem,
               Transport transport = Transport::PCI,
               bool enable_msix = true);

 private:
  // Temporarily expose our state to the PCI transport until the proper
  // accessor methods are defined.
  friend class VirtioPci;
  friend class MmioDevice;

  fbl::Mutex mutex_;

  // Handle kicks from the driver that a queue needs attention.
  zx_status_t Kick(uint16_t queue_sel);

  // Device feature bits.
  //
  // Defined in Virtio 1.0 Section 2.2.
  uint32_t features_ __TA_GUARDED(mutex_) = 0;
  uint32_t features_sel_ __TA_GUARDED(mutex_) = 0;

  // Driver feature bits.
  uint32_t driver_features_ __TA_GUARDED(mutex_) = 0;
  uint32_t driver_features_sel_ __TA_GUARDED(mutex_) = 0;

  // Virtio device id.
  const uint8_t device_id_;

  // Device status field as defined in Virtio 1.0, Section 2.1.
  uint8_t status_ __TA_GUARDED(mutex_) = 0;

  // Interrupt status register.
  uint8_t isr_status_ __TA_GUARDED(mutex_) = 0;

  // Index of the queue currently selected by the driver.
  uint16_t queue_sel_ __TA_GUARDED(mutex_) = 0;

  // Number of bytes used for this devices configuration space.
  //
  // This should cover only bytes used for the device-specific portions of
  // the configuration header, omitting any of the (transport-specific)
  // shared configuration space.
  const size_t device_config_size_ = 0;

  // Virtqueues for this device.
  VirtioQueue* const queues_ = nullptr;

  // Size of queues array.
  const uint16_t num_queues_ = 0;

  // Guest physical memory.
  const PhysMem& phys_mem_;

  // Virtio transport: PCI or MMIO.
  Transport transport_;
  // Whether to use MSI-X or legacy INTx.
  bool use_msix_ = true;
  std::unique_ptr<VirtioPci> pci_;
  std::unique_ptr<MmioDevice> mmio_;
};

template <uint16_t VIRTIO_ID,
          int NUM_QUEUES,
          typename ConfigType,
          uint16_t QUEUE_SIZE_MAX = 128>
class VirtioDeviceBase : public VirtioDevice {
 public:
  VirtioDeviceBase(const PhysMem& phys_mem, Transport transport = Transport::PCI, bool enable_msix = true)
      : VirtioDevice(VIRTIO_ID,
                     sizeof(config_),
                     queues_,
                     NUM_QUEUES,
                     phys_mem,
                     transport,
                     enable_msix) {
    // Advertise support for common/bus features.
    add_device_features(kVirtioBusFeatures);
    for (int i = 0; i < NUM_QUEUES; ++i) {
      queues_[i].set_size(QUEUE_SIZE_MAX);
      queues_[i].set_device(this);
    }
  }

  virtual char const *cfg_name(uint64_t off) const {
    return NULL;
  }

  zx_status_t ReadConfig(uint64_t addr, IoValue* value) override {
    printf("virtio device, readcfg@%lu(%s)\n", addr, cfg_name(addr));
    uint8_t const *buf = reinterpret_cast<uint8_t*>(&config_);
    buf += addr;
    fbl::AutoLock lock(&config_mutex_);
    switch (value->access_size) {
      case 1: {
        uint8_t const *buf8 = reinterpret_cast<uint8_t const *>(buf);
        value->u8 = buf8[0];
        return ZX_OK;
      }
      case 2: {
        uint16_t const *buf16 = reinterpret_cast<uint16_t const *>(buf);
        value->u16 = buf16[0];
        return ZX_OK;
      }
      case 4: {
        uint32_t const *buf32 = reinterpret_cast<uint32_t const *>(buf);
        value->u32 = buf32[0];
        return ZX_OK;
      }
      case 8: {
        uint64_t const *buf64 = reinterpret_cast<uint64_t const *>(buf);
        value->u64 = buf64[0];
        return ZX_OK;
      }
    }
    FXL_LOG(ERROR) << "Unsupported config read at 0x" << std::hex << addr <<
        ", width: " << std::dec << value->access_size;
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t WriteConfig(uint64_t addr, const IoValue& value) override {
    printf("virtio device, writecfg@%lu(%s)\n", addr, cfg_name(addr));
    uint8_t* buf = reinterpret_cast<uint8_t*>(&config_);
    buf += addr;
    fbl::AutoLock lock(&config_mutex_);
    switch (value.access_size) {
      case 1: {
        buf[0] = value.u8;
        return ZX_OK;
      }
      case 2: {
        uint16_t *buf16 = reinterpret_cast<uint16_t*>(buf);
        buf16[0] = value.u16;
        return ZX_OK;
      }
      case 4: {
        uint32_t *buf32 = reinterpret_cast<uint32_t*>(buf);
        buf32[0] = value.u32;
        return ZX_OK;
      }
      case 8: {
        uint64_t* buf64 = reinterpret_cast<uint64_t*>(buf);
        buf64[0] = value.u64;
        return ZX_OK;
      }
    }
    FXL_LOG(ERROR) << "Unsupported config write at 0x" << std::hex << addr <<
        ", width: " << std::dec << value.access_size;
    return ZX_ERR_NOT_SUPPORTED;
  }

  VirtioQueue* queue(uint16_t sel) override {
    return sel >= NUM_QUEUES ? nullptr : &queues_[sel];
  }

 protected:
  // Mutex for accessing device configuration fields.
  mutable fbl::Mutex config_mutex_;
  ConfigType config_ __TA_GUARDED(config_mutex_) = {};

 private:
  VirtioQueue queues_[NUM_QUEUES];
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_DEVICE_H_
