// virtio_block.cc
// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtio_block.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <trusty_std.h>

#include <block-client/client.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <virtio/virtio_ids.h>
#include <virtio/virtio_ring.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>

#include "lib/fxl/logging.h"

#define PERF_BIND_CPU_TEMPORARILY 1

namespace machina {

namespace {
constexpr uint32_t kDefaultGeometry = 128;
constexpr uintptr_t kNormalPriority = 16;
constexpr uintptr_t kVblockPriority = kNormalPriority + 1;
#if PERF_BIND_CPU_TEMPORARILY
constexpr uint32_t kBigClusterCpuMask = 0xf0;
constexpr uint32_t kCpuMaskPoll = kBigClusterCpuMask;
constexpr uint32_t kCpuMaskCq = kBigClusterCpuMask;
#endif
constexpr uint16_t kProducerBatchSize = FIFO_QDEPTH;
constexpr size_t kCompletionBatchSize = FIFO_QDEPTH;

uint8_t VirtioBlockStatus(zx_status_t status) {
  switch (status) {
  case ZX_OK:
    return VIRTIO_BLK_S_OK;
  case ZX_ERR_NOT_SUPPORTED:
    return VIRTIO_BLK_S_UNSUPP;
  default:
    return VIRTIO_BLK_S_IOERR;
  }
}

union VblockCookie {
  uint64_t raw;
  struct {
    uint32_t queue_index;
    uint32_t used_bytes;
  } data;
};

struct RequestPollArgs {
  VirtioBlock *vm;
  uint16_t qidx;
};

inline zx_vaddr_t GuestPaddrToHostVaddr(VirtioDevice *device,
                                        zx_paddr_t guest_paddr) {
  return static_cast<zx_vaddr_t>(
      device->phys_mem().addr() +
      (guest_paddr - device->phys_mem().phys_base()));
}

int PollRequestThread(void *ctx) {
  auto *args = static_cast<RequestPollArgs *>(ctx);
  VirtioBlock *vm = args->vm;
  uint16_t qidx = args->qidx;
  delete args;

#if PERF_BIND_CPU_TEMPORARILY
  uint32_t cpu_mask = kCpuMaskPoll;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY,
                &cpu_mask);
#endif

  VirtioQueue *vq = vm->RequestQueue(qidx);

  while (!vm->IsStopping()) {
    uint16_t heads[kProducerBatchSize];
    uint16_t count = 0;

    zx_status_t status = vq->TryDequeueBatch(heads, kProducerBatchSize, &count);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Batch dequeue failed: " << status;
      break;
    }

    if (count == 0) {
      uint16_t head;
      if (vq->Wait(&head) != ZX_OK) break;
      heads[0] = head;
      count = 1;
    }

    fifo_in_item items[kProducerBatchSize];
    VblockCookie cookies[kProducerBatchSize];
    uint32_t req_ids[kProducerBatchSize];
    uint16_t valid = 0;

    for (uint16_t i = 0; i < count; ++i) {
      fifo_in_item it;
      VblockCookie ck;
      bool queued = false;
      status = vm->HandleBlockRequestAsyncBatch(vq, heads[i], &it, &ck.raw,
                                                &queued);
      if (status == ZX_OK && queued) {
        items[valid] = it;
        cookies[valid] = ck;
        req_ids[valid] = heads[i];
        ++valid;
      }
    }

    if (valid > 0) {
      status = vm->Dispatcher()->SubmitBatch(items, valid,
                                            (uint64_t*)cookies, req_ids);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Batch submit failed: " << status;
        break;
      }
    }
  }
  return 0;
}

int CompletionPollThread(void *ctx) {
  auto *vm = static_cast<VirtioBlock *>(ctx);

#if PERF_BIND_CPU_TEMPORARILY
  uint32_t cpu_mask = kCpuMaskCq;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY,
                &cpu_mask);
