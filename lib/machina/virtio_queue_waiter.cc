// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_queue_waiter.h"

#include "lib/fxl/logging.h"

namespace machina {

VirtioQueueWaiter::VirtioQueueWaiter(async_t* async) : async_(async) {
  FXL_DCHECK(async_);
}

zx_status_t VirtioQueueWaiter::Wait(VirtioQueue* queue, Callback callback) {
  callback_ = fbl::move(callback);
  queue_ = queue;
  wait_.set_object(queue->event());
  wait_.set_trigger(VirtioQueue::SIGNAL_QUEUE_AVAIL);
  wait_.set_handler(fbl::BindMember(this, &VirtioQueueWaiter::Handler));
  zx_status_t status = wait_.Begin(async_);
  if (status != ZX_OK) {
    callback_ = Callback();
    queue_ = nullptr;
  }
  return status;
}

void VirtioQueueWaiter::Cancel() {
  wait_.Cancel(async_);
  callback_ = Callback();
  queue_ = nullptr;
}

async_wait_result_t VirtioQueueWaiter::Handler(
    async_t* async,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  uint16_t index = 0;
  if (status == ZX_OK) {
    status = queue_->NextAvail(&index);
    if (status == ZX_ERR_SHOULD_WAIT) {
      return ASYNC_WAIT_AGAIN;
    }
  }

  Callback callback = std::move(callback_);
  callback(status, index);

  return ASYNC_WAIT_FINISHED;
}

}  // namespace machina
