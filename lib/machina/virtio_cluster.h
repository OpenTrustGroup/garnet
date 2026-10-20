// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>
#include "garnet/lib/machina/virtio_device.h"
#include <virtio/virtio_ids.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include "garnet/public/lib/spi_transport/cpp/client.h"

namespace {
#define CLUSTER_MAX_BUF_SIZE 256
#define VIRTIO_CLUSTER_Q_COUNT 2
struct virtio_cluster_tx {
    size_t size;
    char data[CLUSTER_MAX_BUF_SIZE];
};

struct virtio_cluster_rsp {
    int32_t ret;
};

struct virtio_cluster_rx {
  size_t size;
	char buf[CLUSTER_MAX_BUF_SIZE];
};

}

namespace machina {

struct virtio_cluster_config_t {};

class VirtioCluster : public VirtioDeviceBase<VIRTIO_ID_CLUSTER, VIRTIO_CLUSTER_Q_COUNT,
    virtio_cluster_config_t> {
 public:
   VirtioCluster(const PhysMem& phys_mem);
  ~VirtioCluster() override;
  zx_status_t Init(async_t* async, SpiTransportClient* spi_client);
 private:
  struct CallbackState;

  zx_status_t cluster_write(struct virtio_cluster_tx *tx,
    struct virtio_cluster_rsp *rsp, uint32_t *used);
  static zx_status_t CommandQueueHandler(VirtioQueue* queue,
                                         uint16_t head,
                                         uint32_t* used,
                                         void* ctx);

  zx_status_t HandleCommand(VirtioQueue* queue, uint16_t head, uint32_t* used);
  zx_status_t NotifyGuestCluster(
      const char *buf,
      uint32_t len,
      const std::shared_ptr<CallbackState>& callback_state);
  SpiTransportClient* spi_client_ = nullptr;
  async::Wait command_queue_wait_;
  async_t* async_ = nullptr;
  std::shared_ptr<CallbackState> callback_state_;
};

}  // namespace machina
