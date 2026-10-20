// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_eint.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/grt-eint.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "lib/fxl/logging.h"

namespace machina {

namespace {
constexpr uint32_t kFallbackMaxEintPins = 256;

struct InterruptThreadArgs {
  VirtioEINT *self;
  uint32_t eint_num;
  zx_handle_t irq_handle;
};
} // namespace

#define VIRTIO_EINT_OK 0
#define VIRTIO_EINT_ERR 1

VirtioEINT::VirtioEINT(const PhysMem &phys_mem)
    : VirtioDeviceBase(phys_mem, Transport::PCI, false),
      eint_fd_(-1), max_eint_pins_(kFallbackMaxEintPins) {
  FXL_LOG(INFO) << "VirtioEINT: Initializing virtio-eint backend";

  eint_fd_ = OpenEINTDevice();
  if (eint_fd_ > 0) {
    uint32_t host_max = 0;
    if (ioctl_grt_eint_get_max_eint_num(eint_fd_, &host_max) >= 0 && host_max > 0) {
      max_eint_pins_ = host_max;
      FXL_LOG(INFO) << "VirtioEINT: got host max_eint_num " << host_max;
    }
  } else {
    FXL_LOG(ERROR) << "VirtioEINT: Failed to open eint device ";
    return;
  }

  config_.max_eint_num = max_eint_pins_;
  irq_handles_.assign(max_eint_pins_, ZX_HANDLE_INVALID);
  irq_threads_.assign(max_eint_pins_, thrd_t{});
  irq_counts_.assign(max_eint_pins_, 0);
  eint_enabled_.assign(max_eint_pins_, 0);

  zx_status_t loop_status = eint_loop_.StartThread("virtio-eint");
  if (loop_status != ZX_OK) {
    FXL_LOG(ERROR) << "VirtioEINT: Failed to start async loop: "
                   << loop_status;
    return;
  }
  loop_started_ = true;
  async_ = eint_loop_.async();

  // Start polling the control queue (queue 0)
  zx_status_t status = queue(0)->PollAsync(async_, &control_queue_wait_,
                                           &VirtioEINT::QueueHandler, this);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "VirtioEINT: Failed to start control queue polling: "
                   << status;
    return;
  }

  FXL_LOG(INFO) << "VirtioEINT: Initialization complete";
}

VirtioEINT::~VirtioEINT() {
  if (loop_started_) {
    control_queue_wait_.Cancel(async_);
    eint_loop_.Quit();
    eint_loop_.JoinThreads();
    loop_started_ = false;
  }

  if (eint_fd_ < 0) {
    return;
  }

  for (uint32_t i = 0; i < max_eint_pins_; i++) {
    if (eint_enabled_[i] && irq_handles_[i] != ZX_HANDLE_INVALID) {
      eint_enabled_[i] = false;
      zx_object_signal(irq_handles_[i], 0, ZX_USER_SIGNAL_0);

      if (irq_threads_[i] != 0) {
        thrd_join(irq_threads_[i], NULL);
      }

      zx_handle_close(irq_handles_[i]);
      irq_handles_[i] = ZX_HANDLE_INVALID;
    }
  }
  ioctl_grt_eint_vm_destroy(eint_fd_, &vmid_);

  if (eint_fd_ >= 0) {
    close(eint_fd_);
    eint_fd_ = -1;
  }
}

int VirtioEINT::OpenEINTDevice() {
  int fd = open(kEINTDevicePath, O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR) << "VirtioEINT: Failed to open " << kEINTDevicePath
                   << " errno: " << errno;
    return -1;
  }
  FXL_LOG(INFO) << "VirtioEINT: Opened device " << kEINTDevicePath;
  return fd;
}

// Static queue handler for control queue (queue 0)
zx_status_t VirtioEINT::QueueHandler(VirtioQueue *queue, uint16_t head,
                                     uint32_t *used, void *ctx) {
  VirtioEINT *eint = reinterpret_cast<VirtioEINT *>(ctx);
  return eint->HandleEINTCommand(queue, head, used);
}

zx_status_t VirtioEINT::HandleEINTCommand(VirtioQueue *queue, uint16_t head,
                                          uint32_t *used) {
  virtio_desc_t req_desc;
  queue->ReadDesc(head, &req_desc);

  if (!req_desc.has_next) {
    FXL_LOG(ERROR) << "VirtioEINT: Request descriptor has no next";
    return ZX_OK;
  }

  virtio_desc_t resp_desc;
  queue->ReadDesc(req_desc.next, &resp_desc);

  struct virtio_eint_req_head *req =
      reinterpret_cast<struct virtio_eint_req_head *>(req_desc.addr);
  struct virtio_eint_resp *resp =
      reinterpret_cast<struct virtio_eint_resp *>(resp_desc.addr);

  zx_status_t status = ProcessEINTRequest(req, resp);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "VirtioEINT: Failed to process request: " << status;
    resp->result = VIRTIO_EINT_ERR;
  }

  // Update used length
  *used += sizeof(struct virtio_eint_resp);

  return ZX_OK;
}

