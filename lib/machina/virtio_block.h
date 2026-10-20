// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_BLOCK_H_
#define GARNET_LIB_MACHINA_VIRTIO_BLOCK_H_

#include <atomic>
#include <memory>

#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <virtio/vblock.h>
#include <virtio/virtio_ids.h>

#include "garnet/lib/machina/block_dispatcher.h"
#include "garnet/lib/machina/virtio_device.h"

namespace machina {

constexpr uint16_t kVirtioBlockQdepth = 256;
constexpr uint16_t kVirtioBlockQnum = 1;

// Stores the state of a block device.
class VirtioBlock
    : public VirtioDeviceBase<VIRTIO_ID_BLOCK, kVirtioBlockQnum,
                              virtio_blk_config_t, kVirtioBlockQdepth> {
public:
  static constexpr size_t kSectorSize = 512;

  VirtioBlock(const PhysMem &phys_mem, Transport transport = Transport::MMIO);
  ~VirtioBlock() override;

  // Set the dispatcher to use to interface with the back-end.
  zx_status_t SetDispatcher(fbl::unique_ptr<BlockDispatcher> dispatcher);

  // Starts a thread to monitor the queue for incoming block requests.
  zx_status_t Start();
  void Activate();
  void Stop();

  // Our config space is read-only.
  zx_status_t WriteConfig(uint64_t /*addr*/,
                          const IoValue & /*value*/) override {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t HandleBlockRequest(VirtioQueue *queue, uint16_t head,
                                 uint32_t *used);

  bool IsReadOnly() { return has_device_features(VIRTIO_BLK_F_RO); }

  // The queue used for handling block requests.
  VirtioQueue *RequestQueue(uint16_t qidx = 0) { return queue(qidx); }

  const char *cfg_name(uint64_t off) const override;

  void SetStopping(bool val) {
    stopping_.store(val, std::memory_order_release);
  }
  bool IsStopping() const { return stopping_.load(std::memory_order_acquire); }

  zx_status_t HandleBlockRequestAsyncBatch(VirtioQueue* queue, uint16_t head,
                                           fifo_in_item* out_item,
                                           uint64_t *cookie, bool *queued);

  zx_status_t HandleBlockRequestAsync(VirtioQueue *queue, uint16_t head);

  BlockDispatcher *Dispatcher() const { return dispatcher_.get(); }

  bool IsAsyncFifo() const { return dispatcher_ && dispatcher_->IsAsyncMode(); }

private:
  void FastWriteStatus(VirtioQueue *queue, const virtio_desc_t &desc_lv1,
                       uint8_t status);
  zx_status_t HandleBlockIoctlAsync(VirtioQueue *queue, uint16_t head,
                                    const virtio_desc_t &desc_lv1,
                                    const struct vring_desc *vring_desc,
                                    uint16_t desc_cnt);
  zx_status_t CompletionWorker(uint16_t qidx = 0);

  fbl::unique_ptr<BlockDispatcher> dispatcher_;
  std::atomic<bool> stopping_{false};
  thrd_t poll_thread_ = 0;
  thrd_t complt_thread_ = 0;
};

} // namespace machina

#endif // GARNET_LIB_MACHINA_VIRTIO_BLOCK_H_
