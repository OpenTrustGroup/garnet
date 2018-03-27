// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trusty/virtio_queue_fake.h"

#include <string.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>
#include "lib/fxl/logging.h"

#include "garnet/lib/trusty/third_party/remoteproc/remoteproc.h"
#include "garnet/lib/trusty/virtio_device.h"

namespace trusty {

void VirtioQueueFake::Init(fw_rsc_vdev_vring* vring_descr) {
  queue_size_ = vring_descr->num;
  uintptr_t paddr = vring_descr->da;

  vring vring;
  vring_init(&vring, vring_descr->num, (void*)paddr, vring_descr->align);

  queue_->set_size(queue_size_);
  queue_->set_desc_addr(reinterpret_cast<uintptr_t>(vring.desc));
  queue_->set_avail_addr(reinterpret_cast<uintptr_t>(vring.avail));
  queue_->set_used_addr(reinterpret_cast<uintptr_t>(vring.used));
}

VirtioQueueFake::~VirtioQueueFake() {
  queue_->set_desc_addr(0);
  queue_->set_avail_addr(0);
  queue_->set_used_addr(0);
  queue_->set_size(0);
}

zx_status_t VirtioQueueFake::SetNext(uint16_t desc_index, uint16_t next_index) {
  if (desc_index >= queue_size_) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (next_index >= queue_size_) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto& desc =
      const_cast<volatile vring_desc&>(queue_->ring()->desc[desc_index]);
  desc.flags |= VRING_DESC_F_NEXT;
  desc.next = next_index;
  return ZX_OK;
}

zx_status_t VirtioQueueFake::WriteDescriptor(void* buf,
                                             size_t len,
                                             uint16_t flags,
                                             uint16_t* desc_out) {
  uint16_t desc_index = next_free_desc_;
  if (desc_index >= queue_size_) {
    return ZX_ERR_NO_MEMORY;
  }

  next_free_desc_++;

  auto& desc =
      const_cast<volatile vring_desc&>(queue_->ring()->desc[desc_index]);
  desc.addr = queue_->device()->shared_mem()->VirtToPhys(buf, len);
  desc.len = static_cast<uint32_t>(len);
  desc.flags = flags;

  if (desc_out != nullptr) {
    *desc_out = desc_index;
  }
  return ZX_OK;
}

void VirtioQueueFake::WriteToAvail(uint16_t desc) {
  auto avail = const_cast<volatile vring_avail*>(queue_->ring()->avail);
  uint16_t& avail_idx = const_cast<uint16_t&>(queue_->ring()->avail->idx);
  avail->ring[avail_idx++ % queue_size_] = desc;
}

volatile vring_used_elem* VirtioQueueFake::ReadFromUsed() {
  auto used = const_cast<volatile vring_used*>(queue_->ring()->used);
  uint16_t& used_idx = const_cast<uint16_t&>(queue_->ring()->used->idx);

  if (last_used_idx_ == used_idx)
    return nullptr;

  return &used->ring[last_used_idx_++ % queue_size_];
}

zx_status_t DescBuilder::Build(uint16_t* desc) {
  if (status_ == ZX_OK) {
    queue_->WriteToAvail(head_desc_);
    if (desc != nullptr) {
      *desc = head_desc_;
    }
    head_desc_ = 0;
    prev_desc_ = 0;
    len_ = 0;
    // Signal so that queue event signals will be set.
    status_ = queue_->queue()->Signal();
  }
  return status_;
}

DescBuilder& DescBuilder::Append(void* buf, size_t buf_len, bool write) {
  // If a previous Append operation failed just no-op.
  if (status_ != ZX_OK) {
    return *this;
  }

  uint16_t flags = write ? VRING_DESC_F_WRITE : 0;
  uint16_t desc;
  status_ = queue_->WriteDescriptor(buf, buf_len, flags, &desc);
  if (status_ == ZX_OK) {
    if (len_++ == 0) {
      head_desc_ = desc;
    } else {
      status_ = queue_->SetNext(prev_desc_, desc);
    }

    prev_desc_ = desc;
  }

  return *this;
}

}  // namespace trusty