zx_status_t VirtioEINT::ProcessEINTRequest(struct virtio_eint_req_head *req,
                                           struct virtio_eint_resp *resp) {
  if (eint_fd_ < 0) {
    FXL_LOG(ERROR) << "VirtioEINT: Device not initialized";
    resp->result = VIRTIO_EINT_ERR;
    return ZX_ERR_BAD_STATE;
  }

  if (req->eint_num >= max_eint_pins_) {
    FXL_LOG(ERROR) << "VirtioEINT: Invalid EINT number " << req->eint_num;
    resp->result = VIRTIO_EINT_ERR;
    return ZX_ERR_OUT_OF_RANGE;
  }

  FXL_LOG(INFO) << "VirtioEINT: Processing operation " << req->operation
                << " for EINT " << req->eint_num;

  switch (req->operation) {
  case VIRTIO_EINT_OP_REQUEST_IRQ: {
    zx_handle_t host_irq_handle = ZX_HANDLE_INVALID;
    pdev_eint_req_t ioctl_req = {
        .eint_num = req->eint_num,
        .vmid = vmid_,
    };
    ssize_t res =
        ioctl_grt_eint_irq_request(eint_fd_, &ioctl_req, &host_irq_handle);
    if (res < 0) {
      FXL_LOG(ERROR) << "VirtioEINT: Failed to request IRQ for EINT "
                     << req->eint_num << ": " << res;
      resp->result = VIRTIO_EINT_ERR;
      return static_cast<zx_status_t>(res);
    }

    // Save the host IRQ handle
    irq_handles_[req->eint_num] = host_irq_handle;
    eint_enabled_[req->eint_num] = true;
    irq_counts_[req->eint_num] = 0;

    // Setup interrupt handler thread
    zx_status_t status = SetupInterruptHandler(req->eint_num, host_irq_handle);
    if (status != ZX_OK) {
      FXL_LOG(ERROR)
          << "VirtioEINT: Failed to setup interrupt handler for EINT "
          << req->eint_num << ": " << status;
      zx_handle_close(host_irq_handle);
      irq_handles_[req->eint_num] = ZX_HANDLE_INVALID;
      eint_enabled_[req->eint_num] = false;
      resp->result = VIRTIO_EINT_ERR;
      return status;
    }

    // Return success with EINT number
    resp->result = VIRTIO_EINT_OK;
    resp->eint_num = req->eint_num;
    resp->irq_count = irq_counts_[req->eint_num];

    FXL_LOG(INFO) << "VirtioEINT: IRQ requested for EINT " << req->eint_num
                  << ", host handle: " << host_irq_handle;
    break;
  }

  case VIRTIO_EINT_OP_SET_TYPE: {
    pdev_eint_set_type_t set_type_req = {
        .eint_num = req->eint_num,
        .type = req->type,
    };
    zx_status_t status = ioctl_grt_eint_set_type(eint_fd_, &set_type_req);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "VirtioEINT: Failed to set type for EINT "
                     << req->eint_num << ": " << status;
      resp->result = VIRTIO_EINT_ERR;
      return status;
    }
    resp->result = VIRTIO_EINT_OK;
    resp->eint_num = req->eint_num;
    resp->irq_count = irq_counts_[req->eint_num];
    FXL_LOG(INFO) << "VirtioEINT: Type set for EINT " << req->eint_num;
    break;
  }

  case VIRTIO_EINT_OP_MASK: {
    pdev_eint_op_t mask_req = {
        .eint_num = req->eint_num,
    };
    zx_status_t status = ioctl_grt_eint_mask(eint_fd_, &mask_req);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "VirtioEINT: Failed to mask EINT " << req->eint_num
                     << ": " << status;
      resp->result = VIRTIO_EINT_ERR;
      return status;
    }
    resp->result = VIRTIO_EINT_OK;
    resp->eint_num = req->eint_num;
    resp->irq_count = irq_counts_[req->eint_num];
    FXL_LOG(INFO) << "VirtioEINT: EINT " << req->eint_num << " masked";
    break;
  }

  case VIRTIO_EINT_OP_UNMASK: {
    pdev_eint_op_t unmask_req = {
        .eint_num = req->eint_num,
    };
    zx_status_t status = ioctl_grt_eint_unmask(eint_fd_, &unmask_req);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "VirtioEINT: Failed to unmask EINT " << req->eint_num
                     << ": " << status;
      resp->result = VIRTIO_EINT_ERR;
      return status;
    }
    resp->result = VIRTIO_EINT_OK;
    resp->eint_num = req->eint_num;
    resp->irq_count = irq_counts_[req->eint_num];
    FXL_LOG(INFO) << "VirtioEINT: EINT " << req->eint_num << " unmasked";
    break;
  }

  case VIRTIO_EINT_OP_ACK: {
    pdev_eint_op_t ack_req = {
        .eint_num = req->eint_num,
    };
    zx_status_t status = ioctl_grt_eint_ack(eint_fd_, &ack_req);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "VirtioEINT: Failed to ack EINT " << req->eint_num
                     << ": " << status;
      resp->result = VIRTIO_EINT_ERR;
      return status;
    }
    resp->result = VIRTIO_EINT_OK;
    resp->eint_num = req->eint_num;
    resp->irq_count = irq_counts_[req->eint_num];
    FXL_LOG(INFO) << "VirtioEINT: EINT " << req->eint_num << " acknowledged";
    break;
  }

  case VIRTIO_EINT_OP_SET_WAKE:
    // Temporary standalone bring-up: the host EINT ioctl has no wake
    // operation yet. Treat the request as successful to avoid frontend retry.
    resp->result = VIRTIO_EINT_OK;
    resp->eint_num = req->eint_num;
    resp->irq_count = irq_counts_[req->eint_num];
    FXL_LOG(INFO) << "VirtioEINT: EINT " << req->eint_num
                  << " wake request acknowledged (no-op)";
    break;

  default:
    FXL_LOG(ERROR) << "VirtioEINT: Unknown operation " << req->operation;
    resp->result = VIRTIO_EINT_ERR;
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t VirtioEINT::SetupInterruptHandler(uint32_t eint_num,
                                              zx_handle_t irq_handle) {
  // Allocate the argument structure; the thread will delete it when done.
  InterruptThreadArgs *args =
      new InterruptThreadArgs{this, eint_num, irq_handle};

  auto thread_fn = [](void *void_arg) -> int {
    InterruptThreadArgs *a = reinterpret_cast<InterruptThreadArgs *>(void_arg);
    a->self->InterruptHandler(a->eint_num, a->irq_handle);
    delete a;
    return 0;
  };

  int ret = thrd_create_with_name(&irq_threads_[eint_num], thread_fn, args,
                                  "virtio-eint-irq");
  if (ret != thrd_success) {
    delete args;
    FXL_LOG(ERROR) << "VirtioEINT: Failed to create interrupt thread";
    return ZX_ERR_NO_MEMORY;
  }

  // We will join the thread in the destructor; do not detach it here.
  return ZX_OK;
}