#endif
  zx_thread_set_priority(kVblockPriority);

  while (!vm->IsStopping()) {
    fifo_out_item items[kCompletionBatchSize];
    size_t count = 0;
    bool queue0_needs_notify = false;

    zx_status_t status =
        vm->Dispatcher()->WaitForCompletionBatch(items, kCompletionBatchSize, &count);
    if (status != ZX_OK) {
      if (status != ZX_ERR_CANCELED && status != ZX_ERR_PEER_CLOSED) {
        FXL_LOG(ERROR) << "virtio_block: batch completion failed: " << status;
      }
      break;
    }

    for (size_t i = 0; i < count; ++i) {
      VblockCookie cookie;
      cookie.raw = vm->Dispatcher()->GetCompletionCookie(items[i].head);
      auto vq = vm->RequestQueue(cookie.data.queue_index);
      status = vq->Return(items[i].head, cookie.data.used_bytes,
                          VirtioQueue::InterruptAction::SET_FLAGS);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "virtio_block: batch return failed: " << status;
        break;
      }
      if (cookie.data.queue_index == 0) {
        queue0_needs_notify = true;
      }
    }

    if (status != ZX_OK) {
      break;
    }

    if (queue0_needs_notify) {
      status = vm->NotifyGuest(0);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "virtio_block: batch notify failed: " << status;
        break;
      }
    }
  }
  return 0;
}
} // namespace

VirtioBlock::VirtioBlock(const PhysMem &phys_mem, Transport transport)
    : VirtioDeviceBase(phys_mem, transport) {
  // Virtio 1.0: 5.2.5.2: Devices SHOULD always offer VIRTIO_BLK_F_FLUSH
  add_device_features(VIRTIO_BLK_F_FLUSH |
                      VIRTIO_BLK_F_BLK_SIZE // Required by zircon guests.
                      | (VIRTIO_BLK_F_SEG_MAX) | (VIRTIO_BLK_F_SIZE_MAX));

  if (transport == Transport::MMIO) {
    auto mmio_dev = mmio_device();
    mmio_dev->RegisterDriverReadyHandler(
        std::bind(&VirtioBlock::Activate, this));
    mmio_dev->RegisterDriverResetHandler(std::bind(&VirtioBlock::Stop, this));
    FXL_LOG(INFO) << "set mmio ready handler";
  }
}

VirtioBlock::~VirtioBlock() { Stop(); }

const char *VirtioBlock::cfg_name(uint64_t off) const {
  switch (off) {
  case 0x0 ... 0x7:
    return "capacity";
  case 0x8 ... 0xb:
    return "size_max";
  case 0xc ... 0xf:
    return "seg_max";
  case 0x10 ... 0x13:
    return "geometry";
  case 0x14 ... 0x17:
    return "blk_size";
  case 0x18 ... 0x1a:
    return "num_queues";
  default:
    return nullptr;
  }
}

zx_status_t
VirtioBlock::SetDispatcher(fbl::unique_ptr<BlockDispatcher> dispatcher) {
  if (dispatcher_ != nullptr) {
    FXL_LOG(ERROR) << "Block device has already been initialized";
    return ZX_ERR_BAD_STATE;
  }

  dispatcher_ = fbl::move(dispatcher);

  if (IsAsyncFifo()) {
    fbl::AutoLock lock(&config_mutex_);
    bool crypto = false;
    dispatcher_->SetBlkConfig(config_, crypto);
    // The legacy vblock-drv backend consumes one data descriptor per request.
    // PVBlock has its own device class and native-SG implementation, so keep
    // the legacy virtio advertisement at one segment.
    config_.seg_max = 1;
    if (crypto) {
      add_device_features(VIRTIO_BLK_F_INLINE_CRYPTO);
    }
    add_device_features(VIRTIO_BLK_F_UFS_LUN);
    add_device_features(VIRTIO_RING_F_INDIRECT_DESC | VIRTIO_BLK_F_CONFIG_WCE |
                        VIRTIO_BLK_F_DISCARD);
    dispatcher_->SetCtxStore(kVirtioBlockQdepth);
  } else {
    fbl::AutoLock lock(&config_mutex_);
    config_.capacity = dispatcher_->size() / dispatcher_->blk_size();
    config_.blk_size = dispatcher_->blk_size();
    config_.seg_max = dispatcher_->max_seg_nums();
    config_.num_queues = 1;
    config_.size_max = dispatcher_->max_seg_size();
    config_.geometry.cylinders =
        dispatcher_->size() / kSectorSize / kDefaultGeometry / kDefaultGeometry;
    config_.geometry.heads = kDefaultGeometry;
    config_.geometry.sectors = kDefaultGeometry;
  }

  if (dispatcher_->read_only()) {
    add_device_features(VIRTIO_BLK_F_RO);
  }
  return ZX_OK;
}

void VirtioBlock::Activate() {
  Start();
  FXL_LOG(INFO) << "virtio block started";
}

