// Copyright 2018 OpenTrustGroup. All rights reserved.
// Copyright 2015 Google, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/async/cpp/wait.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zx/channel.h>

#include "garnet/lib/gzos/trusty_virtio/virtio_device.h"
#include "garnet/lib/gzos/trusty_virtio/virtio_queue_waiter.h"

namespace trusty_virtio {

static constexpr uint32_t kTipcDeviceId = 13;

static constexpr char kMaxNameLength = 32;

static constexpr uint8_t kTxQueue = 0;
static constexpr uint8_t kRxQueue = 1;
static constexpr uint8_t kNumQueues = 2;
static_assert(kNumQueues % 2 == 0, "There must be a queue for both RX and TX");

// Trusty IPC device configuration shared with linux side
struct trusty_vdev_config {
  uint32_t msg_buf_max_size;      // max msg size that this device can handle
  uint32_t msg_buf_alignment;     // required msg alignment (PAGE_SIZE)
  char dev_name[kMaxNameLength];  // NS device node name
} __PACKED;

struct trusty_vdev_descr {
  struct fw_rsc_hdr hdr;
  struct fw_rsc_vdev vdev;
  struct fw_rsc_vdev_vring vrings[kNumQueues];
  struct trusty_vdev_config config;
} __PACKED;

#define DECLARE_TRUSTY_VIRTIO_DEVICE_DESCR(_id, _nd_name, _txvq_sz, _rxvq_sz) \
  {                                                                           \
    .hdr.type = RSC_VDEV,                                                     \
    .vdev =                                                                   \
        {                                                                     \
            .id = _id,                                                        \
            .notifyid = 0,                                                    \
            .dfeatures = 0,                                                   \
            .config_len = sizeof(struct trusty_vdev_config),                  \
            .num_of_vrings = kNumQueues,                                      \
        },                                                                    \
    .vrings =                                                                 \
        {                                                                     \
            [kTxQueue] =                                                      \
                {                                                             \
                    .align = PAGE_SIZE,                                       \
                    .num = (_txvq_sz),                                        \
                    .notifyid = kTxQueue,                                     \
                },                                                            \
            [kRxQueue] =                                                      \
                {                                                             \
                    .align = PAGE_SIZE,                                       \
                    .num = (_rxvq_sz),                                        \
                    .notifyid = kRxQueue,                                     \
                },                                                            \
        },                                                                    \
    .config = {                                                               \
      .msg_buf_max_size = PAGE_SIZE,                                          \
      .msg_buf_alignment = PAGE_SIZE,                                         \
      .dev_name = _nd_name,                                                   \
    }                                                                         \
  }

class TrustyVirtioDevice : public VirtioDevice {
 public:
  explicit TrustyVirtioDevice(const trusty_vdev_descr& descr, async_t* async,
                              zx::channel channel);
  ~TrustyVirtioDevice() override {}

  VirtioQueue* tx_queue() { return &queues_[kTxQueue]; }
  VirtioQueue* rx_queue() { return &queues_[kRxQueue]; }

 protected:
  size_t ResourceEntrySize(void) override { return sizeof(trusty_vdev_descr); }
  void GetResourceEntry(void* rsc_entry) override {
    memcpy(rsc_entry, &descr_, sizeof(descr_));
  };
  zx_status_t Probe(void* rsc_entry) override;
  zx_status_t Reset() override;
  zx_status_t Kick(uint32_t vq_id) override;

 private:
  TrustyVirtioDevice();

  zx_status_t ValidateDescriptor(trusty_vdev_descr* descr);
  zx_status_t InitializeVring(fw_rsc_vdev* descr);

  // Queue for handling block requests.
  VirtioQueue queues_[kNumQueues];
  // Resource entry descriptor for this device
  trusty_vdev_descr descr_ = {};

  // Represents an single, unidirectional TX or RX channel.
  class Stream {
   public:
    Stream(async_t* async, VirtioQueue* queue, zx_handle_t channel);
    zx_status_t Start();
    void Stop();

   private:
    zx_status_t WaitOnQueue();
    void OnQueueReady(zx_status_t status, uint16_t index);
    zx_status_t WaitOnChannel();
    void OnChannelReady(async_t* async, async::WaitBase* wait,
                        zx_status_t status, const zx_packet_signal_t* signal);

    void OnStreamClosed(zx_status_t status, const char* action);
    void DropBuffer();

    async_t* async_;
    zx_handle_t channel_;
    VirtioQueue* queue_;
    VirtioQueueWaiter queue_wait_;
    async::WaitMethod<Stream, &Stream::OnChannelReady> channel_wait_{this};
    uint16_t head_;
    virtio_desc_t desc_;
  };

  zx::channel channel_;
  Stream rx_stream_;
  Stream tx_stream_;
};

}  // namespace trusty_virtio