void VirtioEINT::InterruptHandler(uint32_t eint_num, zx_handle_t irq_handle) {
  FXL_LOG(INFO) << "VirtioEINT: Interrupt handler started for EINT "
                << eint_num;

  while (eint_enabled_[eint_num]) {
    zx_signals_t observed = 0;
    zx_status_t status = zx_object_wait_one(irq_handle, ZX_USER_SIGNAL_0,
                                            ZX_TIME_INFINITE, &observed);
    if (status != ZX_OK) {
      if (status == ZX_ERR_CANCELED || status == ZX_ERR_BAD_HANDLE) {
        FXL_LOG(INFO) << "VirtioEINT: Interrupt handler for EINT " << eint_num
                      << " canceled";
      } else {
        FXL_LOG(ERROR) << "VirtioEINT: Interrupt wait failed for EINT "
                       << eint_num << ": " << status;
      }
      break;
    }

    zx_object_signal(irq_handle, ZX_USER_SIGNAL_0, 0);
    if (!eint_enabled_[eint_num]) {
      break;
    }

    zx_time_t hw_timestamp = zx_clock_get(ZX_CLOCK_MONOTONIC);

    // Increment interrupt count
    irq_counts_[eint_num]++;

    FXL_LOG(INFO) << "VirtioEINT: Hardware interrupt on EINT " << eint_num
                  << ", count: " << irq_counts_[eint_num];

    // Send interrupt event to guest via queue 1
    status = SendInterruptEvent(eint_num, hw_timestamp);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "VirtioEINT: Failed to send interrupt event: "
                     << status;
    }
  }

}

zx_status_t VirtioEINT::SendInterruptEvent(uint32_t eint_num,
                                           zx_time_t timestamp) {
  virtio_eint_irq_event event = {};
  event.eint_num = eint_num;
  event.irq_count = irq_counts_[eint_num];
  event.timestamp = timestamp;
  event.status = 0;

  VirtioQueue *q = queue(1);
  uint16_t head;

  std::lock_guard<std::mutex> lock(event_queue_mutex_);

  zx_status_t status = q->NextAvail(&head);
  if (status == ZX_ERR_SHOULD_WAIT) {
    // No space in the event queue; drop the notification.
    FXL_LOG(WARNING) << "VirtioEINT: event queue full, dropping interrupt "
                     << eint_num;
    return ZX_ERR_SHOULD_WAIT;
  }
  if (status != ZX_OK) {
    return status;
  }

  virtio_desc_t desc;
  status = q->ReadDesc(head, &desc);
  if (status != ZX_OK) {
    // return descriptor in case of error to avoid leak
    q->Return(head, 0);
    return status;
  }

  if (desc.len < sizeof(event)) {
    FXL_LOG(ERROR) << "VirtioEINT: event descriptor too small (" << desc.len
                   << ")";
    q->Return(head, 0);
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(desc.addr, &event, sizeof(event));

  // Return the buffer and trigger an interrupt to the guest so it will
  // consume the event.
  return q->Return(head, sizeof(event),
                   VirtioQueue::InterruptAction::SEND_INTERRUPT);
}

} // namespace machina
