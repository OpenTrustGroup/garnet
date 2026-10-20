// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mem_controller_client.h"

#include "lib/app/cpp/environment_services.h"

namespace machina {

static constexpr uint64_t kBlockSize = 2 * 1024 * 1024;

MemControllerClient::MemControllerClient(Guest* guest) : guest_(guest){
  auto status = loop_.StartThread();
  FXL_CHECK(status == ZX_OK);

  component::ConnectToEnvironmentService<machina::MemController>(svc_.NewRequest());
  svc_->RegisterMemControllerListener(
      event_listener_binding_.NewBinding(loop_.async()));
}

zx_status_t MemControllerClient::UnplugMemory(uint64_t gpa_base,
                                              uint64_t size) {
  if (size == 0) {
    return ZX_OK;
  }

  if (gpa_range_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  return guest_->UnmapPhysicalMemory(gpa_range_, gpa_base, size);
}

void MemControllerClient::OnSetGpaRegion(
    uint64_t gpa_base,
    uint64_t size,
    OnSetGpaRegionCallback callback) {
  if (gpa_range_ != nullptr) {
    if (gpa_range_base_ == gpa_base && gpa_range_size_ == size) {
      callback(ZX_OK);
      return;
    }

    FXL_LOG(ERROR) << "GPA region is already reserved: base=0x" << std::hex
                   << gpa_range_base_ << ", size=0x" << gpa_range_size_;
    callback(ZX_ERR_ALREADY_EXISTS);
    return;
  }

  Guest::GpaRange* range = nullptr;
  zx_status_t status = guest_->ReserveGpaRange(gpa_base, size, &range);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to reserve UOS GPA region: base=0x" << std::hex
                   << gpa_base << ", size=0x" << size
                   << ", status=" << status;
  } else {
    gpa_range_ = range;
    gpa_range_base_ = gpa_base;
    gpa_range_size_ = size;
  }
  callback(status);
}

void MemControllerClient::OnPlug(uint64_t gpa_base,
                                 fidl::VectorPtr<uint64_t> phys_addrs,
                                 OnPlugCallback callback) {
  if (!phys_addrs || phys_addrs->empty() || gpa_base % kBlockSize != 0) {
    FXL_LOG(ERROR) << "Invalid plug request, gpa_base=0x" << std::hex
                   << gpa_base;
    callback(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (gpa_range_ == nullptr) {
    FXL_LOG(ERROR) << "GPA range is not reserved";
    callback(ZX_ERR_BAD_STATE);
    return;
  }

  for (uint64_t block_pa : *phys_addrs) {
    if (block_pa % kBlockSize != 0) {
      FXL_LOG(ERROR) << "Physical block is not aligned: 0x" << std::hex
                     << block_pa;
      callback(ZX_ERR_INVALID_ARGS);
      return;
    }
  }

  uint64_t offset = 0;

  for (uint64_t block_pa : *phys_addrs) {
    zx_status_t res = guest_->MapPhysicalMemory(
        gpa_range_, gpa_base + offset, block_pa, kBlockSize,
        /*writeable=*/true);
    if (res != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map physical block at GPA 0x" << std::hex
                     << (gpa_base + offset) << ", PA 0x" << block_pa
                     << ", status=" << res;
      if (offset != 0) {
        // Unmap the successfully mapped prefix before host releases its pages.
        zx_status_t rollback_status = UnplugMemory(gpa_base, offset);
        if (rollback_status != ZX_OK) {
          FXL_LOG(ERROR)
              << "Failed to rollback plugged memory; mapped prefix may remain: "
                 "gpa_base=0x"
              << std::hex << gpa_base << ", size=0x" << offset
              << ", blocks=" << (offset / kBlockSize)
              << ", status=" << rollback_status;
        }
      }
      callback(ZX_ERR_IO);
      return;
    }

    offset += kBlockSize;
  }

  callback(ZX_OK);
}

void MemControllerClient::OnUnPlug(uint64_t gpa_base,
                                   uint64_t size,
                                   OnUnPlugCallback callback) {
  zx_status_t res = UnplugMemory(gpa_base, size);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to unmap memory block at GPA " << std::hex
                   << gpa_base;
    callback(ZX_ERR_IO);
    return;
  }
  callback(ZX_OK);
}

}  // namespace machina
