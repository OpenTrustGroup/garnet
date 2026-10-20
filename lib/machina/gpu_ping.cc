// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trusty_std.h>
#include <uapi/err.h>
#include "ipc_channel.h"

#include "garnet/lib/machina/fdt_utils.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/gpu_ping.h"
#include "garnet/lib/machina/guest_config.h"

namespace machina {

constexpr size_t kGpuPingFifoSize = 4096;
constexpr uint64_t kGpuPingFifoBase = 0x88000a000UL;

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_EXECUTE |
        ZX_VM_FLAG_SPECIFIC;

GpuPingServiceImpl::GpuPingServiceImpl(
    component::ServiceProviderBridge* bridge,
    Guest* guest) : guest_(guest) {
  bridge->AddService<GpuPingService>(
      [this](fidl::InterfaceRequest<GpuPingService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  zx_paddr_t addr;
  fifo_size_ = kGpuPingFifoSize;
  fifo_gpaddr_ = kGpuPingFifoBase;
  zx_status_t status = zx::vmo::create(kGpuPingFifoSize, 0, &fifo_vmo_);
  FXL_CHECK(status == ZX_OK);
  status = zx::unowned_vmar::wrap(guest->vmar())
          .map(fifo_gpaddr_, fifo_vmo_, 0, fifo_size_, kMapFlags, &addr);
  FXL_CHECK(status == ZX_OK);
}

void GpuPingServiceImpl::GetPingIrq(uint16_t vmid, GetPingIrqCallback callback) {
  callback(hwirq_);
}

void GpuPingServiceImpl::GetFifoBufVmo(uint16_t vmid, GetFifoBufVmoCallback callback) {
  zx::vmo vmo;
  auto status = fifo_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FXL_CHECK(status == ZX_OK);
  callback(std::move(vmo));
}

GpuPingClient::GpuPingClient(Guest* guest)
    : guest_(guest) {}

void GpuPingClient::MapVmo() {
  zx::vmo vmo;
  svc_->GetFifoBufVmo(guest_->vmid(), &vmo);
  fifo_vmo_ = std::move(vmo);
  fifo_gpaddr_ = kGpuPingFifoBase;
  fifo_size_ = kGpuPingFifoSize;
  zx_paddr_t addr;
  zx_status_t status =
      zx::unowned_vmar::wrap(guest_->vmar())
          .map(fifo_gpaddr_, fifo_vmo_, 0, fifo_size_, kMapFlags, &addr);
  FXL_CHECK(status == ZX_OK);
}

void GpuPingClient::GetPingIrq() {
  uint32_t hwirq;
  svc_->GetPingIrq(guest_->vmid(), &hwirq);
  hwirq_ = hwirq;
  FXL_LOG(INFO) << "uos get gpu-ping irq: " << hwirq_;
}

zx_status_t GpuPingClient::CreateGpuPingTreeNodes(uintptr_t guest_phys_base) {

  uintptr_t dtb_offset = guest_->dtb_spec().base - guest_phys_base;
  size_t dtb_size = guest_->dtb_spec().size;

  // Validate device tree.
  void* dtb = guest_->phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int offs = fdt_path_offset(dtb, "/nbl-gpu-ping");
  if (offs < 0) {

    FXL_LOG(INFO) << "can not find /nbl-gpu-ping, now create a new node";

    offs = fdt_add_subnode(dtb, 0, "nbl-gpu-ping");
    check_status(offs, "nbl-gpu-ping");
    
    ret = fdt_setprop_string(dtb, offs, "compatible", "nbl,gpu_ping");
    check_status(ret, "compatible");
    ret = fdt_setprop_cells_u64(dtb, offs, "reg", 2, fifo_gpaddr_,
                                fifo_size_);
    check_status(ret, "reg");

    ret = fdt_setprop_u32(dtb, offs, "ping-irq", hwirq_);
    check_status(ret, "ping-irq");

    fdt_pack(dtb);
  }
  
  return ZX_OK;
}

}  // namespace machina