void VirtioBlock::Stop() {
  SetStopping(true);

  if (queue(0)) {
    // stop virtqueue Poll task thread and virtio_block's poll req thread first
    queue(0)->Terminate();
  }

  if (IsAsyncFifo()) {
    if (poll_thread_ != 0) {
      thrd_join(poll_thread_, nullptr);
      poll_thread_ = 0;
      FXL_LOG(INFO) << "virtio_block: poll_thread_ joined successfully.";
    }

    if (dispatcher_) {
      dispatcher_->Shutdown();
    }

    if (complt_thread_ != 0) {
      thrd_join(complt_thread_, nullptr);
      complt_thread_ = 0;
      FXL_LOG(INFO) << "virtio_block: complt_thread_ joined successfully.";
    }

  } else {
    if (queue(0)) {
      // join virtio queue Poll task thread
      queue(0)->Join();
      FXL_LOG(INFO) << "virtio_block: fdio vq poll thread joined successfully.";
    }
  }
  dispatcher_.reset();
}

zx_status_t VirtioBlock::Start() {
  if (!dispatcher_) {
    FXL_LOG(ERROR) << "Cannot start VirtioBlock: dispatcher not set.";
    return ZX_ERR_BAD_STATE;
  }

  if (IsAsyncFifo()) {
    auto args = new RequestPollArgs{this, 0};

    zx_status_t ret = thrd_create_with_name(&poll_thread_, PollRequestThread,
                                            args, "vblock_poll");
    if (ret != thrd_success) {
      delete args;
      FXL_LOG(ERROR) << "block_dispatcher@" << dispatcher_->Vmid()
                     << ", failed to create poll worker, ret:" << ret;
      return ret;
    }

    ret = thrd_create_with_name(&complt_thread_, CompletionPollThread, this,
                                "vblock_complt");
    if (ret != thrd_success) {
      FXL_LOG(ERROR) << "block_dispatcher@" << dispatcher_->Vmid()
                     << ", failed to create completion worker, ret:" << ret;
    }
    return ret;
  } else {
    auto poll_func =
        +[](VirtioQueue *queue, uint16_t head, uint32_t *used, void *ctx) {
          return static_cast<VirtioBlock *>(ctx)->HandleBlockRequest(
              queue, head, used);
        };
    return queue(0)->Poll(poll_func, this, "virtio-block");
  }
}

void VirtioBlock::FastWriteStatus(VirtioQueue *queue,
                                  const virtio_desc_t &desc_lv1,
                                  uint8_t status) {
  VirtioDevice *device = queue->device();
  virtio_desc_t desc = desc_lv1;

  if (device->has_device_features(VIRTIO_RING_F_INDIRECT_DESC) &&
      desc_lv1.indirect) {
    const auto *vring_desc =
        reinterpret_cast<const struct vring_desc *>(desc_lv1.addr);
    uint32_t desc_cnt = desc_lv1.len / sizeof(struct vring_desc);

    if (desc_cnt < 2) {
      FXL_LOG(ERROR) << "virtio_block@" << dispatcher_->Vmid()
                     << ", indirect, err vring desc cnt: " << desc_cnt;
      return;
    }

    if (vring_desc[desc_cnt - 1].len != 1) {
      FXL_LOG(ERROR) << "virtio_block@" << dispatcher_->Vmid()
                     << ", err status vring desc len: "
                     << vring_desc[desc_cnt - 1].len;
    }

    auto *status_addr = reinterpret_cast<uint8_t *>(
        GuestPaddrToHostVaddr(device, vring_desc[desc_cnt - 1].addr));
    status_addr[0] = status;
    return;
  }

  FXL_LOG(WARNING) << "virtio_block@" << dispatcher_->Vmid()
                   << ", not indirect desc";

  // Safe iteration loop to prevent infinite hangs on desc read errors
  while (true) {
    if (desc.has_next) {
      if (queue->ReadDesc(desc.next, &desc) != ZX_OK) {
        FXL_LOG(ERROR) << "virtio_block@" << dispatcher_->Vmid()
                       << ", FastWriteStatus failed to read desc";
        return;
      }
      continue;
    }

    if (desc.len == 1 && desc.writable) {
      auto *status_ptr = static_cast<uint8_t *>(desc.addr);
      status_ptr[0] = status;
      return;
    }

    FXL_LOG(ERROR) << "virtio_block@" << dispatcher_->Vmid()
                   << ", direct, err vring desc";
    return;
  }
}

