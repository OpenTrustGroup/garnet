// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>
#include <virtio/virtio_ids.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <vector>
#include "garnet/lib/machina/virtio_device.h"
#include <lib/async-loop/cpp/loop.h>
#include <mutex>

namespace machina {

#define VIRTIO_EINT_Q_COUNT 2

struct virtio_eint_config {
  uint32_t max_eint_num;
} __PACKED;

struct virtio_eint_req_head {
  uint32_t eint_num;
  uint32_t operation;
  uint32_t flags;
  uint32_t type;
};
struct virtio_eint_resp {
  int32_t result;
  uint32_t eint_num;
  uint32_t irq_count;
  uint32_t reserved;
};

struct virtio_eint_irq_event {
  uint32_t eint_num;  // EINT pin that triggered
  uint32_t irq_count; // Interrupt count
  uint64_t timestamp; // Timestamp when interrupt occurred
  uint32_t status;    // Interrupt status
  uint32_t reserved;  // Reserved
};

enum virtio_eint_ops {
  VIRTIO_EINT_OP_REQUEST_IRQ = 0,
  VIRTIO_EINT_OP_SET_TYPE = 1,
  VIRTIO_EINT_OP_MASK = 2,
  VIRTIO_EINT_OP_UNMASK = 3,
  VIRTIO_EINT_OP_ACK = 4,
  VIRTIO_EINT_OP_SET_WAKE = 5,
};

class VirtioEINT : public VirtioDeviceBase<VIRTIO_ID_EINT, VIRTIO_EINT_Q_COUNT,
                                           virtio_eint_config> {
public:
  VirtioEINT(const PhysMem &phys_mem);
  ~VirtioEINT() override;

  // Handle EINT command from control queue
  zx_status_t HandleEINTCommand(VirtioQueue *queue, uint16_t head,
                                uint32_t *used);
  void SetVmid(uint16_t vmid) { vmid_ = vmid; }

protected:
  static zx_status_t QueueHandler(VirtioQueue *queue, uint16_t head,
                                  uint32_t *used, void *ctx);

private:
  zx_status_t InitEINT();
  zx_status_t ProcessEINTRequest(struct virtio_eint_req_head *req,
                                 struct virtio_eint_resp *resp);

  int OpenEINTDevice();

  // Setup interrupt handler for an EINT pin
  zx_status_t SetupInterruptHandler(uint32_t eint_num, zx_handle_t irq_handle);
  void InterruptHandler(uint32_t eint_num, zx_handle_t irq_handle);

  // Send interrupt event to guest via queue evt
  zx_status_t SendInterruptEvent(uint32_t eint_num, zx_time_t timestamp);

  async::Wait control_queue_wait_;
  async::Loop eint_loop_;
  async_t *async_ = nullptr;
  bool loop_started_ = false;

  std::mutex event_queue_mutex_;
  int eint_fd_;
  uint16_t vmid_;

  uint32_t max_eint_pins_;

  // IRQ handles for each EINT pin (host side)
  std::vector<zx_handle_t> irq_handles_;
  // Interrupt threads for each EINT pin
  std::vector<thrd_t> irq_threads_;
  std::vector<uint32_t> irq_counts_;
  std::vector<uint8_t> eint_enabled_;

  static constexpr const char *kEINTDevicePath =
      "/dev/sys/platform/mtk_eint/mtk_eint";
};

} // namespace machina
