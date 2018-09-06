// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_pci.h"

#include <stdio.h>

#include <trace/event.h>
#include <virtio/virtio_ids.h>

#include "garnet/lib/machina/bits.h"
#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue.h"
#include "lib/fxl/logging.h"

namespace machina {

static constexpr uint8_t kPciBar64BitMultiplier = 2;

static constexpr uint8_t kPciCapTypeVendorSpecific = 0x9;
static constexpr uint16_t kPciVendorIdVirtio = 0x1af4;

// Virtio PCI Bar Layout.
//
// Expose all read/write fields on BAR0 using a strongly ordered mapping.
// Map the Queue notify region to BAR1 with a BELL type that does not require
// the guest to decode any instruction fields. The queue to notify can be
// inferred based on the address accessed alone.
//
//          BAR0                BAR1
//      ------------  00h   ------------  00h
//     | Virtio PCI |      |  Queue 0   |
//     |   Common   |      |   Notify   |
//     |   Config   |      |------------| 04h
//     |------------| 38h  |  Queue 1   |
//     | ISR Config |      |   Notify   |
//     |------------| 3ch  |------------|
//     |  Device-   |      |    ...     |
//     | Specific   |      |------------| 04 * N
//     |  Config    |      |  Queue N   |
//     |            |      |   Notify   |
//      ------------        ------------
// These structures are defined in Virtio 1.0 Section 4.1.4.
static constexpr uint8_t kVirtioPciBar = 0;
static constexpr uint8_t kVirtioPciNotifyBar = 1;
static_assert(kVirtioPciBar < kPciMaxBars && kVirtioPciNotifyBar < kPciMaxBars,
              "Not enough BAR registers available");

// Common configuration.
static constexpr size_t kVirtioPciCommonCfgBase = 0;
static constexpr size_t kVirtioPciCommonCfgSize = 0x38;
static constexpr size_t kVirtioPciCommonCfgTop =
    kVirtioPciCommonCfgBase + kVirtioPciCommonCfgSize - 1;
static_assert(kVirtioPciCommonCfgSize == sizeof(virtio_pci_common_cfg_t),
              "virtio_pci_common_cfg_t has unexpected size");
// Virtio 1.0 Section 4.1.4.3.1: offset MUST be 4-byte aligned.
static_assert(is_aligned(kVirtioPciCommonCfgBase, 4),
              "Virtio PCI common config has illegal alignment");

// Notification configuration.
static constexpr size_t kVirtioPciNotifyCfgBase = 0;
// Virtio 1.0 Section 4.1.4.4.1: offset MUST be 2-byte aligned.
static_assert(is_aligned(kVirtioPciNotifyCfgBase, 2),
              "Virtio PCI notify config has illegal alignment");

// Interrupt status configuration.
static constexpr size_t kVirtioPciIsrCfgBase = 0x38;
static constexpr size_t kVirtioPciIsrCfgSize = 1;
static constexpr size_t kVirtioPciIsrCfgTop =
    kVirtioPciIsrCfgBase + kVirtioPciIsrCfgSize - 1;
// Virtio 1.0 Section 4.1.4.5: The offset for the ISR status has no alignment
// requirements.

// Device-specific configuration.
static constexpr size_t kVirtioPciDeviceCfgBase = 0x3c;
// Virtio 1.0 Section 4.1.4.6.1: The offset for the device-specific
// configuration MUST be 4-byte aligned.
static_assert(is_aligned(kVirtioPciDeviceCfgBase, 4),
              "Virtio PCI notify config has illegal alignment");

static constexpr uint16_t virtio_pci_id(uint16_t virtio_id) {
  return static_cast<uint16_t>(virtio_id + 0x1040u);
}

static constexpr uint32_t virtio_pci_class_code(uint16_t virtio_id) {
  // See PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section D.
  switch (virtio_id) {
    case VIRTIO_ID_BALLOON:
      return 0x05000000;
    case VIRTIO_ID_BLOCK:
      return 0x01800000;
    case VIRTIO_ID_CONSOLE:
      return 0x07020000;
    case VIRTIO_ID_GPU:
      return 0x03808000;
    case VIRTIO_ID_INPUT:
      return 0x09800000;
    case VIRTIO_ID_NET:
      return 0x02000000;
    case VIRTIO_ID_VSOCK:
      return 0x02800000;
  }
  return 0;
}

// Virtio 1.0 Section 4.1.2.1: Non-transitional devices SHOULD have a PCI
// Revision ID of 1 or higher.
static constexpr uint32_t kVirtioPciRevisionId = 1;

static constexpr uint32_t virtio_pci_device_class(uint16_t virtio_id) {
  return virtio_pci_class_code(virtio_id) | kVirtioPciRevisionId;
}

VirtioPci::VirtioPci(VirtioDeviceConfig* device_config)
    : PciDevice({
          .device_id = virtio_pci_id(device_config->device_id),
          .vendor_id = kPciVendorIdVirtio,
          .subsystem_id = device_config->device_id,
          .subsystem_vendor_id = 0,
          .device_class = virtio_pci_device_class(device_config->device_id),
      }),
      device_config_(device_config) {
  SetupCaps();
}

zx_status_t VirtioPci::ReadBar(uint8_t bar, uint64_t offset,
                               IoValue* value) const {
  TRACE_DURATION("machina", "pci_readbar", "bar", bar, "offset", offset,
                 "access_size", value->access_size);
  switch (bar) {
    case kVirtioPciBar:
      return ConfigBarRead(offset, value);
  }
  FXL_LOG(ERROR) << "Unhandled read of BAR " << bar;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioPci::WriteBar(uint8_t bar, uint64_t offset,
                                const IoValue& value) {
  TRACE_DURATION("machina", "pci_writebar", "bar", bar, "offset", offset,
                 "access_size", value.access_size);
  switch (bar) {
    case kVirtioPciBar:
      return ConfigBarWrite(offset, value);
    case kVirtioPciNotifyBar:
      return NotifyBarWrite(offset, value);
  }
  FXL_LOG(ERROR) << "Unhandled write to BAR " << bar;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioPci::Interrupt() {
  bool should_interrupt;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    should_interrupt = isr_status_ > 0;
  }
  return should_interrupt ? PciDevice::Interrupt() : ZX_OK;
}

// Handle reads to the common configuration structure as defined in
// Virtio 1.0 Section 4.1.4.3.
zx_status_t VirtioPci::CommonCfgRead(uint64_t addr, IoValue* value) const {
  switch (addr) {
    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES_SEL: {
      std::lock_guard<std::mutex> lock(mutex_);
      value->u32 = driver_features_sel_;
      value->access_size = 4;
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES_SEL: {
      std::lock_guard<std::mutex> lock(mutex_);
      value->u32 = device_features_sel_;
      value->access_size = 4;
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES: {
      // We currently only support a single feature word.
      std::lock_guard<std::mutex> lock(mutex_);
      value->u32 = driver_features_sel_ > 0 ? 0 : driver_features_;
      value->access_size = 4;
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES: {
      // Virtio 1.0 Section 6:
      //
      // A device MUST offer VIRTIO_F_VERSION_1.
      //
      // VIRTIO_F_VERSION_1(32) This indicates compliance with this
      // specification, giving a simple way to detect legacy devices or
      // drivers.
      //
      // This is the only feature supported beyond the first feature word so
      // we just special case it here.
      std::lock_guard<std::mutex> lock(mutex_);
      value->access_size = 4;
      if (device_features_sel_ == 1) {
        value->u32 = 1;
        return ZX_OK;
      }

      value->u32 =
          device_features_sel_ > 0 ? 0 : device_config_->device_features;
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_NUM_QUEUES: {
      std::lock_guard<std::mutex> lock(mutex_);
      value->u16 = device_config_->num_queues;
      value->access_size = 2;
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_STATUS: {
      std::lock_guard<std::mutex> lock(mutex_);
      value->u8 = status_;
      value->access_size = 1;
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SEL: {
      std::lock_guard<std::mutex> lock(mutex_);
      value->u16 = queue_sel_;
      value->access_size = 2;
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SIZE: {
      VirtioQueue* queue = selected_queue();
      if (queue == nullptr) {
        return ZX_ERR_BAD_STATE;
      }

      value->u16 = queue->size();
      value->access_size = 2;
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_ENABLE:
      // Virtio 1.0 Section 4.1.4.3: The device MUST present a 0 in
      // queue_enable on reset.
      //
      // Note the implementation currently does not respect this value.
      value->access_size = 2;
      value->u16 = 0;
      return ZX_OK;
    case VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW ... VIRTIO_PCI_COMMON_CFG_QUEUE_USED_HIGH: {
      VirtioQueue* queue = selected_queue();
      if (queue == nullptr) {
        return ZX_ERR_BAD_STATE;
      }

      size_t word =
          (addr - VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW) / sizeof(uint32_t);
      value->access_size = 4;
      value->u32 = queue->UpdateRing<uint32_t>(
          [word](VirtioRing* ring) { return ring->addr.words[word]; });
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_NOTIFY_OFF: {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_sel_ >= device_config_->num_queues) {
        return ZX_ERR_BAD_STATE;
      }

      value->u32 = queue_sel_;
      value->access_size = 4;
      return ZX_OK;
    }

    // Currently not implemented.
    case VIRTIO_PCI_COMMON_CFG_CONFIG_GEN:
    case VIRTIO_PCI_COMMON_CFG_QUEUE_MSIX_VECTOR:
    case VIRTIO_PCI_COMMON_CFG_MSIX_CONFIG:
      value->u32 = 0;
      return ZX_OK;
  }
  FXL_LOG(ERROR) << "Unhandled common config read 0x" << std::hex << addr;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioPci::ConfigBarRead(uint64_t addr, IoValue* value) const {
  switch (addr) {
    case kVirtioPciCommonCfgBase ... kVirtioPciCommonCfgTop:
      return CommonCfgRead(addr - kVirtioPciCommonCfgBase, value);
    case kVirtioPciIsrCfgBase ... kVirtioPciIsrCfgTop:
      std::lock_guard<std::mutex> lock(mutex_);
      value->u8 = isr_status_;
      value->access_size = 1;

      // From VIRTIO 1.0 Section 4.1.4.5:
      //
      // To avoid an extra access, simply reading this register resets it to
      // 0 and causes the device to de-assert the interrupt.
      isr_status_ = 0;
      return ZX_OK;
  }

  size_t device_config_top =
      kVirtioPciDeviceCfgBase + device_config_->config_size;
  if (addr >= kVirtioPciDeviceCfgBase && addr < device_config_top) {
    uint64_t cfg_addr = addr - kVirtioPciDeviceCfgBase;
    std::lock_guard<std::mutex> lock(device_config_->mutex);
    switch (value->access_size) {
      case 1: {
        uint8_t* buf = static_cast<uint8_t*>(device_config_->config);
        value->u8 = buf[cfg_addr];
        return ZX_OK;
      }
      case 2: {
        uint16_t* buf = static_cast<uint16_t*>(device_config_->config);
        value->u16 = buf[cfg_addr / 2];
        return ZX_OK;
      }
      case 4: {
        uint32_t* buf = static_cast<uint32_t*>(device_config_->config);
        value->u32 = buf[cfg_addr / 4];
        return ZX_OK;
      }
    }
  }
  FXL_LOG(ERROR) << "Unhandled config BAR read 0x" << std::hex << addr;
  return ZX_ERR_NOT_SUPPORTED;
}

// Handle writes to the common configuration structure as defined in
// Virtio 1.0 Section 4.1.4.3.
zx_status_t VirtioPci::CommonCfgWrite(uint64_t addr, const IoValue& value) {
  switch (addr) {
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES_SEL: {
      if (value.access_size != 4) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }

      std::lock_guard<std::mutex> lock(mutex_);
      device_features_sel_ = value.u32;
      return ZX_OK;
    }

    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES_SEL: {
      if (value.access_size != 4) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }

      std::lock_guard<std::mutex> lock(mutex_);
      driver_features_sel_ = value.u32;
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DRIVER_FEATURES: {
      if (value.access_size != 4) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }

      std::lock_guard<std::mutex> lock(mutex_);
      if (driver_features_sel_ == 0) {
        driver_features_ = value.u32;
      }
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_DEVICE_STATUS: {
      if (value.access_size != 1) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }

      uint32_t negotiated_features;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = value.u8;
        negotiated_features =
            device_config_->device_features & driver_features_;
      }

      if (value.u8 & VIRTIO_STATUS_DRIVER_OK) {
        return device_config_->ready_device(negotiated_features);
      }
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SEL: {
      if (value.access_size != 2) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      if (value.u16 >= device_config_->num_queues) {
        return ZX_ERR_OUT_OF_RANGE;
      }

      std::lock_guard<std::mutex> lock(mutex_);
      queue_sel_ = value.u16;
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_SIZE: {
      if (value.access_size != 2) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      VirtioQueue* queue = selected_queue();
      if (queue == nullptr) {
        return ZX_ERR_BAD_STATE;
      }

      zx_gpaddr_t desc_addr, avail_addr, used_addr;
      queue->GetAddrs(&desc_addr, &avail_addr, &used_addr);
      queue->Configure(value.u16, desc_addr, avail_addr, used_addr);
      return ZX_OK;
    }
    case VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW ... VIRTIO_PCI_COMMON_CFG_QUEUE_USED_HIGH: {
      if (value.access_size != 4) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      VirtioQueue* queue = selected_queue();
      if (queue == nullptr) {
        return ZX_ERR_BAD_STATE;
      }

      // Update the configuration words for the queue.
      size_t word =
          (addr - VIRTIO_PCI_COMMON_CFG_QUEUE_DESC_LOW) / sizeof(uint32_t);
      queue->UpdateRing<void>([&value, word](VirtioRing* ring) {
        ring->addr.words[word] = value.u32;
      });

      // Update the configuration based on the new configuration words.
      zx_gpaddr_t desc_addr, avail_addr, used_addr;
      queue->GetAddrs(&desc_addr, &avail_addr, &used_addr);
      queue->Configure(queue->size(), desc_addr, avail_addr, used_addr);
      return ZX_OK;
    }
    // Not implemented registers.
    case VIRTIO_PCI_COMMON_CFG_QUEUE_MSIX_VECTOR:
    case VIRTIO_PCI_COMMON_CFG_MSIX_CONFIG:
    case VIRTIO_PCI_COMMON_CFG_QUEUE_ENABLE:
      return ZX_OK;
    // Read-only registers.
    case VIRTIO_PCI_COMMON_CFG_QUEUE_NOTIFY_OFF:
    case VIRTIO_PCI_COMMON_CFG_NUM_QUEUES:
    case VIRTIO_PCI_COMMON_CFG_CONFIG_GEN:
    case VIRTIO_PCI_COMMON_CFG_DEVICE_FEATURES:
      FXL_LOG(ERROR) << "Unsupported write 0x" << std::hex << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
  FXL_LOG(ERROR) << "Unhandled common config write 0x" << std::hex << addr;
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t write_device_config(VirtioDeviceConfig* device_config,
                                       uint64_t addr, const IoValue& value) {
  switch (value.access_size) {
    case 1: {
      std::lock_guard<std::mutex> lock(device_config->mutex);
      uint8_t* buf = static_cast<uint8_t*>(device_config->config);
      buf[addr] = value.u8;
      return ZX_OK;
    }
    case 2: {
      std::lock_guard<std::mutex> lock(device_config->mutex);
      uint16_t* buf = static_cast<uint16_t*>(device_config->config);
      buf[addr / 2] = value.u16;
      return ZX_OK;
    }
    case 4: {
      std::lock_guard<std::mutex> lock(device_config->mutex);
      uint32_t* buf = static_cast<uint32_t*>(device_config->config);
      buf[addr / 4] = value.u32;
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioPci::ConfigBarWrite(uint64_t addr, const IoValue& value) {
  switch (addr) {
    case kVirtioPciCommonCfgBase ... kVirtioPciCommonCfgTop: {
      uint64_t offset = addr - kVirtioPciCommonCfgBase;
      return CommonCfgWrite(offset, value);
    }
  }

  size_t device_config_top =
      kVirtioPciDeviceCfgBase + device_config_->config_size;
  if (addr >= kVirtioPciDeviceCfgBase && addr < device_config_top) {
    uint64_t cfg_addr = addr - kVirtioPciDeviceCfgBase;
    zx_status_t status = write_device_config(device_config_, cfg_addr, value);
    if (status == ZX_OK) {
      return device_config_->update_config(cfg_addr, value);
    }
  }
  FXL_LOG(ERROR) << "Unhandled config BAR write 0x" << std::hex << addr;
  return ZX_ERR_NOT_SUPPORTED;
}

void setup_cap(pci_cap_t* cap, virtio_pci_cap_t* virtio_cap, uint8_t cfg_type,
               size_t cap_len, size_t data_length, uint8_t bar,
               size_t bar_offset) {
  virtio_cap->cfg_type = cfg_type;
  virtio_cap->bar = bar * kPciBar64BitMultiplier;
  virtio_cap->offset = static_cast<uint32_t>(bar_offset);
  virtio_cap->length = static_cast<uint32_t>(data_length);

  cap->id = kPciCapTypeVendorSpecific;
  cap->data = reinterpret_cast<uint8_t*>(virtio_cap);
  cap->len = virtio_cap->cap_len = static_cast<uint8_t>(cap_len);
}

void VirtioPci::SetupCaps() {
  // Common configuration.
  setup_cap(&capabilities_[0], &common_cfg_cap_, VIRTIO_PCI_CAP_COMMON_CFG,
            sizeof(common_cfg_cap_), kVirtioPciCommonCfgSize, kVirtioPciBar,
            kVirtioPciCommonCfgBase);

  // Notify configuration.
  notify_cfg_cap_.notify_off_multiplier = kVirtioPciNotifyCfgMultiplier;
  size_t notify_size =
      device_config_->num_queues * kVirtioPciNotifyCfgMultiplier;
  setup_cap(&capabilities_[1], &notify_cfg_cap_.cap, VIRTIO_PCI_CAP_NOTIFY_CFG,
            sizeof(notify_cfg_cap_), notify_size, kVirtioPciNotifyBar,
            kVirtioPciNotifyCfgBase);
  bar_[kVirtioPciNotifyBar].size = notify_size;
  bar_[kVirtioPciNotifyBar].trap_type = TrapType::MMIO_BELL;

  // ISR configuration.
  setup_cap(&capabilities_[2], &isr_cfg_cap_, VIRTIO_PCI_CAP_ISR_CFG,
            sizeof(isr_cfg_cap_), kVirtioPciIsrCfgSize, kVirtioPciBar,
            kVirtioPciIsrCfgBase);

  // Device-specific configuration.
  setup_cap(&capabilities_[3], &device_cfg_cap_, VIRTIO_PCI_CAP_DEVICE_CFG,
            sizeof(device_cfg_cap_), device_config_->config_size, kVirtioPciBar,
            kVirtioPciDeviceCfgBase);

  // Note VIRTIO_PCI_CAP_PCI_CFG is not implemented.
  // This one is more complex since it is writable and doesn't seem to be
  // used by Linux or Zircon.

  static_assert(kVirtioPciNumCapabilities == 4,
                "Incorrect number of capabilities");
  set_capabilities(capabilities_, kVirtioPciNumCapabilities);

  bar_[kVirtioPciBar].size =
      kVirtioPciDeviceCfgBase + device_config_->config_size;
  bar_[kVirtioPciBar].trap_type = TrapType::MMIO_SYNC;
}

VirtioQueue* VirtioPci::selected_queue() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_sel_ >= device_config_->num_queues) {
    return nullptr;
  }
  return &device_config_->queues[queue_sel_];
}

zx_status_t VirtioPci::NotifyBarWrite(uint64_t offset, const IoValue& value) {
  if (!is_aligned(offset, kVirtioPciNotifyCfgMultiplier)) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto queue = static_cast<uint16_t>(offset / kVirtioPciNotifyCfgMultiplier);
  return device_config_->notify_queue(queue);
}

}  // namespace machina