zx_status_t VirtioBlock::HandleBlockIoctlAsync(
    VirtioQueue *queue, uint16_t head, const virtio_desc_t &desc_lv1,
    const struct vring_desc *vring_desc, uint16_t desc_cnt) {
  auto complete_with_error = [&](uint8_t block_status, const char *err_msg) {
    FXL_LOG(ERROR) << "virtio_block@" << dispatcher_->Vmid() << ": "
                   << err_msg;
    FastWriteStatus(queue, desc_lv1, block_status);
    queue->Return(head, sizeof(struct vring_desc));
    return ZX_ERR_IO;
  };

  if (desc_cnt < 4 || vring_desc[1].len != sizeof(uint32_t)) {
    return complete_with_error(VIRTIO_BLK_S_IOERR, "Bad ioctl desc");
  }

  VirtioDevice *device = queue->device();
  const auto *request_ptr = reinterpret_cast<const uint32_t *>(
      GuestPaddrToHostVaddr(device, vring_desc[1].addr));
  if (!request_ptr) {
    return complete_with_error(VIRTIO_BLK_S_IOERR,
                               "Bad ioctl request addr");
  }

  auto *arg = reinterpret_cast<uint8_t *>(
      GuestPaddrToHostVaddr(device, vring_desc[2].addr));
  const uint32_t arg_len = vring_desc[2].len;
  if (arg_len != 0 && !arg) {
    return complete_with_error(VIRTIO_BLK_S_IOERR,
                               "Bad ioctl split arg addr");
  }

  const bool arg_writable =
      (vring_desc[2].flags & VRING_DESC_F_WRITE) != 0;
  zx_status_t status = dispatcher_->ioctl(*request_ptr, arg, arg_len);
  FastWriteStatus(queue, desc_lv1, VirtioBlockStatus(status));

  uint32_t used = 1;
  if (status == ZX_OK && arg_writable) {
    used += arg_len;
  }
  queue->Return(head, used);
  return ZX_OK;
}

zx_status_t VirtioBlock::HandleBlockRequestAsyncBatch(
    VirtioQueue* queue, uint16_t head,
    fifo_in_item* out_item, uint64_t *cookie, bool *queued) {
  virtio_desc_t desc;
  VblockCookie* out_cookie = (VblockCookie *)cookie;
  struct phys_range desc_phy[1];
  VirtioDevice* device = queue->device();
  *queued = false;

  zx_status_t status = queue->ReadDesc(head, &desc, desc_phy);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "virtio_block@" << dispatcher_->Vmid()
                   << ", failed to read desc: " << status;
    return status;
  }

  auto set_error = [&](uint8_t block_status, const char *err_msg) {
    FXL_LOG(ERROR) << "virtio_block@" << dispatcher_->Vmid() << ": " << err_msg;
    FastWriteStatus(queue, desc, block_status);
    queue->Return(head, sizeof(struct vring_desc));
    return ZX_ERR_IO;
  };

  if (!device->has_device_features(VIRTIO_RING_F_INDIRECT_DESC) ||
      !desc.indirect) {
    set_error(VIRTIO_BLK_S_IOERR, "Not indirect mode");
    return ZX_ERR_NOT_SUPPORTED;
  }

  const auto* vring_desc =
      reinterpret_cast<const struct vring_desc*>(desc.addr);
  uint16_t desc_cnt = desc.len / sizeof(struct vring_desc);

  if (desc_cnt < 2) {
    set_error(VIRTIO_BLK_S_IOERR, "Fatal error: desc_cnt < 2");
    return ZX_ERR_INVALID_ARGS;
  }
  if (vring_desc[0].len != sizeof(virtio_blk_req_t)) {
    set_error(VIRTIO_BLK_S_IOERR, "Bad desc, size unmatch");
    return ZX_ERR_INVALID_ARGS;
  }

  const auto* req = reinterpret_cast<const virtio_blk_req_t*>(
      GuestPaddrToHostVaddr(device, vring_desc[0].addr));

  if (req != nullptr && req->type == VIRTIO_BLK_T_IOCTL) {
    return HandleBlockIoctlAsync(queue, head, desc, vring_desc, desc_cnt);
  }

  if (req != nullptr && req->type == VIRTIO_BLK_T_OUT && IsReadOnly()) {
    set_error(VIRTIO_BLK_S_IOERR,
              "Err: try to write to read-only disk");
    return ZX_ERR_ACCESS_DENIED;
  }

  out_item->addr = desc_phy->paddr;
  out_item->len  = desc_cnt;
  out_item->head = head;

  out_cookie->data.queue_index = queue->index();
  out_cookie->data.used_bytes = static_cast<uint32_t>(sizeof(struct vring_desc));

  *queued = true;
  return ZX_OK;
}

