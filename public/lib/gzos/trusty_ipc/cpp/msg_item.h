// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>

namespace trusty_ipc {

class MessageItem
    : public fbl::DoublyLinkedListable<fbl::unique_ptr<MessageItem>> {
 public:
  MessageItem() = delete;
  MessageItem(uint32_t msg_id) : msg_id_(msg_id) {}

  ~MessageItem() { Reset(); }

  zx_status_t InitNew(size_t size) {
    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(size, 0, &vmo);
    if (status != ZX_OK) {
      return status;
    }

    status = vmo.op_range(ZX_VMO_OP_COMMIT, 0u, size, nullptr, 0u);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::vmo::op_range failed, status " << status;
      return status;
    }

    return InitInternal(fbl::move(vmo));
  }

  zx_status_t InitFromVmo(zx::vmo vmo) { return InitInternal(fbl::move(vmo)); }

  bool initialized() const { return buffer_ptr_ != nullptr; }

  void* PtrFromOffset(uint64_t offset) const {
    FXL_DCHECK(buffer_ptr_ != nullptr);
    FXL_DCHECK(offset < size_);
    return buffer_ptr_ + offset;
  }

  void Reset() {
    if (buffer_ptr_ != nullptr) {
      FXL_DCHECK(size_ != 0);
      zx_status_t status = zx::vmar::root_self().unmap(
          reinterpret_cast<uintptr_t>(buffer_ptr_), size_);
      FXL_CHECK(status == ZX_OK);
      buffer_ptr_ = nullptr;
    }

    size_ = 0;
    filled_size_ = 0;
    vmo_.reset();
  }

  zx::vmo GetDuplicateVmo() const {
    zx::vmo vmo;
    zx_status_t status =
        vmo_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_MAP, &vmo);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::handle::duplicate failed, status " << status;
    }
    return vmo;
  }

  uint64_t size() const { return size_; }
  uint64_t filled_size() const { return filled_size_; }
  void update_filled_size(uint64_t size) { filled_size_ = size; }
  uint32_t msg_id() const { return msg_id_; }

 private:
  zx_status_t InitInternal(zx::vmo vmo) {
    Reset();

    uint64_t size;
    zx_status_t status = vmo.get_size(&size);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::vmo::get_size failed, status " << status;
      return status;
    }
    size_ = size;
    filled_size_ = 0;

    uintptr_t mapped_buffer = 0u;
    status = zx::vmar::root_self().map(
        0, vmo, 0u, size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
        &mapped_buffer);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::vmar::map failed, status " << status;
      return status;
    }

    buffer_ptr_ = reinterpret_cast<uint8_t*>(mapped_buffer);

    vmo_ = fbl::move(vmo);

    return ZX_OK;
  }

  uint8_t* buffer_ptr_ = nullptr;
  uint32_t msg_id_;
  zx::vmo vmo_;
  size_t size_;
  size_t filled_size_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageItem);
};

}  // namespace trusty_ipc
