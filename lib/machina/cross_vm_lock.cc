// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2026 GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/cross_vm_lock.h"

namespace machina {

constexpr size_t kCrossVMLockMemSize = 4096;
constexpr uint64_t kCrossVMLockMemBase = 0x8A000A000UL;

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE |
    ZX_VM_FLAG_PERM_EXECUTE | ZX_VM_FLAG_SPECIFIC;

CrossVMLockGMServiceImpl::CrossVMLockGMServiceImpl(
    component::ApplicationContext* application_context) {
  application_context->outgoing_services()->AddService<CrossVMLockGMService>(
      [this](fidl::InterfaceRequest<CrossVMLockGMService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  zx_status_t status = zx::vmo::create(kCrossVMLockMemSize, 0, &mem_vmo_);
  FXL_CHECK(status == ZX_OK);
}

void CrossVMLockGMServiceImpl::GetVmo(GetVmoCallback callback) {
  zx::vmo vmo;
  zx_status_t status = mem_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FXL_CHECK(status == ZX_OK);
  callback(std::move(vmo));
}

CrossVMLockSOSServiceImpl::CrossVMLockSOSServiceImpl(
    component::ServiceProviderBridge* bridge, zx::vmo vmo)
    : mem_vmo_(std::move(vmo)) {
  bridge->AddService<CrossVMLockSOSService>(
      [this](fidl::InterfaceRequest<CrossVMLockSOSService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void CrossVMLockSOSServiceImpl::GetVmo(GetVmoCallback callback) {
  zx::vmo vmo;
  zx_status_t status = mem_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FXL_CHECK(status == ZX_OK);
  callback(std::move(vmo));
}

CrossVMLockGMClient::CrossVMLockGMClient(Guest* guest)
    : guest_(guest) {}

void CrossVMLockGMClient::MapVmo() {
  zx::vmo vmo;
  svc_->GetVmo(&vmo);
  mem_vmo_ = std::move(vmo);
  mem_size_ = kCrossVMLockMemSize;
  mem_gpaddr_ = kCrossVMLockMemBase;
  zx_paddr_t addr;
  zx_status_t status =
      zx::unowned_vmar::wrap(guest_->vmar())
          .map(mem_gpaddr_, mem_vmo_, 0, mem_size_, kMapFlags, &addr);
  FXL_CHECK(status == ZX_OK);
}

CrossVMLockSOSClient::CrossVMLockSOSClient(Guest* guest)
    : guest_(guest) {}

void CrossVMLockSOSClient::MapVmo() {
  zx::vmo vmo;
  svc_->GetVmo(&vmo);
  mem_vmo_ = std::move(vmo);
  mem_size_ = kCrossVMLockMemSize;
  mem_gpaddr_ = kCrossVMLockMemBase;
  zx_paddr_t addr;
  zx_status_t status =
      zx::unowned_vmar::wrap(guest_->vmar())
          .map(mem_gpaddr_, mem_vmo_, 0, mem_size_, kMapFlags, &addr);
  FXL_CHECK(status == ZX_OK);
}

}  // namespace machina