zx_status_t VirtioBlock::HandleBlockRequestAsync(VirtioQueue *queue,
                                                 uint16_t head) {
  virtio_desc_t desc;
  struct phys_range desc_phy[1];
  VirtioDevice *device = queue->device();

  zx_status_t status = queue->ReadDesc(head, &desc, desc_phy);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "virtio_block@" << dispatcher_->Vmid()
                   << ", failed to read desc: " << status;
    return status;
  }

  auto complete_with_error = [&](uint8_t block_status, const char *err_msg) {
    FXL_LOG(ERROR) << "virtio_block@" << dispatcher_->Vmid() << ": " << err_msg;
    FastWriteStatus(queue, desc, block_status);
    queue->Return(head, sizeof(struct vring_desc));
    return ZX_ERR_IO;
  };

  if (!device->has_device_features(VIRTIO_RING_F_INDIRECT_DESC) ||
      !desc.indirect) {
    return complete_with_error(VIRTIO_BLK_S_IOERR, "Not indirect mode");
  }

  const auto *vring_desc =
      reinterpret_cast<const struct vring_desc *>(desc.addr);
  uint16_t desc_cnt = desc.len / sizeof(struct vring_desc);

  if (desc_cnt < 2) {
    return complete_with_error(VIRTIO_BLK_S_IOERR, "Fatal error: desc_cnt < 2");
  }

  if (vring_desc[0].len != sizeof(virtio_blk_req_t)) {
    return complete_with_error(VIRTIO_BLK_S_IOERR, "Bad desc, size unmatch");
  }

  const auto *req = reinterpret_cast<const virtio_blk_req_t *>(
      GuestPaddrToHostVaddr(device, vring_desc[0].addr));

  if (req != nullptr && req->type == VIRTIO_BLK_T_IOCTL) {
    return HandleBlockIoctlAsync(queue, head, desc, vring_desc, desc_cnt);
  }

  if (req != nullptr && req->type == VIRTIO_BLK_T_OUT && IsReadOnly()) {
    return complete_with_error(VIRTIO_BLK_S_IOERR,
                               "Err: try to write to read-only disk");
  }

  VblockCookie cookie = {};
  cookie.data.queue_index = queue->index();
  cookie.data.used_bytes = static_cast<uint32_t>(sizeof(struct vring_desc));

  return dispatcher_->SubmitAsync(desc_phy->paddr, desc_cnt, head, cookie.raw);
}

