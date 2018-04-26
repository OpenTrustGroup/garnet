// Copyright 2018 OpenTrustGroup. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/trusty/shared_mem.h"
#include "garnet/lib/trusty/tipc_device.h"
#include "garnet/lib/trusty/virtio_queue_fake.h"
#include "magma_util/simple_allocator.h"

namespace trusty {

class TipcFrontendFake;

// This class emulates a remote system that communicate with us
class TipcRemoteFake {
 public:
  static fbl::unique_ptr<TipcRemoteFake> Create(
      fbl::RefPtr<SharedMem> shared_mem) {
    auto alloc =
        magma::SimpleAllocator::Create(shared_mem->addr(), shared_mem->size());
    if (!alloc)
      return NULL;

    return fbl::unique_ptr<TipcRemoteFake>(
        new TipcRemoteFake(std::move(alloc), shared_mem));
  }

  zx_status_t HandleResourceTable(
      resource_table* table,
      const fbl::Vector<fbl::RefPtr<VirtioDevice>>& devs);

  void* AllocBuffer(size_t size) {
    uint64_t buf;
    if (alloc_->Alloc(size, 0, &buf))
      return reinterpret_cast<void*>(buf);
    else
      return nullptr;
  }

  void FreeBuffer(void* addr) {
    alloc_->Free(reinterpret_cast<uint64_t>(addr));
  }

  uintptr_t VirtToPhys(void* addr, size_t size) {
    return shared_mem_->VirtToPhys(addr, size);
  }

  TipcFrontendFake* GetFrontend(uint32_t notify_id);

 private:
  TipcRemoteFake(std::unique_ptr<magma::SimpleAllocator> alloc,
                 fbl::RefPtr<SharedMem> shared_mem)
      : alloc_(std::move(alloc)), shared_mem_(shared_mem) {}

  std::unique_ptr<magma::SimpleAllocator> alloc_;

  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::Vector<fbl::unique_ptr<TipcFrontendFake>> frontends_;
};

// This class emulates a fake tipc frontend on the remote system
class TipcFrontendFake {
 public:
  TipcFrontendFake(TipcDevice* device, TipcRemoteFake* remote)
      : rx_queue_(device->tx_queue()),
        tx_queue_(device->rx_queue()),
        notify_id_(device->notify_id()),
        remote_(remote) {}
  ~TipcFrontendFake() {}

  zx_status_t Init(fw_rsc_vdev* vdev) {
    zx_status_t status;

    // Tipc Tx Queue is our Rx Queue
    status = AllocVring(&rx_queue_, &vdev->vring[kTipcTxQueue]);
    if (status != ZX_OK)
      return status;

    // Tipc Rx Queue is our Tx Queue
    status = AllocVring(&tx_queue_, &vdev->vring[kTipcRxQueue]);
    if (status != ZX_OK)
      return status;

    vdev->status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_DRIVER_OK;
    return ZX_OK;
  }

  VirtioQueueFake& rx_queue() { return rx_queue_; }
  VirtioQueueFake& tx_queue() { return tx_queue_; }
  uint32_t notify_id() { return notify_id_; }

 private:
  zx_status_t AllocVring(VirtioQueueFake* queue, fw_rsc_vdev_vring* vring) {
    size_t size = vring_size(vring->num, vring->align);

    auto buf = remote_->AllocBuffer(size);
    if (!buf) {
      return ZX_ERR_NO_MEMORY;
    }

    vring->da = remote_->VirtToPhys(buf, size);
    queue->Init(vring);

    return ZX_OK;
  }

  VirtioQueueFake rx_queue_;
  VirtioQueueFake tx_queue_;
  uint32_t notify_id_;
  TipcRemoteFake* remote_ = nullptr;
};

}  // namespace trusty
