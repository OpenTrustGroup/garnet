// Copyright 2018 OpenTrustGroup. All rights reserved.
// Copyright 2015 Google, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <zircon/assert.h>

#include <string>

#include "garnet/lib/gzos/trusty_virtio/shared_mem.h"
#include "garnet/lib/gzos/trusty_virtio/virtio_device.h"

namespace trusty_virtio {

zx_status_t VirtioBus::AddDevice(fbl::RefPtr<VirtioDevice> vdev) {
  fbl::AutoLock mutex(&mutex_);

  if (state_ != State::UNINITIALIZED) {
    return ZX_ERR_BAD_STATE;
  }
  vdev->shared_mem_ = shared_mem_;

  fbl::AllocChecker ac;
  vdevs_.push_back(vdev, &ac);
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  return ZX_OK;
}

zx_status_t VirtioBus::GetResourceTable(void* buf, size_t* buf_size) {
  fbl::AutoLock mutex(&mutex_);
  FinalizeVdevRegisteryLocked();

  if (state_ != State::IDLE) {
    return ZX_ERR_BAD_STATE;
  }

  if (*buf_size < rsc_table_size_) {
    return ZX_ERR_NO_MEMORY;
  }

  if (!shared_mem_->validate_vaddr_range(buf, *buf_size)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto table = reinterpret_cast<resource_table*>(buf);
  table->ver = kVirtioResourceTableVersion;
  table->num = vdevs_.size();

  uint32_t idx = 0;
  for (const auto& vdev : vdevs_) {
    size_t size = vdev->ResourceEntrySize();
    uint32_t offset = vdev->rsc_entry_offset_;

    ZX_DEBUG_ASSERT(offset <= rsc_table_size_);
    ZX_DEBUG_ASSERT(offset + size <= rsc_table_size_);

    table->offset[idx] = offset;
    vdev->GetResourceEntry(rsc_entry<void>(table, idx++));
  }

  *buf_size = rsc_table_size_;
  return ZX_OK;
}

zx_status_t VirtioBus::Start(void* buf, size_t buf_size) {
  fbl::AutoLock mutex(&mutex_);

  if (state_ != State::IDLE) {
    return ZX_ERR_BAD_STATE;
  }

  if (buf_size != rsc_table_size_) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::unique_ptr<uint8_t[]> tmp_buf(new uint8_t[buf_size]);
  if (!tmp_buf) {
    return ZX_ERR_NO_MEMORY;
  }

  // copy resource table out of NS memory before parsing it
  memcpy(tmp_buf.get(), buf, buf_size);

  auto table = reinterpret_cast<resource_table*>(tmp_buf.get());

  uint32_t idx = 0;
  for (const auto& vdev : vdevs_) {
    zx_status_t status = vdev->Probe(rsc_entry<void>(table, idx++));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to probe virtio device: " << status;
      return status;
    }
  }

  state_ = State::ACTIVE;

  return ZX_OK;
}

zx_status_t VirtioBus::Stop(void* buf, size_t buf_size) {
  fbl::AutoLock mutex(&mutex_);

  if (state_ != State::ACTIVE) {
    return ZX_ERR_BAD_STATE;
  }

  for (const auto& vdev : vdevs_) {
    vdev->Reset();
  }

  state_ = State::IDLE;
  return ZX_OK;
}

zx_status_t VirtioBus::ResetDevice(uint32_t dev_id) {
  fbl::AutoLock mutex(&mutex_);

  if (state_ != State::ACTIVE) {
    return ZX_ERR_BAD_STATE;
  }

  for (const auto& vdev : vdevs_) {
    if (vdev->notify_id() == dev_id)
      return vdev->Reset();
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t VirtioBus::KickVqueue(uint32_t dev_id, uint32_t vq_id) {
  fbl::AutoLock mutex(&mutex_);

  if (state_ != State::ACTIVE) {
    return ZX_ERR_BAD_STATE;
  }

  for (const auto& vdev : vdevs_) {
    if (vdev->notify_id() == dev_id)
      return vdev->Kick(vq_id);
  }
  return ZX_ERR_NOT_FOUND;
}

void VirtioBus::FinalizeVdevRegisteryLocked(void) {
  if (state_ == State::UNINITIALIZED) {
    uint32_t offset = rsc_table_hdr_size(vdevs_.size());

    // go through the list of vdev and calculate total resource table size
    // and resource entry offset withing resource table buffer
    for (const auto& vdev : vdevs_) {
      vdev->rsc_entry_offset_ = offset;
      offset += vdev->ResourceEntrySize();
    }

    rsc_table_size_ = offset;
    state_ = State::IDLE;
  }
}

uint32_t VirtioDevice::next_notify_id_ = 0;

}  // namespace trusty_virtio