zx_status_t VirtioBlock::HandleBlockRequest(VirtioQueue *queue, uint16_t head,
                                            uint32_t *used) {
  uint8_t block_status = VIRTIO_BLK_S_OK;
  uint8_t *block_status_ptr = nullptr;
  const virtio_blk_req_t *req = nullptr;
  off_t offset = 0;
  virtio_desc_t desc;

  zx_status_t status = queue->ReadDesc(head, &desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "virtio_block, read desc failed";
    return status;
  }

  if (desc.len == sizeof(virtio_blk_base_req_t) ||
      desc.len == sizeof(virtio_blk_req_t)) {
    req = static_cast<const virtio_blk_req_t *>(desc.addr);
  } else {
    FXL_LOG(ERROR) << "virtio_block, unmatched req hdr, len: " << desc.len;
    block_status = VIRTIO_BLK_S_IOERR;
  }

  // VIRTIO 1.0 Section 5.2.6.2: A device MUST set the status byte to
  // VIRTIO_BLK_S_IOERR for a write request if the VIRTIO_BLK_F_RO feature
  // if offered, and MUST NOT write any data.
  if (req != nullptr && req->type == VIRTIO_BLK_T_OUT && IsReadOnly()) {
    FXL_LOG(ERROR) << "virtio_block, write read-only disk";
    block_status = VIRTIO_BLK_S_IOERR;
  }

  // VIRTIO Version 1.0: A driver MUST set sector to 0 for a
  // VIRTIO_BLK_T_FLUSH request. A driver SHOULD NOT include any data in a
  // VIRTIO_BLK_T_FLUSH request.
  if (req != nullptr && req->type == VIRTIO_BLK_T_FLUSH && req->sector != 0) {
    block_status = VIRTIO_BLK_S_IOERR;
    FXL_LOG(ERROR) << "virtio_block, flush operation with un-zero sector";
  }

  // VIRTIO 1.0 Section 5.2.5.2: If the VIRTIO_BLK_F_BLK_SIZE feature is
  // negotiated, blk_size can be read to determine the optimal sector size
  // for the driver to use. This does not affect the units used in the
  // protocol (always 512 bytes), but awareness of the correct value can
  // affect performance.
  if (req != nullptr) {
    offset = req->sector * kSectorSize;
  }
  struct phys_range desc_phy[1];
  while (desc.has_next) {
    status = queue->ReadDesc(desc.next, &desc, desc_phy);
    if (status != ZX_OK) {
      block_status =
          block_status != VIRTIO_BLK_S_OK ? block_status : VIRTIO_BLK_S_IOERR;
      FXL_LOG(ERROR) << "virtio block, read resc failed";
      break;
    }

    // Requests should end with a single 1b status byte.
    if (desc.len == 1 && desc.writable && !desc.has_next) {
      block_status_ptr = static_cast<uint8_t *>(desc.addr);
      break;
    }

    // Skip doing any file ops if we've already encountered an error, but
    // keep traversing the descriptor chain looking for the status tailer.
    if (block_status != VIRTIO_BLK_S_OK) {
      FXL_LOG(ERROR) << "virtio block io error chain traversal, line: "
                     << __LINE__;
      continue;
    }

    zx_status_t status;
    switch (req->type) {
    case VIRTIO_BLK_T_IN: {
      if (desc.len % kSectorSize != 0) {
        block_status = VIRTIO_BLK_S_IOERR;
        FXL_LOG(ERROR) << "virtio block, read, unaligned desc";
        continue;
      }
      status = dispatcher_->Read(
          offset, reinterpret_cast<void *>(desc_phy->paddr), desc_phy->len);
      *used += desc.len;
      offset += desc.len;
      if (status != ZX_OK)
        FXL_LOG(ERROR) << "virtio block io read error, status: " << status;
      break;
    }
    case VIRTIO_BLK_T_OUT: {
      if (desc.len % kSectorSize != 0) {
        block_status = VIRTIO_BLK_S_IOERR;
        FXL_LOG(ERROR) << "virtio block, write, unaligned desc";
        continue;
      }
      status = dispatcher_->Write(offset, desc.addr, desc.len);
      offset += desc.len;
      if (status != ZX_OK)
        FXL_LOG(ERROR) << "virtio block io write error, status: " << status;
      break;
    }
    case VIRTIO_BLK_T_FLUSH:
      status = dispatcher_->Flush();
      if (status != ZX_OK)
        FXL_LOG(ERROR) << "virtio block, flush error";
      break;
    case VIRTIO_BLK_T_IOCTL:
      if (desc.len < sizeof(uint32_t)) {
        block_status = VIRTIO_BLK_S_IOERR;
        FXL_LOG(ERROR) << "virtio block, ioctl arg desc too small: "
                       << desc.len;
        continue;
      }
      {
        auto *request = static_cast<uint32_t *>(desc.addr);
        auto *arg = reinterpret_cast<uint8_t *>(desc.addr) + sizeof(*request);
        const size_t ioctl_arg_len = desc.len - sizeof(*request);
        status = dispatcher_->ioctl(*request, arg, ioctl_arg_len);
      }
      if (status != ZX_OK)
        FXL_LOG(ERROR) << "virtio block, ioctl error: " << status
                       << ", request: " << *static_cast<uint32_t *>(desc.addr);
      if (status == ZX_OK && desc.writable)
        *used += desc.len - sizeof(uint32_t);
      break;
    default:
      block_status = VIRTIO_BLK_S_UNSUPP;
      FXL_LOG(ERROR) << "virtio block, unsupported operation";
      break;
    }

    // Report any failures queuing the IO request.
    if (block_status == VIRTIO_BLK_S_OK && status != ZX_OK) {
      block_status = status == ZX_ERR_NOT_SUPPORTED ? VIRTIO_BLK_S_UNSUPP
                                                    : VIRTIO_BLK_S_IOERR;
      FXL_LOG(ERROR) << "virtio block, operation not ok";
    }
  }

  // Wait for operations to become consistent.
  status = dispatcher_->Submit();
  if (block_status == VIRTIO_BLK_S_OK && status != ZX_OK) {
    block_status = VIRTIO_BLK_S_IOERR;
  }

  // Set the output status if we found the byte in the descriptor chain.
  if (block_status_ptr != nullptr) {
    *block_status_ptr = block_status;
    ++*used;
  }
  return ZX_OK;
}

} // namespace machina
