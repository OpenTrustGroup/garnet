// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_device.h"

#include <stdio.h>
#include <string.h>

#include <fbl/auto_lock.h>

#include "lib/fxl/logging.h"

#define QUEUE_SIZE 128u

namespace machina {

VirtioDevice::VirtioDevice(uint8_t device_id,
                           size_t config_size,
                           VirtioQueue* queues,
                           uint16_t num_queues,
                           const PhysMem& phys_mem,
                           Transport transport,
                           bool enable_msix)
    : device_id_(device_id),
      device_config_size_(config_size),
      queues_(queues),
      num_queues_(num_queues),
      phys_mem_(phys_mem),
      transport_(transport),
      use_msix_(enable_msix) {
  if (transport_ == Transport::PCI)
    pci_ = std::make_unique<VirtioPci>(this);
  else
    mmio_ = std::make_unique<MmioDevice>(this);

  if (queues_) {
    for (uint16_t i = 0; i < num_queues_; i++) {
      queues_[i].set_index(i);
    }
  }
}

zx_status_t VirtioDevice::NotifyGuest() {
  bool interrupt = false;
  {
    fbl::AutoLock lock(&mutex_);
    interrupt = isr_status_ > 0;
  }

  if (!interrupt) {
    return ZX_OK;
  }
  if (transport_ == Transport::PCI)
    return pci_->Interrupt();
  return mmio_->Interrupt();
}

zx_status_t VirtioDevice::NotifyGuest(uint32_t queue_index) {
  if (transport_ == Transport::PCI) {
    if (pci_->msix_enabled()) {
      TriggerMsixInterrupt(queue_index + 1);
      return ZX_OK;
    }
  }
  return NotifyGuest();
}

zx_status_t VirtioDevice::Kick(uint16_t kicked_queue) {
  if (kicked_queue >= num_queues_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx_status_t status = HandleQueueNotify(kicked_queue);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to handle queue notify event";
    return status;
  }

  // Send an interrupt back to the guest if we've generated one while
  // processing the queue.
  status = NotifyGuest();
  if (status != ZX_OK) {
    return status;
  }

  // Notify threads waiting on a descriptor.
  return queues_[kicked_queue].Signal();
}

}  // namespace machina
