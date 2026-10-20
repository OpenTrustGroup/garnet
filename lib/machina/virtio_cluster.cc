// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_cluster.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string.h>

#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <thread>
#include <zircon/syscalls.h>
#include "lib/fxl/logging.h"

namespace machina {

namespace {
static constexpr zx_duration_t kClusterStopPollInterval = ZX_MSEC(100);
}  // namespace

struct VirtioCluster::CallbackState {
  std::atomic<bool> stopping{false};
  std::mutex mutex;
  std::condition_variable idle;
  uint32_t in_flight = 0;
};

zx_status_t VirtioCluster::NotifyGuestCluster(
    const char *buf,
    uint32_t len,
    const std::shared_ptr<CallbackState>& callback_state) {
  uint16_t head;
  if (len > CLUSTER_MAX_BUF_SIZE) {
    FXL_LOG(ERROR) << "[virtio cluster]Buffer length exceeds maximum size: \n" << len;
    return ZX_ERR_OUT_OF_RANGE;
  }

  VirtioQueue* rx_queue = queue(1);
  while (!callback_state->stopping.load()) {
    zx_status_t status = rx_queue->NextAvail(&head);
    if (status == ZX_OK) {
      break;
    }
    if (status != ZX_ERR_SHOULD_WAIT) {
      FXL_LOG(ERROR) << "[virtio cluster]Failed to get rx descriptor: "
                     << status;
      return status;
    }

    status = zx_object_wait_one(rx_queue->event(),
                                VirtioQueue::SIGNAL_QUEUE_AVAIL,
                                zx_deadline_after(kClusterStopPollInterval),
                                nullptr);
    if (status == ZX_ERR_TIMED_OUT) {
      continue;
    }
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "[virtio cluster]Failed to wait rx descriptor: "
                     << status;
      return status;
    }
  }
  if (callback_state->stopping.load()) {
    return ZX_ERR_STOP;
  }

  virtio_desc_t desc;
  zx_status_t status = rx_queue->ReadDesc(head, &desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "[virtio cluster]Failed to read descriptor: " << status;
    return status;
  }
  auto event_out = static_cast<virtio_cluster_rx*>(desc.addr);
  event_out->size = len;
  memcpy(event_out->buf, buf, len);
  return rx_queue->Return(head, sizeof(struct virtio_cluster_rx));
}

zx_status_t VirtioCluster::cluster_write(struct virtio_cluster_tx* tx,
  struct virtio_cluster_rsp* rsp, uint32_t *used) {
  zx_status_t status = spi_client_->Send(tx->data, tx->size);
  rsp->ret = status;
  *used += sizeof(struct virtio_cluster_rsp);
  return status;
}

VirtioCluster::VirtioCluster(const PhysMem& phys_mem)
    : VirtioDeviceBase(phys_mem),
      callback_state_(std::make_shared<CallbackState>()) {}

VirtioCluster::~VirtioCluster() {
  if (async_) {
    command_queue_wait_.Cancel(async_);
    async_ = nullptr;
  }

  if (callback_state_) {
    auto callback_state = callback_state_;
    callback_state->stopping.store(true);
    std::unique_lock<std::mutex> lock(callback_state->mutex);
    callback_state->idle.wait(
        lock, [callback_state] { return callback_state->in_flight == 0; });
    callback_state_.reset();
  }
}

zx_status_t VirtioCluster::Init(async_t* async, SpiTransportClient* spi_client) {
  async_ = async;
  spi_client_ = spi_client;
  zx_status_t status = queue(0)->PollAsync(
    async, &command_queue_wait_, &VirtioCluster::CommandQueueHandler, this);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to PollAsync: " << status;
    return status;
  }
  spi_client_->ReigsterRxCallback(
    [this, callback_state = callback_state_](const char* data, size_t size) {
      if (callback_state->stopping.load()) {
        return;
      }
      {
        std::lock_guard<std::mutex> lock(callback_state->mutex);
        if (callback_state->stopping.load()) {
          return;
        }
        callback_state->in_flight++;
      }

      zx_status_t status = NotifyGuestCluster(data, size, callback_state);
      if (status != ZX_OK && status != ZX_ERR_STOP) {
        FXL_LOG(ERROR) << "[virtio cluster]Failed to notify guest: "
                       << status;
      }

      {
        std::lock_guard<std::mutex> lock(callback_state->mutex);
        callback_state->in_flight--;
      }
      callback_state->idle.notify_all();
  });

  return status;
}

zx_status_t VirtioCluster::CommandQueueHandler(VirtioQueue* queue,
                                             uint16_t head,
                                             uint32_t* used,
                                             void* ctx) {
  auto thiz = reinterpret_cast<VirtioCluster*>(ctx);
  return thiz->HandleCommand(queue, head, used);
}

zx_status_t VirtioCluster::HandleCommand(VirtioQueue* queue,
                                       uint16_t head,
                                       uint32_t* used) {
  virtio_desc_t desc;
  zx_status_t err = ZX_OK;
  auto status = queue->ReadDesc(head, &desc);
  FXL_DCHECK(status == ZX_OK);

  auto tx = (struct virtio_cluster_tx*)desc.addr;
  *used = 0;

  if (desc.has_next) {
    head = desc.next;
    status = queue->ReadDesc(head, &desc);
    FXL_DCHECK(status == ZX_OK);

    if (desc.len != sizeof(struct virtio_cluster_rsp)) {
      FXL_LOG(ERROR) << "unexpected vq response buffer size: " << desc.len;
      return ZX_ERR_INTERNAL;
    }

    auto rsp = (struct virtio_cluster_rsp*)desc.addr;
    err = cluster_write(tx, rsp, used);
  }

  return err;
}

}  // namespace machina
