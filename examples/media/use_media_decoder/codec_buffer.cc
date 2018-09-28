// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_buffer.h"

#include "util.h"

#include <lib/fxl/logging.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include <stdio.h>

CodecBuffer::CodecBuffer(uint32_t buffer_index, size_t size_bytes)
    : buffer_index_(buffer_index), size_bytes_(size_bytes) {}

uint32_t CodecBuffer::buffer_index() { return buffer_index_; }

void CodecBuffer::SetPhysicallyContiguousRequired(
    const ::zx::handle& very_temp_kludge_bti_handle) {
  is_physically_contiguous_required_ = true;
  zx_status_t status =
      ::zx::unowned_bti(very_temp_kludge_bti_handle.get())
          ->duplicate(ZX_RIGHT_SAME_RIGHTS, &very_temp_kludge_bti_handle_);
  FXL_CHECK(status == ZX_OK);
}

bool CodecBuffer::Init() {
  zx::vmo local_vmo;
  zx_status_t res;

  // Create the VMO.
  if (is_physically_contiguous_required_) {
    res = zx_vmo_create_contiguous(very_temp_kludge_bti_handle_.get(),
                                   size_bytes_, 0,
                                   local_vmo.reset_and_get_address());
    if (res != ZX_OK) {
      printf(
          "Failed to create _physically contiguous_ %zu byte buffer vmo (res "
          "%d)\n",
          size_bytes_, res);
      return false;
    }
  } else {
    res = zx::vmo::create(size_bytes_, 0, &local_vmo);
    if (res != ZX_OK) {
      printf("Failed to create %zu byte buffer vmo (res %d)\n", size_bytes_,
             res);
      return false;
    }
  }

  // Map the VMO in the local address space.
  uintptr_t tmp;
  res = zx::vmar::root_self()->map(0, local_vmo, 0, size_bytes_,
                                   ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &tmp);
  if (res != ZX_OK) {
    printf("Failed to map %zu byte buffer vmo (res %d)\n", size_bytes_, res);
    return false;
  }
  base_ = reinterpret_cast<uint8_t*>(tmp);

  // If we don't make it to here, then local_vmo takes care of freeing the
  // zx::vmo as needed.
  vmo_ = std::move(local_vmo);
  return true;
}

CodecBuffer::~CodecBuffer() {
  if (base_) {
    zx_status_t res = zx::vmar::root_self()->unmap(
        reinterpret_cast<uintptr_t>(base_), size_bytes_);
    if (res != ZX_OK) {
      Exit("Failed to unmap %zu byte buffer vmo (res %d) - exiting\n",
           size_bytes_, res);
    }
    base_ = nullptr;
  }
}

bool CodecBuffer::GetDupVmo(bool is_for_write, zx::vmo* out_vmo) {
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP;
  if (is_for_write) {
    rights |= ZX_RIGHT_WRITE;
  }
  zx_status_t res = vmo_.duplicate(rights, out_vmo);
  if (res != ZX_OK) {
    printf("Failed to duplicate buffer vmo handle (res %d)\n", res);
    return false;
  }
  return true;
}

// A real client would want to enforce a max allocation size before size_bytes
// gets here.
std::unique_ptr<CodecBuffer> CodecBuffer::Allocate(
    uint32_t buffer_index,
    const fuchsia::mediacodec::CodecBufferConstraints& constraints) {
  std::unique_ptr<CodecBuffer> result(new CodecBuffer(
      buffer_index, constraints.per_packet_buffer_bytes_recommended));
  if (constraints.is_physically_contiguous_required) {
    result->SetPhysicallyContiguousRequired(
        constraints.very_temp_kludge_bti_handle);
  }
  if (!result->Init()) {
    return nullptr;
  }
  return result;
}
