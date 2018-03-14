// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_PCI_H_
#define GARNET_LIB_MACHINA_VIRTIO_PCI_H_

#include <virtio/virtio.h>
#include <zircon/types.h>

#include "garnet/lib/machina/pci.h"

namespace machina {

class VirtioDevice;
class VirtioQueue;

static constexpr size_t kVirtioPciNumCapabilities = 4;

/* Virtio PCI transport implementation. */
class VirtioPci : public PciDevice {
 public:
  VirtioPci(VirtioDevice* device);

  // Read a value at |bar| and |offset| from this device.
  zx_status_t ReadBar(uint8_t bar,
                      uint64_t offset,
                      IoValue* value) const override;
  // Write a value at |bar| and |offset| to this device.
  zx_status_t WriteBar(uint8_t bar,
                       uint64_t offset,
                       const IoValue& value) override;

 private:
  // Handle accesses to the general configuration BAR.
  zx_status_t ConfigBarRead(uint64_t addr, IoValue* value) const;
  zx_status_t ConfigBarWrite(uint64_t addr, const IoValue& value);

  // Handle accesses to the common configuration region.
  zx_status_t CommonCfgRead(uint64_t addr, IoValue* value) const;
  zx_status_t CommonCfgWrite(uint64_t addr, const IoValue& value);

  // Handle writes to the notify BAR.
  zx_status_t NotifyBarWrite(uint64_t addr, const IoValue& value);

  void SetupCaps();
  void SetupCap(pci_cap_t* cap,
                virtio_pci_cap_t* virtio_cap,
                uint8_t cfg_type,
                size_t cap_len,
                size_t data_length,
                uint8_t bar,
                size_t bar_offset);

  VirtioQueue* selected_queue() const;

  // We need one of these for every virtio_pci_cap_t structure we expose.
  pci_cap_t capabilities_[kVirtioPciNumCapabilities];
  // Virtio PCI capabilities.
  virtio_pci_cap_t common_cfg_cap_;
  virtio_pci_cap_t device_cfg_cap_;
  virtio_pci_notify_cap_t notify_cfg_cap_;
  virtio_pci_cap_t isr_cfg_cap_;

  VirtioDevice* device_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_PCI_H_
