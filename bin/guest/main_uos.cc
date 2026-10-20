// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ios>
#include <string>
#include <vector>

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/task.h>
#include <pal/platform_defs.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/threads.h>
#include <zircon/vmid.h>

#include "garnet/bin/guest/guest_config.h"
#include "garnet/bin/guest/guest_features.h"
#include "garnet/bin/guest/linux.h"
#include "garnet/bin/guest/message_handler.h"
#include "garnet/bin/guest/zircon.h"
#include "garnet/lib/block_locator/block_locator.h"
#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/fdt_utils.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/inspect_service_impl.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/ipc_client.h"
#include "garnet/lib/machina/pci.h"
#include "garnet/lib/machina/uart.h"
#include "garnet/lib/machina/utrace.h"
#include "garnet/lib/machina/vcpu.h"
#include "garnet/lib/machina/vhm_device.h"
#include "garnet/lib/machina/cross_vm_lock.h"
#include "garnet/lib/machina/mem_controller_client.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "garnet/lib/machina/virtio_block.h"
#include "lib/fxl/files/file.h"

#include "garnet/lib/machina/mmio_bus.h"
#include "garnet/bin/guest/proto/vm_config.pb.h"
#include "garnet/lib/machina/arch/arm64/pl031.h"
#include "garnet/lib/machina/arch/arm64/smmu_v3.h"
#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/ipc_message.h"
#include "garnet/lib/machina/monitor_virtio.h"
#include "garnet/lib/machina/reg_access.h"
#include "garnet/lib/machina/remoteproc.h"
#include "garnet/lib/machina/remoteproc_vm_monitor.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "garnet/lib/machina/gpu_ping.h"
#include "tipc_vqueue_notifier.h"
#include "vmlog_srv.h"
#include "garnet/lib/machina/gpu_scheduler_vcpu_selection.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include <nbl_fwk/nmt_tee.h>
#include <trusty_std.h>

#define LK2IMG_SIZE_OFFSET 20

//0:UOS image integrity will be verified even without license.
//1:UOS image verification will only be performed when the license has been activated
#define SECURE_BOOT_WITH_LICENSE 0

#include "garnet/lib/machina/arch/arm64/gic_its.h"
#include "garnet/lib/machina/virtio_spi.h"
#include "garnet/lib/machina/virtio_i2c.h"
#include "garnet/lib/machina/virtio_eint.h"
#include "garnet/lib/machina/virtio_rtc.h"
#include "garnet/lib/machina/virtio_rpmb.h"
#include "garnet/lib/machina/pv_block.h"
#include "garnet/lib/machina/vm_id.h"

#include "garnet/lib/machina/gpu_scheduler.h"

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
static constexpr int32_t kMmioIrqVector = 180;
static constexpr char kGuestMemoryAllocator[] =
    "/dev/sys/platform/00:00:f/guest_memory_allocator";

static constexpr uint32_t kESpiBase = 4096;

namespace {
static bool should_use_pvblk_for_uos_guest(
    const GuestFeatures& features, const nbl_vmm::VmConfig& cfg, int32_t vmid) {
  return features.pvblk_enabled() && cfg.pvblk_enabled() &&
         (machina::IsAlpsVmid(vmid) || machina::IsTboxVmid(vmid));
}

static bool pvblk_backend_visible_to_uos_guest(int32_t vmid, uint32_t index) {
  if (machina::IsAlpsVmid(vmid) || machina::IsTboxVmid(vmid)) {
    return index == 2;
  }
  return false;
}

static uint32_t pvblk_uos_guest_disk_index(int32_t vmid,
                                            uint32_t backend_index) {
  if (machina::IsAlpsVmid(vmid) || machina::IsTboxVmid(vmid)) {
    return 0;
  }
  return backend_index;
}

static bool string_list_contains(const std::string& list, const char* value) {
  size_t pos = 0;
  const size_t value_len = strlen(value);

  while (pos <= list.size()) {
    size_t comma = list.find(',', pos);
    size_t end = comma == std::string::npos ? list.size() : comma;
    if (end - pos == value_len && list.compare(pos, value_len, value) == 0) {
      return true;
    }
    if (comma == std::string::npos) {
      break;
    }
    pos = comma + 1;
  }

  return false;
}

static std::string add_android_boot_device(std::string bootargs,
                                           const char* boot_device) {
  static constexpr char kBootDevicesKey[] = "androidboot.boot_devices=";
  const size_t key_len = strlen(kBootDevicesKey);
  size_t pos = bootargs.find(kBootDevicesKey);

  if (pos == std::string::npos) {
    if (!bootargs.empty() && bootargs.back() != ' ') {
      bootargs.push_back(' ');
    }
    bootargs.append(kBootDevicesKey);
    bootargs.append(boot_device);
    return bootargs;
  }

  size_t value_begin = pos + key_len;
  size_t value_end = bootargs.find(' ', value_begin);
  if (value_end == std::string::npos) {
    value_end = bootargs.size();
  }

  std::string devices = bootargs.substr(value_begin, value_end - value_begin);
  if (string_list_contains(devices, boot_device)) {
    return bootargs;
  }

  bootargs.insert(value_end, std::string(",") + boot_device);
  return bootargs;
}

static std::vector<machina::VmemSpec> vmems_build_from(nbl_vmm::VmConfig& cfg) {
  std::vector<machina::VmemSpec> vmems;
  for (int i = 0; i < cfg.mem_size(); i++) {
    auto& v = cfg.mem(i);
    machina::VmemSpec vmem = {
        .name = v.name(),
        .gpa_base = v.gpa_base(),
        .hpa_base = v.hpa_base(),
        .size = v.size(),
        .policy = static_cast<machina::MemoryPolicy>(v.policy()),
        .is_physmem = v.is_physmem(),
        .is_reservedmem = v.is_reservedmem(),
        .permission = v.permission(),
    };
    vmems.push_back(vmem);
  }

  return vmems;
}

static std::vector<machina::DefaultRegSpec> default_reg_build_from(nbl_vmm::VmConfig& cfg) {
  std::vector<machina::DefaultRegSpec> reg_infos;
  for (int i = 0; i < cfg.reg_info_size(); i++) {
    auto& v = cfg.reg_info(i);
    machina::DefaultRegSpec reg_info = {
        .regbase = v.regbase(),
        .vmstatus = v.vmstatus(),
        .value_num = v.value_num(),
        .value_reg = v.value_reg(),
    };
    reg_infos.push_back(reg_info);
  }

  return reg_infos;
}

static uint64_t get_phys_base(nbl_vmm::VmConfig& cfg) {
  for (int i = 0; i < cfg.mem_size(); i++) {
    auto& v = cfg.mem(i);
    if (v.is_physmem()) {
      return v.gpa_base();
    }
  }
  FXL_CHECK(false);
  __builtin_unreachable();
}

static machina::VgicSpec vgic_build_from(nbl_vmm::VmConfig& cfg) {
  auto& v = cfg.vgic();

  machina::VgicSpec vgic = {
      .gicd_paddr = v.gicd_paddr(),
      .gicr_paddr = v.gicr_paddr(),
      .has_its = v.has_its(),
      .its_paddr = v.its_paddr(),
  };
  for (int i = 0; i < v.interrupts_size(); i++) {
    vgic.irqs.push_back(v.interrupts(i));
  }
  for (int i = 0; i < v.percpu_interrupts_size(); i++) {
    vgic.percpu_irqs.push_back(v.percpu_interrupts(i));
  }

  return vgic;
}

static std::vector<machina::VsmmuSpec> vsmmu_build_from(
    nbl_vmm::VmConfig& cfg) {
  std::vector<machina::VsmmuSpec> vsmmus;

  for (int i = 0; i < cfg.vsmmu_size(); i++) {
    auto& v = cfg.vsmmu(i);
    machina::VsmmuSpec vsmmu;
    vsmmu.paddr = v.paddr();
    vsmmu.irq = v.irq();

    for (int i = 0; i < v.sids_size(); i++) {
      vsmmu.sids.push_back(v.sids(i));
    }
    vsmmus.push_back(vsmmu);
  }

  return vsmmus;
}

static std::vector<machina::RegAccessSpec> convert_proto_to_specs(nbl_vmm::VmConfig& cfg) {
  uint32_t product_id;
  uint64_t range_min = 0;
  uint64_t range_max = 0;

  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_PRODUCT_ID, &product_id);

  // validation check, prevent VM to access any memory region
  if (product_id == BOARD_PID_MTK_MT8668_EVB) {
    range_min = 0x0;
    range_max = 0x40000000;
  } else {
    range_min = 0x0;
    range_max = 0x0;
  }

  std::vector<machina::RegAccessSpec> specs;

  for (int i = 0; i < cfg.reg_access_size(); i++) {
    const auto& proto_spec = cfg.reg_access(i);
    machina::RegAccessSpec local_spec;
    local_spec.start_addr = proto_spec.start_addr();
    local_spec.end_addr = proto_spec.end_addr();
    if (local_spec.start_addr >= range_min && local_spec.end_addr <= range_max) {
      specs.push_back(local_spec);
    }
  }
  return specs;
}

}  // namespace

zx_status_t update_cmdline(nbl_vmm::VmConfig& cfg, int32_t vmid, void* dtb,
                           size_t dtb_size, const GuestFeatures& features) {
  std::string cmdline = cfg.cmdline();
  if (cmdline.empty())
    return ZX_OK;

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int offset = fdt_path_offset(dtb, "/chosen");
  if (offset < 0) {
    FXL_LOG(WARNING) << "//chosen path not found, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }

  int len = 0;
  const void* bootargs = fdt_getprop(dtb, offset, "bootargs", &len);
  if (!bootargs || (len < 0)) {
    FXL_LOG(WARNING) << "//chosen path not found, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }

  cmdline.append(static_cast<const char*>(bootargs));
  if (machina::IsAlpsVmid(vmid) &&
      should_use_pvblk_for_uos_guest(features, cfg, vmid)) {
    cmdline = add_android_boot_device(cmdline, "a10002000.pvblk");
  }
  ret = fdt_setprop(dtb, offset, "bootargs", cmdline.data(),
                    cmdline.length() + 1);
  if (ret < 0) {
    FXL_LOG(INFO) << "cannot modify bootargs";
    fdt_pack(dtb);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  fdt_pack(dtb);
  return ZX_OK;
}

static bool is_qemu_platform() {
  static uint32_t product_id;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_PRODUCT_ID, &product_id);
  return product_id == BOARD_VID_QEMU;
}

static void update_virtio_gpu_irq(nbl_vmm::VmConfig& cfg,
                                  void* dtb,
                                  size_t dtb_size) {
  if (cfg.gpu_irqs_size() <= 0) {
    return;
  }

  std::vector<uint32_t> props;
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
  props.push_back(cpu_to_fdt32(cfg.gpu_irqs(1) - 32));
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
  if (!is_qemu_platform())
    props.push_back(0);
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
  props.push_back(cpu_to_fdt32(cfg.gpu_irqs(0) - 32));
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
  if (!is_qemu_platform())
    props.push_back(0);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int irq_offset = fdt_add_subnode(dtb, offset, "virtio_gpu_irq");
  if (irq_offset < 0) {
    FXL_LOG(WARNING) << "failed to add virtio gpu irq node.";
    goto out;
  }

  ret = fdt_setprop_string(dtb, irq_offset, "compatible", "nbl,virtio_gpu_irq");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_irq compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_irq interrupts.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static void update_virtio_vsock_irq(nbl_vmm::VmConfig& cfg,
                                    void* dtb,
                                    size_t dtb_size) {
  uint32_t cnt = cfg.vsock_irqs_size();
  if (cnt == 0) {
    return;
  }

  std::vector<uint32_t> props;
  uint32_t idx = 0;
  while (idx < cnt) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(cfg.vsock_irqs(idx) - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
    if (!is_qemu_platform())
      props.push_back(0);
    idx++;
  }

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int irq_offset = fdt_add_subnode(dtb, offset, "virtio_vsock_irq");
  if (irq_offset < 0) {
    FXL_LOG(WARNING) << "failed to add virtio vsock irq node.";
    goto out;
  }

  ret =
      fdt_setprop_string(dtb, irq_offset, "compatible", "nbl,virtio_vsock_irq");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_vsock compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_irq interrupts.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static void update_virtio_apu_irq(nbl_vmm::VmConfig& cfg,
                                  void* dtb,
                                  size_t dtb_size) {
  uint32_t cnt = cfg.apu_irqs_size();
  if (cnt == 0) {
    return;
  }

  std::vector<uint32_t> props;
  uint32_t idx = 0;
  while (idx < cnt) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(cfg.apu_irqs(idx) - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
    if (!is_qemu_platform())
      props.push_back(0);
    idx++;
  }

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int irq_offset = fdt_add_subnode(dtb, offset, "virtio_apu_irq");
  if (irq_offset < 0) {
    FXL_LOG(WARNING) << "failed to add virtio apu irq node.";
    goto out;
  }

  ret = fdt_setprop_string(dtb, irq_offset, "compatible", "nbl,virtio_apu_irq");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_apu_irq compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_irq interrupts.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static void update_virtio_cmdq_irq(nbl_vmm::VmConfig& cfg,
                                   void* dtb,
                                   size_t dtb_size) {
  uint32_t cnt = cfg.cmdq_irqs_size();
  if (cnt == 0) {
    return;
  }

  std::vector<uint32_t> props;
  uint32_t idx = 0;
  while (idx < cnt) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(cfg.cmdq_irqs(idx) - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
    if (!is_qemu_platform())
      props.push_back(0);
    idx++;
  }

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int irq_offset = fdt_add_subnode(dtb, offset, "virtio_cmdq_irq");
  if (irq_offset < 0) {
    FXL_LOG(WARNING) << "failed to add virtio apu irq node.";
    goto out;
  }

  ret =
      fdt_setprop_string(dtb, irq_offset, "compatible", "nbl,virtio_cmdq_irq");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_cmdq_irq compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set virtio_gpu_irq interrupts.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static void update_smc_irq(nbl_vmm::VmConfig& cfg,
                           void* dtb, size_t dtb_size) {
  if (cfg.smc_irq() <= 32U) {
    FXL_LOG(WARNING) << "invalid smc irq";
    return;
  }

  std::vector<uint32_t> props;
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
  props.push_back(cpu_to_fdt32(cfg.smc_irq() - 32));
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
  if (!is_qemu_platform())
    props.push_back(0);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int irq_offset = fdt_add_subnode(dtb, offset, "nebula_smc");
  if (irq_offset < 0) {
    FXL_LOG(WARNING) << "failed to add nebula_smc node.";
    goto out;
  }

  ret =
      fdt_setprop_string(dtb, irq_offset, "compatible", "grt,nebula_smc");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set nebula_smc compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set nebula_smc interrupts.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static void update_cvm_lock_node(nbl_vmm::VmConfig& cfg,
                                 void* dtb, size_t dtb_size,
                                 uint64_t mem_gpaddr,
                                 size_t mem_size) {
  if (cfg.cvm_lock_irq() <= 32U) {
    FXL_LOG(WARNING) << "invalid cvm_lock irq";
    return;
  }

  std::vector<uint32_t> props;
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
  props.push_back(cpu_to_fdt32(cfg.cvm_lock_irq() - 32));
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
  if (!is_qemu_platform())
    props.push_back(0);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    fdt_pack(dtb);
    return;
  }

  int cvm_lock_offset = fdt_add_subnode(dtb, offset, "cross_vm_lock");
  if (cvm_lock_offset < 0) {
    FXL_LOG(WARNING) << "failed to add cross_vm_lock node.";
    goto out;
  }

  ret =
      fdt_setprop_string(dtb, cvm_lock_offset, "compatible", "grt,cross_vm_lock");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set cross_vm_lock compatible.";
    goto out;
  }

  ret = fdt_setprop(dtb, cvm_lock_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set cross_vm_lock interrupts.";
    goto out;
  }

  ret = fdt_setprop_cells_u64(dtb, cvm_lock_offset, "reg", 2,
                              mem_gpaddr, mem_size);
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set cross_vm_lock memory.";
    goto out;
  }

out:
  fdt_pack(dtb);
}

static int find_subnode_offset(const void* dtb, int start_offset,
                        const char* search_string) {
  int offset = start_offset;
  int depth = 0;
  const char* node_name = NULL;

  if (!dtb) {
    FXL_LOG(WARNING) << "Invalid args!";
    return -FDT_ERR_INTERNAL;
  }

  // Start from the root node
  for (depth = 0; (offset >= 0) && (depth >= 0);
       offset = fdt_next_node(dtb, offset, &depth)) {
    if (depth > 2) {
      continue;
    }

    node_name = fdt_get_name(dtb, offset, NULL);
    if (node_name == NULL) {
      continue;
    }

    // Check if node name contains the search string
    if (strstr(node_name, search_string)) {
      return offset;
    }
  }

  return -FDT_ERR_NOTFOUND;
}

static zx_status_t create_virtio_pci_nodes(nbl_vmm::VmConfig& cfg,
                           void* dtb, size_t dtb_size) {
  if (is_qemu_platform())
    return ZX_OK;
  int ret = 0;
  int32_t* pci_global_irqs = machina::PciBus::get_pci_global_irqs();
  if (pci_global_irqs == nullptr) {
    FXL_LOG(ERROR) << "Invalid pci_global_irqs pointer";
    return ZX_ERR_INTERNAL;
  }

  ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int root_offset = fdt_path_offset(dtb, "/");
  check_status(root_offset, "root");

  int pci_offset = fdt_add_subnode(dtb, root_offset, "pci@fde000000");
  bool node_exists = (pci_offset == -FDT_ERR_EXISTS);

  if (node_exists) {
    FXL_LOG(INFO) << "pci@fde000000 node already exists, get node offset";
    pci_offset = fdt_path_offset(dtb, "/pci@fde000000");
    if (pci_offset < 0) {
      FXL_LOG(ERROR) << "Failed to get pci@fde000000 node offset";
      return ZX_ERR_NOT_FOUND;
    }
  } else {
    check_status(pci_offset, "pci@fde000000");

    ret = fdt_setprop_string(dtb, pci_offset, "device_type", "pci");
    check_status(ret, "device_type");

    ret = fdt_setprop_string(dtb, pci_offset, "compatible",
                             "pci-host-ecam-generic");
    check_status(ret, "compatible");

    uint64_t base = 0xfde000000;
    uint64_t size = 0x1000000;
    ret = fdt_setprop_cells_u64(dtb, pci_offset, "reg", 2, base, size);
    check_status(ret, "reg");

    ret = fdt_setprop_cells_u32(dtb, pci_offset, "bus-range", 2, 1u, 1u);
    check_status(ret, "bus-range");

    ret = fdt_setprop_cells_u32(dtb, pci_offset, "ranges", 7, 0x2000000u, 0x0u,
                                0xb0001000u, 0xfu, 0xb0001000u, 0x0u, 0xfff000u);
    check_status(ret, "ranges");

    ret = fdt_setprop_cell(dtb, pci_offset, "#address-cells", 0x03);
    check_status(ret, "#address-cells");

    fdt_setprop_cell(dtb, pci_offset, "#size-cells", 0x02);
    check_status(ret, "#size-cells");

    fdt_setprop_cell(dtb, pci_offset, "#interrupt-cells", 0x01);
    check_status(ret, "#interrupt-cells");
  }

  int its_offset = find_subnode_offset(dtb, root_offset, "its");
  check_status(its_offset, "its");

  uint32_t its_phandle = 0xa0001;
  ret = fdt_getprop_cells_u32(dtb, its_offset, "phandle", 1, &its_phandle);
  if (ret < 0) {
    FXL_LOG(INFO) << "its phandle not found, add its phandle node";
    ret = fdt_setprop_cells_u32(dtb, its_offset, "phandle", 1, its_phandle);
    check_status(ret, "its/phandle");
  }

  if (!node_exists) {
    fdt_setprop_cell(dtb, pci_offset, "msi-parent", its_phandle);
    check_status(ret, "msi-parent");
  }

  uint32_t gic_phandle = 0xa0000;

  int gic_offset = fdt_node_offset_by_prop_value(dtb, root_offset, "compatible",
                                                 "arm,gic-v3", 11);
  check_status(gic_offset, "gic");

  ret = fdt_getprop_cells_u32(dtb, gic_offset, "phandle", 1, &gic_phandle);
  if (ret < 0) {
    FXL_LOG(INFO) << "gic phandle not found, add gic phandle node";
    ret = fdt_setprop_cells_u32(dtb, gic_offset, "phandle", 1, gic_phandle);
    check_status(ret, "gic/phandle");
  }

  ret = fdt_setprop_cells_u32(dtb, pci_offset, "interrupt-map-mask", 4, 0xf800u,
                              0x0u, 0x0u, 0x1u);
  check_status(ret, "interrupt-map-mask");

  uint32_t full_irq_map[11 * PCI_MAX_DEVICES] = {0};
  uint32_t* irq_map = full_irq_map;
  uint32_t map_index = 0;

  while (pci_global_irqs[map_index] != -1 && map_index < PCI_MAX_DEVICES) {
    // interrupt-map entry format:
    // <addr_hi addr_mid addr_low addr_space> <&gic 0 0 0 irq_type irq_flags>
    irq_map[map_index * 11 + 0] = cpu_to_fdt32(map_index << 11);
    irq_map[map_index * 11 + 1] = cpu_to_fdt32(0x0u);
    irq_map[map_index * 11 + 2] = cpu_to_fdt32(0x0u);
    irq_map[map_index * 11 + 3] = cpu_to_fdt32(0x1u);
    irq_map[map_index * 11 + 4] = cpu_to_fdt32(gic_phandle);
    irq_map[map_index * 11 + 5] = cpu_to_fdt32(0x0u);
    irq_map[map_index * 11 + 6] = cpu_to_fdt32(0x0u);
    irq_map[map_index * 11 + 7] = cpu_to_fdt32(0x0u);
    irq_map[map_index * 11 + 8] = cpu_to_fdt32(pci_global_irqs[map_index] - 32);
    irq_map[map_index * 11 + 9] = cpu_to_fdt32(0x4u);
    irq_map[map_index * 11 + 10] = cpu_to_fdt32(0x0u);
    map_index++;
  }

  ret = fdt_setprop(dtb, pci_offset, "interrupt-map", full_irq_map,
                    map_index * 11 * sizeof(uint32_t));
  check_status(ret, "interrupt-map");

  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t create_pvblk_nodes_for_guest(
    const nbl_vmm::VmConfig& cfg, int32_t vmid, void* dtb, size_t dtb_size,
    const GuestFeatures& features) {
  if (is_qemu_platform() ||
      !should_use_pvblk_for_uos_guest(features, cfg, vmid)) {
    return ZX_OK;
  }

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree for pvblk nodes";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int root_offset = fdt_path_offset(dtb, "/");
  check_status(root_offset, "root");

  uint32_t cells_size = 3;
  machina::Guest::GetGICInterruptCellSize(dtb, cells_size);

  for (uint32_t index = 0; index < machina::kPvBlockDeviceCount; ++index) {
    if (!pvblk_backend_visible_to_uos_guest(vmid, index)) {
      continue;
    }
    uint64_t mmio_base =
        machina::kPvBlockMmioBase +
        index * static_cast<uint64_t>(PVBLK_MMIO_SIZE);
    uint32_t irq = machina::kPvBlockIrqBase + index;

    char node_name[64];
    snprintf(node_name, sizeof(node_name), "pvblk@%lx", mmio_base);
    int node_offset = fdt_add_subnode(dtb, root_offset, node_name);
    if (node_offset == -FDT_ERR_EXISTS) {
      std::string node_path = std::string("/") + node_name;
      node_offset = fdt_path_offset(dtb, node_path.c_str());
    }
    check_status(node_offset, node_name);

    ret = fdt_setprop_string(dtb, node_offset, "compatible", "grt,mtk-pvblk");
    check_status(ret, "compatible");
    ret = fdt_setprop_cells_u64(dtb, node_offset, "reg", 2, mmio_base,
                                static_cast<uint64_t>(PVBLK_MMIO_SIZE));
    check_status(ret, "reg");

    uint32_t props[] = {
        cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI),
        cpu_to_fdt32(irq - 32),
        cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI),
        cpu_to_fdt32(0),
    };
    ret = fdt_setprop(dtb, node_offset, "interrupts", props,
                      cells_size * sizeof(uint32_t));
    check_status(ret, "interrupts");

    ret = fdt_setprop_cell(dtb, node_offset, "queue-count",
                           machina::kPvBlockQueueCount);
    check_status(ret, "queue-count");
    ret = fdt_setprop_cell(dtb, node_offset, "queue-depth",
                           machina::kPvBlockQueueDepth);
    check_status(ret, "queue-depth");
    ret = fdt_setprop_cell(dtb, node_offset, "disk-index",
                           pvblk_uos_guest_disk_index(vmid, index));
    check_status(ret, "disk-index");
  }

  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t get_root_resource(zx::resource* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  resource->reset(rsc_handle);
  close(fd);
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

static int update_nodes(void* src_dtb,
                        void* dest_dtb,
                        std::vector<std::string>& nodes) {
  int src_root = fdt_path_offset(src_dtb, "/");
  if (src_root < 0) {
    FXL_LOG(INFO) << "Failed to get src_dtb root";
  }

  int dest_root = fdt_path_offset(dest_dtb, "/");
  if (dest_root < 0) {
    FXL_LOG(INFO) << "Failed to get dest_dtb root";
  }

  for (std::string& node : nodes) {
    int src_node = fdt_node_offset_by_compatible(src_dtb, 0, node.c_str());
    if (src_node < 0) {
      FXL_LOG(INFO) << "Failed to get src_dtb root";
    }

    int dest_node = fdt_node_offset_by_compatible(dest_dtb, 0, node.c_str());
    if (dest_node == -FDT_ERR_NOTFOUND) {
      int new_node = fdt_add_subnode(dest_dtb, dest_root,
                                     fdt_get_name(src_dtb, src_node, NULL));
      if (new_node < 0) {
        FXL_LOG(INFO) << "Failed to add node in dest DTB";
        return new_node;
      }

      int src_prop = fdt_first_property_offset(src_dtb, src_node);
      while (src_prop > 0) {
        const char* prop_name;
        int len = 0;

        const void* prop_value =
            fdt_getprop_by_offset(src_dtb, src_prop, &prop_name, &len);
        if (prop_name == NULL || len == 0) {
          FXL_LOG(INFO) << "Not found any prop in node";
        }

        if (strcmp(prop_name, "phandle") != 0) {
          int result =
              fdt_setprop(dest_dtb, new_node, prop_name, prop_value, len);
          if (result < 0) {
            FXL_LOG(INFO) << "Failed to set node properties";
            return result;
          }
        }

        src_prop = fdt_next_property_offset(src_dtb, src_prop);
      }
    } else if (dest_node >= 0) {
      int src_prop = fdt_first_property_offset(src_dtb, src_node);
      while (src_prop > 0) {
        const char* prop_name;
        int len = 0;

        const void* prop_value =
            fdt_getprop_by_offset(src_dtb, src_prop, &prop_name, &len);
        if (prop_name == NULL || len == 0) {
          FXL_LOG(INFO) << "Not found any prop in node";
        }

        if (strcmp(prop_name, "compatible") == 0) {
          break;
        }

        int result =
            fdt_setprop(dest_dtb, dest_node, prop_name, prop_value, len);
        if (result < 0) {
          FXL_LOG(INFO) << "Failed to set node properties";
          return result;
        }

        src_prop = fdt_next_property_offset(src_dtb, src_prop);
      }
    }
  }

  return 0;
}

static void copy_sos_dtb_node(machina::DeviceTreeSpec dtb_spec,
                              zx_handle_t handle,
                              void* dtb,
                              size_t dtb_size) {
  zx::vmo vmo;
  zx::resource root_resource;
  std::vector<std::string> append_nodes = {"mediatek,mddriver",
                                           "mediatek,md_attr_node"};

  zx_status_t status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource " << status;
    return;
  }

  FXL_LOG(ERROR) << "get sos dtb base=" << dtb_spec.base
                 << " size=" << dtb_spec.size;
  status = zx_vmo_create_physical(root_resource.get(), dtb_spec.base,
                                  dtb_spec.size, vmo.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create physical VMO " << status;
    return;
  }

  status = vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set cache policy on VMO " << status;
    return;
  }

  size_t size;
  zx_vmo_get_size(vmo.get(), &size);
  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_MAP_CONTIGUOUS;
  zx_paddr_t mapped_addr;
  status = zx_vmar_map(handle, map_flags, 0, vmo.get(), 0, size, &mapped_addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map trace physical memory " << status;
    return;
  }
  FXL_CHECK(vmo);

  void* sos_dtb = reinterpret_cast<void*>(mapped_addr);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    goto out;
  }

  ret = update_nodes(sos_dtb, dtb, append_nodes);
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to append device node";
    goto out;
  }

out:
  zx_vmar_unmap(handle, mapped_addr, dtb_spec.size);
  fdt_pack(dtb);
}

static void add_vtee_irq_to_vgic(nbl_vmm::VmConfig& cfg,
                                 machina::VgicSpec& vgic) {
  if (cfg.vtee_notifier_irq() == 0) {
    return;
  }

  auto& irq = vgic.irqs;
  for (auto i = 0; i < irq.size(); i++) {
    if (irq[i] == cfg.vtee_notifier_irq()) {
      return;
    }
  }

  irq.push_back(cfg.vtee_notifier_irq());
}

static zx_status_t create_notify_irq_node(nbl_vmm::VmConfig& cfg,
                                          void* dtb,
                                          size_t dtb_size,
                                          const std::string& node,
                                          uint32_t irq) {
  if (irq == 0) {
    return ZX_OK;
  }

  if (irq < 32) {
    FXL_LOG(ERROR) << "Set irq value too small, set irq > 32";
    return ZX_ERR_OUT_OF_RANGE;
  }

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int vtee_virtio_offset = fdt_path_offset(dtb, node.c_str());
  if (vtee_virtio_offset < 0) {
    FXL_LOG(WARNING) << "vtee dts not found, skipped: " << node;
    fdt_pack(dtb);
    return ZX_OK;
  }

  uint32_t props[] = {cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI),
                      cpu_to_fdt32(cfg.vtee_notifier_irq() - 32),
                      cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI),
                      cpu_to_fdt32(0)};
  ret = fdt_setprop(dtb, vtee_virtio_offset, "interrupts", props,
                    sizeof(props));
  check_status(ret, "interrupts");

  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t probe_vtee(nbl_vmm::VmConfig& cfg,
                              void* dtb,
                              size_t dtb_size) {
  int status = ZX_OK;
  uint32_t tee_vendor = (zx_system_get_tee_vendor_id() & ZX_TEE_VENDOR_MASK);
  std::string tee_node;
  switch (tee_vendor) {
    case ZX_TEE_VENDOR_NEBULA:
      tee_node = "/nebula-vtee/virtio";
      status = create_notify_irq_node(cfg, dtb, dtb_size, tee_node,
                                      cfg.vtee_notifier_irq());
      if (status != ZX_OK) {
        FXL_CHECK(status == ZX_OK)
            << "Failed to create nebula tee vqueue notify irq node " << status;
      } else {
        FXL_LOG(INFO) << "Nebula vTEE probe ok";
      }
      break;
    case ZX_TEE_VENDOR_TRUSTONIC:
      tee_node = "/mobicore";
      status = create_notify_irq_node(cfg, dtb, dtb_size, tee_node,
                                      cfg.vtee_notifier_irq());
      if (status != ZX_OK) {
        FXL_CHECK(status == ZX_OK)
            << "Failed to create trustonic irq node " << status;
      } else {
        FXL_LOG(INFO) << "Trustonic vTEE probe ok";
      }
      break;
    default:
      break;
  }
  return status;
}

static void add_arbiter_dts(void* dtb,
                            size_t dtb_size,
                            machina::GpuSchedulerClient* gpu_sched_raw) {
  uint64_t guest_ipa;
  uint32_t ipa_size;
  uint64_t guest_aee_ipa = 0;
  size_t guest_aee_size = 0;
  uint32_t gpu_irq[MAX_GPU_IRQ] = {};
  uint32_t notify_irq[MAX_NOTIFY_IRQ] = {};
  std::vector<uint64_t> gpu_regions;
  bool gpu_reserved_mem;
  int arbiter_aee_off = 0;

  if (gpu_sched_raw == nullptr) {
    return;
  }

  gpu_sched_raw->GetGpuSchedInfo(guest_ipa, ipa_size, notify_irq, gpu_irq,
                                 gpu_regions, gpu_reserved_mem);
  gpu_sched_raw->GetGpuSchedAeeInfo(guest_aee_ipa, guest_aee_size);
  FXL_LOG(INFO) << "[swa-aee]add_arbiter_dts GetGpuSchedAeeInfo("
                << guest_aee_ipa << ", " << guest_aee_size << ") done\n";
  for (int i = 0; i < MAX_NOTIFY_IRQ; i++) {
    if (notify_irq[i] == 0) {
      FXL_LOG(ERROR) << "Skip arbiter dts: invalid notify irq index " << i;
      return;
    }
  }
  for (int i = 0; i < MAX_GPU_IRQ; i++) {
    if (gpu_irq[i] == 0) {
      FXL_LOG(ERROR) << "Skip arbiter dts: invalid gpu irq index " << i;
      return;
    }
  }
  std::vector<uint32_t> props;
  bool is_qemu = is_qemu_platform();
  const char* irq_names[] = {
      "GRANT",    "STOP",     "LOST",     "RCVR",     "MALI-JOB",
      "MALI-MMU", "MALI-GPU", "MALI-EVT", "MALI-PWR",
  };

  for (int i = 0; i < MAX_NOTIFY_IRQ; i++) {
    if (notify_irq[i] >= kESpiBase) {
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_ESPI));
      props.push_back(cpu_to_fdt32(notify_irq[i] - kESpiBase));
    } else {
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
      props.push_back(cpu_to_fdt32(notify_irq[i] - 32));
    }
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
    if (!is_qemu)
      props.push_back(0);
  }

  for (int i = 0; i < MAX_GPU_IRQ; i++) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(gpu_irq[i] - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
    if (!is_qemu) {
      props.push_back(0);
    }
  }

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "invalid device tree";
    return;
  }

  int arbiter_off = -1;
  int offset = fdt_path_offset(dtb, "/");
  if (offset < 0) {
    FXL_LOG(WARNING) << "/ path not found, skipped";
    goto out;
  }

  arbiter_off = fdt_add_subnode(dtb, offset, "arbiter");
  if (arbiter_off < 0) {
    FXL_LOG(WARNING) << "failed to add abiter node.";
    goto out;
  }

  ret = fdt_setprop_string(dtb, arbiter_off, "compatible", "mali,sw-arbiter");
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set arbiter compatible.";
    goto out;
  }

  ret = fdt_setprop_cells_u64(dtb, arbiter_off, "reg", 2, guest_ipa, ipa_size);
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set arbiter interrupts.";
    goto out;
  }

  ret = fdt_setprop(dtb, arbiter_off, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  if (ret < 0) {
    FXL_LOG(WARNING) << "failed to set arbiter interrupts.";
    goto out;
  }

  for (int i = 0; i < ARRAY_SIZE(irq_names); i++) {
    fdt_appendprop_string(dtb, arbiter_off, "interrupt-names", irq_names[i]);
  }

  if (gpu_reserved_mem) {
    // reseve memory for aribter test.
    uint32_t length = 3;
    std::vector<uint64_t> reg_data;
    reg_data.reserve(gpu_regions.size());
    for (int i = 0; i < gpu_regions.size(); i += length) {
      reg_data.push_back(cpu_to_fdt64(gpu_regions[i + 0]));
      reg_data.push_back(cpu_to_fdt64(gpu_regions[i + 1]));
    }

    int offs = fdt_path_offset(dtb, "/reserved-memory");

    if (offs < 0) {
      offs = fdt_add_subnode(dtb, 0, "reserved-memory");
      FXL_CHECK(offs > 0) << "reserved-memory";
      fdt_setprop(dtb, offs, "ranges", nullptr, 0);
      fdt_setprop_cell(dtb, offs, "#size-cells", 0x02);
      fdt_setprop_cell(dtb, offs, "#address-cells", 0x02);
      offs = fdt_path_offset(dtb, "/reserved-memory");
    }

    offs = fdt_add_subnode(dtb, offs, "arbiter_res_mem");
    FXL_CHECK(offs > 0) << "arbiter_res_mem";
    fdt_setprop_string(dtb, offs, "compatible", "mali,arbiter_res_mem");
    fdt_setprop(dtb, offs, "reg", reg_data.data(),
                static_cast<int>(reg_data.size() * sizeof(uint64_t)));
    fdt_setprop(dtb, offs, "no-map", nullptr, 0);
  }

  if (guest_aee_size > 0) {
    arbiter_aee_off = fdt_add_subnode(dtb, offset, "arbiter-aee");
    if (arbiter_aee_off < 0) {
      FXL_LOG(WARNING) << "[swa-aee]failed to add abiter aee node.";
      goto out;
    }

    ret = fdt_setprop_string(dtb, arbiter_aee_off, "compatible",
                             "mali,sw-arbiter-aee");
    if (ret < 0) {
      FXL_LOG(WARNING) << "[swa-aee]failed to set arbiter aee compatible.";
      goto out;
    }

    FXL_LOG(INFO) << "[swa-aee]add_arbiter_dts add reg dts(" << guest_aee_ipa
                  << ", " << guest_aee_size << ")...\n";
    ret = fdt_setprop_cells_u64(dtb, arbiter_aee_off, "reg", 2, guest_aee_ipa,
                                guest_aee_size);
    if (ret < 0) {
      FXL_LOG(WARNING) << "[swa-aee]failed to set arbiter aee reg.";
      goto out;
    }
  }

out:
  fdt_pack(dtb);
}

static zx_status_t sec_boot_uos_lk2(uintptr_t guest_ip, machina::Guest& guest) {
  uint32_t product_id;
  uint32_t sec_boot;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_PRODUCT_ID, &product_id);
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_SEC_BOOT, &sec_boot);
  FXL_LOG(INFO) << "product_id:" << product_id
                << "   vmid:" << zx_log_guest_vmid(guest.vmid())
                << "   sec_boot:" << sec_boot;

#if SECURE_BOOT_WITH_LICENSE == 1
  int rc = nbl_act_status(NMT_ACT_PRODUCT_BASIC);
  if (rc != 0) {
    FXL_LOG(WARNING) << "SECURE BOOT DISABLED due to License not activated";
    return ZX_OK;
  }
#endif

  std::string str_pubk;
  // according produce id to slect str_pubk
  switch (product_id) {
    case BOARD_VID_QEMU:
      return ZX_OK;
    case 20:  // product_id = 20 , 8678 for BYD
      // get pubk
      str_pubk =
          "-----BEGIN PUBLIC KEY-----\n"
          "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA2zVjz+NL4XCxpTAmOsep\n"
          "Uuieh1lRID0+w5VG3vRELegjipre752rUiWCp0u0tf/TTcmzSakxVIFCvkdEFpYQ\n"
          "ze8+05ZBT/1uws9k02C5vorB1phXdDsTk8JPew5QNo4b8/nxsASufwifdmodUQuC\n"
          "fsvmSngazA0qYtRgMVShe097C7cPeSS40JvhsagY49cELMjNVKWmgMd7a8e+RUfa\n"
          "2ZwgKkwY9JgEqIeqI+8UUENJqnCsK2bG9IjFscZrM11IZrQt3Ise/CydG5TuxC2Q\n"
          "Te7UV3Bl0lF0r44RNCKO9o3sMTFNb206EIwxs4qVDDDVtpNG0Dalt7su86hMs2Gu\n"
          "XwIDAQAB\n"
          "-----END PUBLIC KEY-----\n";
      break;
    default:
      break;
  }
  if (sec_boot) {
    if (sec_boot == 11) {
      str_pubk =
          "-----BEGIN PUBLIC KEY-----\n"
          "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvXjNmeR7iflrbcp/A+3i\n"
          "3B07blfQUZOZ1+TbDpbJe1YikaV5KBdIeolaOPkPlirxjYH6pJTt4ACBMlbzkTgX\n"
          "C2q1+w1QVsseOsP5xnTTYK4V0asc6q66I9Cg5se16nHoCHVzvfqpidP5NFr7APA2\n"
          "I4J1ANGH8Hb7x+H018JBWLdaF3LQHNd/pnT5CNANY1f6hkQtptifLydkkz34FL+6\n"
          "TDUgix5Es9OljY1/WejRBr97czzNekWhRwTlb5o9D20bigJUxPcU0oMGM3b9cG6s\n"
          "Pj7lVBCgdqh6bw4wCVVRMuuqA2hkwQr9WWKXMyEqEWmMg5pd/cF/axcz2p0MisA9\n"
          "TQIDAQAB\n"
          "-----END PUBLIC KEY-----\n";
    } else if (sec_boot == 12) {
      str_pubk =
          "-----BEGIN PUBLIC KEY-----\n"
          "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvn55XgD1e5pR4+3kFDHn\n"
          "LQlVIJC1CtpS+1VaI68SuPLZIFf78QzGr3ypScZ9BMdVwwdHMrg6CNLRjkoqFPHr\n"
          "82PPtb+0DVSCfTq2wA8MyiHc3Fp3Mv4mTwMYazrFTAhU62fug0WTVUF2ZqhrBuXX\n"
          "arhSEO0ZCtOYq/nEQ1AWaqIgjgR8rE98ECSrRz0vAjdT5BChTsx/4cJ1z4jv6icF\n"
          "tAhHqMGWhzQjJP5LHWeass7DivNXC9JrY+Ca0okAmyXAKSwFhYJTPXNqi1qrIsCV\n"
          "9COLpnnId9qCuRca9cZhkf888Ygh/PJ1EbMD37sqH5HQ5jX0WuTYmusNU6iL4Hc7\n"
          "lwIDAQAB\n"
          "-----END PUBLIC KEY-----\n";
    }

    uintptr_t image_addr_start_pre = (uintptr_t)(guest_ip - 0x2000);

    int status = guest.Init_Sec_Boot(image_addr_start_pre);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map sec_boot mem";
      return status;
    }

    zx_vaddr_t image_addr_start = guest.va_sec_boot + 0xd00;
    zx_vaddr_t addr_lk2img_size = image_addr_start + LK2IMG_SIZE_OFFSET;
    char* str_size = reinterpret_cast<char*>(addr_lk2img_size);

    /* deal string */
    size_t size_Value = std::stoi(str_size);
    const char* str_pubk_ptr = str_pubk.c_str();

    // get signature
    zx_vaddr_t addr_signature = image_addr_start + (zx_vaddr_t)size_Value;
    unsigned char* str_signature =
        reinterpret_cast<unsigned char*>(addr_signature);
    OpenSSL_add_all_algorithms();
    ERR_load_BIO_strings();
    ERR_load_crypto_strings();
    BIO* bio = BIO_new(BIO_s_mem());
    BIO_puts(bio, str_pubk_ptr);
    EVP_PKEY* pubKey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pubKey);
    void* file_addr_start = reinterpret_cast<void*>(image_addr_start);
    EVP_DigestVerifyUpdate(ctx, file_addr_start, size_Value);
    size_t signature_len = 256;  // signature len
    int result = EVP_DigestVerifyFinal(ctx, str_signature, signature_len);
    if (result != 1) {
      FXL_LOG(ERROR) << "loading bl-an.img sec boot fail:";
      return -1;
    }
    FXL_LOG(INFO) << "loading bl-an.img sec boot success:";
    EVP_MD_CTX_free(ctx);
    BIO_free_all(bio);
    EVP_PKEY_free(pubKey);
    if (sec_boot > 0) {
      status = guest.Release_Init_Sec_Boot();
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to unmap sec_boot mem";
      }
    }
  }
  return 0;
}

static zx_status_t platform_init_rpmb(fbl::unique_ptr<machina::VirtioRpmb>& rpmb,
                                      machina::Guest& guest,
                                      machina::PciBus& bus) {
  if (is_qemu_platform())
    return ZX_OK;

  int const guest_vmid = guest.vmid();
  int const rpmb_vmid = machina::GuestVmidToBlockBackendVmid(guest_vmid);
  if (rpmb_vmid < machina::kBlockSosBackendVmid ||
      rpmb_vmid >= UFS_VIRTIO_RPMB_MAX_GUESTS) {
    FXL_LOG(ERROR) << "invalid rpmb backend vmid: guest_vmid="
                   << zx_log_guest_vmid(guest_vmid)
                   << " rpmb_vmid=" << rpmb_vmid;
    return ZX_ERR_INVALID_ARGS;
  }

  FXL_LOG(INFO) << "InitializeVirtioRpmb guest_vmid="
                << zx_log_guest_vmid(guest_vmid)
                << " backend_vmid=" << rpmb_vmid
                << " phys_base=0x" << std::hex << guest.phys_mem().phys_base()
                << " size=0x" << guest.phys_mem().size() << std::dec;

  auto virtio_rpmb = fbl::make_unique<machina::VirtioRpmb>(
      guest.phys_mem(), static_cast<uint16_t>(rpmb_vmid),
      machina::VirtioDevice::Transport::PCI, guest.device_async());
  if (!virtio_rpmb) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = bus.Connect(virtio_rpmb->pci_device());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "pci rpmb device connect bus failed " << status;
    return status;
  }

  rpmb = fbl::move(virtio_rpmb);
  return ZX_OK;
}

static zx_status_t init_mmio_block(
    std::vector<fbl::unique_ptr<machina::VirtioBlock>>& virtio_blocks,
    machina::Guest& guest, machina::MmioBus& mmio_bus) {
  if (is_qemu_platform())
    return ZX_OK;
  struct BlockBackendConfig {
    const char* name;
    uint16_t ufs_lun;
  };
  static const BlockBackendConfig kBlockBackends[] = {
      {"ufs-lun0", 0},
      {"ufs-lun1", 1},
      {"ufs-lun2", 2},
  };

  for (const auto& backend : kBlockBackends) {
    const char* const blk_backend_name = backend.name;
    const bool shared_block = backend.ufs_lun == 0 || backend.ufs_lun == 1;
    if (shared_block) {
      continue;
    }

    zx_status_t status;
    FXL_LOG(INFO) << "init_virtio_block: " << blk_backend_name;
    fbl::unique_fd backend_fd;
    status = block_locator::OpenUfsLun(
        backend.ufs_lun, block_locator::OpenMode::kReadWrite,
        ZX_TIME_INFINITE, &backend_fd);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to resolve direct-IO backend "
                     << blk_backend_name << ": " << status;
      return status;
    }

    fbl::unique_ptr<machina::BlockDispatcher> dispatcher;
    machina::BlockDispatcher::DispatcherOptions fdio_opts = {
        .mode = machina::BlockDispatcher::Mode::RW,
        .data_plane = machina::BlockDispatcher::DataPlane::DIRECTIO,
        .vmid = guest.vmid(),
    };
    status = machina::BlockDispatcher::CreateFromFd(
        backend_fd.release(), fdio_opts, guest.phys_mem(), &dispatcher);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to create direct-IO block_dispatcher for "
                     << blk_backend_name << ": " << status;
      return status;
    }

    auto fdio_block = fbl::make_unique<machina::VirtioBlock>(guest.phys_mem());
    if (!fdio_block) {
      return ZX_ERR_NO_MEMORY;
    }
    status = fdio_block->SetDispatcher(fbl::move(dispatcher));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to set direct-IO virtio_block dispatcher for "
                     << blk_backend_name << ": " << status;
      return status;
    }
    status = mmio_bus.Connect(fdio_block->mmio_device());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "mmio block device connect bus failed for "
                     << blk_backend_name << ": " << status;
      return status;
    }
    virtio_blocks.push_back(fbl::move(fdio_block));
  }

  return ZX_OK;
}

static zx_status_t init_pci_block(
    std::vector<fbl::unique_ptr<machina::VirtioBlock>>& virtio_blocks,
    machina::Guest& guest, machina::PciBus& bus) {
  if (is_qemu_platform())
    return ZX_OK;
  struct BlockBackendConfig {
    const char* name;
    uint16_t ufs_lun;
  };
  static const BlockBackendConfig kBlockBackends[] = {
      {"ufs-lun0", 0},
      {"ufs-lun1", 1},
      {"ufs-lun2", 2},
  };

  for (const auto& backend : kBlockBackends) {
    const char* const blk_backend_name = backend.name;
    const bool shared_block = backend.ufs_lun == 0 || backend.ufs_lun == 1;
    if (shared_block && guest.vmid() != -1) {
      continue;
    }

    zx_status_t status;
    FXL_LOG(INFO) << "init_virtio_block: " << blk_backend_name;
    fbl::unique_fd backend_fd;
    status = block_locator::OpenUfsLun(
        backend.ufs_lun, block_locator::OpenMode::kReadWrite,
        ZX_TIME_INFINITE, &backend_fd);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to resolve FIFO backend " << blk_backend_name
                     << ": " << status;
      return status;
    }

    fbl::unique_ptr<machina::BlockDispatcher> fifo_dispatcher;
    machina::BlockDispatcher::DispatcherOptions fifo_opts = {
        .mode = machina::BlockDispatcher::Mode::RW,
        .data_plane = machina::BlockDispatcher::DataPlane::FIFO,
        .vmid = guest.vmid(),
    };
    status = machina::BlockDispatcher::CreateFromFd(
        backend_fd.release(), fifo_opts, guest.phys_mem(), &fifo_dispatcher);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to create fifo block_dispatcher for "
                     << blk_backend_name << ": " << status;
      return status;
    }

    auto block = fbl::make_unique<machina::VirtioBlock>(
        guest.phys_mem(), machina::VirtioBlock::Transport::PCI);
    if (!block) {
      return ZX_ERR_NO_MEMORY;
    }
    status = block->SetDispatcher(fbl::move(fifo_dispatcher));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to set fifo virtio_block dispatcher for "
                     << blk_backend_name << ": " << status;
      return status;
    }

    status = block->Start();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "VirtioBlock start failed for " << blk_backend_name
                     << ": " << status;
      return status;
    }

    status = bus.Connect(block->pci_device());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "pci block device connect bus failed for "
                     << blk_backend_name << ": " << status;
      return status;
    }
    virtio_blocks.push_back(fbl::move(block));
  }

  return ZX_OK;
}

static zx_status_t init_pvblk_block(
    std::vector<fbl::unique_ptr<machina::PvBlockDevice>>& pv_blocks,
    machina::Guest& guest,
    machina::InterruptController& interrupt_controller) {
  if (is_qemu_platform()) {
    return ZX_OK;
  }

  struct BlockBackendConfig {
    const char* name;
    uint16_t ufs_lun;
  };
  static const BlockBackendConfig kBlockBackends[] = {
      {"ufs-lun0", 0},
      {"ufs-lun1", 1},
      {"ufs-lun2", 2},
  };

  for (uint32_t index = 0; index < machina::kPvBlockDeviceCount; ++index) {
    if (!pvblk_backend_visible_to_uos_guest(guest.vmid(), index)) {
      continue;
    }
    uint64_t mmio_base =
        machina::kPvBlockMmioBase +
        index * static_cast<uint64_t>(PVBLK_MMIO_SIZE);
    uint32_t irq = machina::kPvBlockIrqBase + index;
    fbl::unique_fd backend_fd;
    zx_status_t status = block_locator::OpenUfsLun(
        kBlockBackends[index].ufs_lun, block_locator::OpenMode::kReadWrite,
        ZX_TIME_INFINITE, &backend_fd);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to resolve pvblk backend "
                     << kBlockBackends[index].name << ": " << status;
      return status;
    }
    fbl::unique_ptr<machina::PvBlockDevice> pvblk;
    status = machina::PvBlockDevice::CreateFromFd(
        backend_fd.release(), &guest, &interrupt_controller, mmio_base, irq,
        &pvblk);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to initialize pvblk backend for "
                     << kBlockBackends[index].name << ": " << status;
      return status;
    }
    pv_blocks.push_back(fbl::move(pvblk));
  }
  return ZX_OK;
}

static void config_check(nbl_vmm::VmConfig& cfg)
{

  auto& v = cfg.vgic();
  std::vector<uint16_t> irqs;

  for (int i = 0; i < v.interrupts_size(); i++) {
    irqs.push_back(v.interrupts(i));
  }

  for (int i = 1; i < cfg.vsock_irqs_size(); i+=2) {
    auto irq = cfg.vsock_irqs(i);
    irqs.push_back(irq);
  }

  for (int i = 1; i < cfg.apu_irqs_size(); i+=2) {
    auto irq = cfg.apu_irqs(i);
    irqs.push_back(irq);
  }

  for (int i = 1; i < cfg.cmdq_irqs_size(); i+=2) {
    auto irq = cfg.cmdq_irqs(i);
    irqs.push_back(irq);
  }

  {
    auto irq = std::find(irqs.begin(), irqs.end(), cfg.sched_irq());
    FXL_CHECK(irq == irqs.end()) << "sched-irq can not set passthrough:" << cfg.sched_irq();
  }

  {
    auto irq = std::find(irqs.begin(), irqs.end(), cfg.dump_irq());
    FXL_CHECK(irq == irqs.end()) << "dump-irq can not set passthrough:" << cfg.dump_irq();
  }

  FXL_CHECK(machina::ValidateGpuSchedVcpus(cfg.gpu_sched_vcpus(), cfg.num_cpus()) ==
            ZX_OK)
      << "gpu_sched_vcpus must contain unique IDs less than num_cpus";

}

int main(int argc, char** argv) {
  FXL_CHECK(argc == 2) << "Missing vmid argument";
  uint16_t vmid = std::stoul(argv[1]);

  fsl::MessageLoop loop;
  std::unique_ptr<component::ApplicationContext> application_context =
      component::ApplicationContext::CreateFromStartupInfo();

  machina::VhmDevice vhm_device(vmid);
  component::ConnectToEnvironmentService(vhm_device.NewRequest());
  vhm_device.FetchVmConfig();

  nbl_vmm::VmConfig cfg;
  FXL_CHECK(cfg.ParseFromString(vhm_device.cfg()));

  machina::Guest guest;
  guest.SetVmid(vmid);
  auto vmems = vmems_build_from(cfg);
  zx_status_t status = guest.Init(vmems);
  FXL_CHECK(status == ZX_OK) << "failed to init guest:" << status;
  if (status != ZX_OK) {
    return status;
  }

  GuestFeatures features;
  zx_status_t feature_status = ZX_ERR_INTERNAL;
  LoadGuestFeatures(
      application_context.get(),
      [&loop, &feature_status, &features](zx_status_t status,
                                          GuestFeatures loaded_features) {
        feature_status = status;
        if (status == ZX_OK) {
          features = loaded_features;
        }
        loop.PostQuitTask();
      });
  loop.Run();
  status = feature_status;
  FXL_CHECK(status == ZX_OK)
      << "Failed to load guest feature config: " << status;
  machina::GuestDeviceAccessPolicy device_access_policy;
  device_access_policy.spi_dma_control_enabled = features.virtio_spi_enabled();
  device_access_policy.i2c_dma_control_enabled = features.virtio_i2c_enabled();
  guest.SetDeviceAccessPolicy(device_access_policy);

  config_check(cfg);
  auto reg_infos = default_reg_build_from(cfg);
  status = guest.SetDefaultValue(reg_infos, true);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "failed to set default value";
  }
#if 0
  // Setup multi SMMUv3
  std::vector<std::unique_ptr<machina::Smmuv3>> vsmmus;
  auto smmu_specs = vsmmu_build_from(cfg);
  if (smmu_specs.empty()) {
    FXL_LOG(INFO) << "Failed to initialize UOS vSmmu devices: no valid "
                     "configurations provided.";
  }
  for (const auto& spec : smmu_specs) {
    auto smmu_type = machina::Smmuv3::GetSmmuType(spec.paddr);
    auto vsmmu = std::make_unique<machina::Smmuv3>(
        &guest, static_cast<uint8_t>(smmu_type));

    status = vsmmu->Init(spec);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create UOS vSmmu device";
      return status;
    }
    vsmmus.push_back(std::move(vsmmu));
  }

  if (vsmmus.empty())
    FXL_LOG(ERROR) << "All UOS vSmmu device initializations failed.";
  else
    FXL_LOG(INFO) << "Successfully created multi UOS vSmmu devices.";
#endif

  guest.SetSchedIRQ(cfg.sched_irq());
  guest.SetDumpIRQ(cfg.dump_irq());
  guest.SetUosExpIRQ(cfg.uos_exp_irq());

  std::vector<std::unique_ptr<machina::RegAccess>> reg_handlers;
  auto specs = convert_proto_to_specs(cfg);
  if (!specs.empty()) {
    status = machina::RegisterRegAccessHandlers(guest, specs, reg_handlers);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "UOS: Failed to initialize register access traps";
        return status;
    }
  }

  uintptr_t guest_ip = cfg.entry();

  status = sec_boot_uos_lk2(guest_ip, guest);
  FXL_CHECK(status == ZX_OK) << "failed to init guest:" << status;
  if (status != ZX_OK) {
    return status;
  }

  if (features.pci_bus_enabled()) {
    std::vector<uint32_t> pci_irqs;
    int pci_irqs_size = cfg.pci_irqs_size();
    for (int i = 0; i < pci_irqs_size; i++) {
      pci_irqs.push_back(cfg.pci_irqs(i));
    }

    status = machina::PciBus::config_pci_global_irqs(pci_irqs);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to config pci global irqs: " << status;
      return status;
    }
  }

  std::vector<uint32_t> bind_pcpus;
  int bind_cpu_size = cfg.bind_pcpus_size();
  if (bind_cpu_size != 0) {
    FXL_CHECK(bind_cpu_size == cfg.num_cpus())
        << "cpu num mismtach:" << bind_cpu_size << "," << cfg.num_cpus();

    for (int i = 0; i < bind_cpu_size; i++) {
      zx_poweron_phy_cpu(cfg.bind_pcpus(i));
      bind_pcpus.push_back(cfg.bind_pcpus(i));
    }
    status = guest.SetCpuAffinity(bind_pcpus.data(), bind_cpu_size);
    FXL_CHECK(status == ZX_OK) << "failed to set cpu affinity:" << status;
  }

  guest.SetCpuNums(cfg.num_cpus());
  guest.SetUserVmid(guest.vmid() & 0xff);
  guest.SetSchedId((uint8_t)cfg.sched_id());
  guest.SetBindLkPcpu(cfg.bindlk_pcpu());
  std::vector<uint32_t> wakeup_irqs;
  auto wakeup_irq_num = cfg.wakeup_irqs_size();
  for (int i = 0; i < wakeup_irq_num; i++) {
    wakeup_irqs.push_back(cfg.wakeup_irqs(i));
  }

  guest.SetWakeupIrqs(wakeup_irqs.data(), wakeup_irq_num);
  if (features.smc_enabled()) {
    guest.SetSmcIRQ(cfg.smc_irq());
  }
  if (features.cvm_lock_enabled()) {
    guest.SetCvmLockIRQ(cfg.cvm_lock_irq());
  }

  // Instantiate the inspect service.
  auto inspect_svc = std::make_unique<machina::InspectServiceImpl>(
      application_context.get(), guest.phys_mem());

  std::unique_ptr<VmlogStore> log_store_;
  if (features.vmlog_enabled()) {
    FXL_LOG(INFO) << "create VM logstore";
    log_store_ = std::make_unique<VmlogStore>();
    if (log_store_ == nullptr) {
      FXL_LOG(ERROR) << "Failed to create vm log store";
      return ZX_ERR_NO_MEMORY;
    }

    status = log_store_->Initialize(guest.vmid());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to initial vmlog store: " << status;
      return status;
    }
  }

  // Setup virtual UARTs.
  FXL_LOG(INFO) << "create UOS uart device";
  machina::Uart uart;
  status = uart.Init(&guest, machina::kPl011PhysBase, guest.vmid(),
                     log_store_.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create UART at " << std::hex
                   << machina::kPl011PhysBase;
    return status;
  }

  machina::InterruptServiceSyncPtr interrupt_svc;
  component::ConnectToEnvironmentService(interrupt_svc.NewRequest());

  // Setup interrupt controller.
  machina::InterruptController interrupt_controller(&guest, &interrupt_svc);
  machina::GicIts gic_its(&guest, &interrupt_controller);
  auto vgic = vgic_build_from(cfg);
  if (cfg.has_vgic()) {
    interrupt_controller.SetSPIVcpuMask(0xFF);
    add_vtee_irq_to_vgic(cfg, vgic);
    status = interrupt_controller.Init(cfg.num_cpus(), machina::Gic::V3, vgic);
    if (status == ZX_OK && vgic.has_its) {
      status = gic_its.Init(vgic.its_paddr);
      if (status == ZX_OK) {
        interrupt_controller.EnableIts(&gic_its);
      }
    }
  } else {
    std::vector<uint16_t> interrupts, percpu_interrupts;
    status = interrupt_controller.Init(cfg.num_cpus(), machina::Gic::V3,
                                       interrupts, percpu_interrupts);
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create interrupt controller";
    return status;
  }

  std::unique_ptr<machina::MmioBus> mmio_bus;
  if (features.mmio_bus_enabled()) {
    /* the MMIO interrupt number should be same with configuration in .lua */
    mmio_bus = std::make_unique<machina::MmioBus>(
        &guest, &interrupt_controller, kMmioIrqVector);
  }

  std::unique_ptr<machina::PciBus> pci_bus;
  if (features.pci_bus_enabled()) {
    pci_bus = std::make_unique<machina::PciBus>(
        &guest, &interrupt_controller, &gic_its);
    status = pci_bus->Init();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create PCI bus";
      return status;
    }
  }

  fbl::unique_ptr<machina::VirtioRpmb> virtio_rpmb;

  std::vector<fbl::unique_ptr<machina::VirtioBlock>> virtio_blocks;
  std::vector<fbl::unique_ptr<machina::PvBlockDevice>> pv_blocks;
  const bool use_pvblk_for_guest =
      should_use_pvblk_for_uos_guest(features, cfg, guest.vmid());
  if (use_pvblk_for_guest) {
    status = init_pvblk_block(pv_blocks, guest, interrupt_controller);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to initialize PVBlock device";
      return status;
    }
  }
  if (!use_pvblk_for_guest && features.mmio_virtio_block_enabled()) {
    if (!mmio_bus) {
      FXL_LOG(ERROR) << "MMIO virtio-block requires MMIO bus";
      return ZX_ERR_BAD_STATE;
    }
    status = init_mmio_block(virtio_blocks, guest, *mmio_bus);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to initialize MMIO block device";
      return status;
    }
  }

  if (!use_pvblk_for_guest && features.pci_virtio_block_enabled()) {
    if (!pci_bus) {
      FXL_LOG(ERROR) << "PCI virtio-block requires PCI bus";
      return ZX_ERR_BAD_STATE;
    }
    status = init_pci_block(virtio_blocks, guest, *pci_bus);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to initialize PCI block device";
      return status;
    }
  }

  if (features.ufs_enabled()) {
    if (!pci_bus) {
      FXL_LOG(ERROR) << "virtio-rpmb requires PCI bus";
      return ZX_ERR_BAD_STATE;
    }
    status = platform_init_rpmb(virtio_rpmb, guest, *pci_bus);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to initialize virtio rpmb device";
      return status;
    }
  }

  status = vhm_device.Init(&guest, &interrupt_controller, &gic_its);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize vhm device";
    return status;
  }
  auto shutdown_vhm_device =
      fbl::MakeAutoCall([&vhm_device]() { vhm_device.Shutdown(); });

  std::unique_ptr<machina::VirtioSPI> spi;
  if (features.virtio_spi_enabled()) {
    if (!pci_bus) {
      FXL_LOG(ERROR) << "virtio-spi requires PCI bus";
      return ZX_ERR_BAD_STATE;
    }
    //Setup spi BE
    FXL_LOG(INFO) << "spi be create.";
    spi = std::make_unique<machina::VirtioSPI>(guest.phys_mem());
    status = pci_bus->Connect(spi->pci_device());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "spi devices connect bus failed " << status;
      return status;
    }
  }

  std::unique_ptr<machina::VirtioI2C> i2c;
  if (features.virtio_i2c_enabled()) {
    if (!pci_bus) {
      FXL_LOG(ERROR) << "virtio-i2c requires PCI bus";
      return ZX_ERR_BAD_STATE;
    }
    //Setup i2c BE
    FXL_LOG(INFO) << "i2c be create.";
    i2c = std::make_unique<machina::VirtioI2C>(guest.phys_mem());
    status = pci_bus->Connect(i2c->pci_device());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "i2c devices connect bus failed " << status;
      return status;
    }
  }

  std::unique_ptr<machina::VirtioEINT> eint;
  if (features.virtio_eint_enabled()) {
    if (!pci_bus) {
      FXL_LOG(ERROR) << "virtio-eint requires PCI bus";
      return ZX_ERR_BAD_STATE;
    }
    //Setup eint BE
    FXL_LOG(INFO) << "eint be create.";
    eint = std::make_unique<machina::VirtioEINT>(guest.phys_mem());
    eint->SetVmid(vmid);
    status = pci_bus->Connect(eint->pci_device());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "eint devices connect bus failed " << status;
      return status;
    }
  }

  std::unique_ptr<machina::VirtioRTC> rtc;
  if (features.virtio_rtc_enabled() && !is_qemu_platform()) {
    if (!pci_bus) {
      FXL_LOG(ERROR) << "virtio-rtc requires PCI bus";
      return ZX_ERR_BAD_STATE;
    }
    FXL_LOG(INFO) << "rtc be create.";
    rtc = std::make_unique<machina::VirtioRTC>(guest.phys_mem());
    status = rtc->Init(guest.vmid());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to init RTC: " << status;
      return status;
    }
    status = pci_bus->Connect(rtc->pci_device());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "rtc device connect bus failed " << status;
      return status;
    }
    FXL_LOG(INFO) << "rtc device connect pci bus successed. device id: "
                  << rtc->device_id();
  }

  machina::IpcClient ipc_mbox_client(&guest);
  component::ConnectToEnvironmentService(ipc_mbox_client.NewRequest());
  ipc_mbox_client.Init(vmid);
  guest.RegisterIpcRequestHandler(&ipc_mbox_client);

  std::unique_ptr<machina::MemControllerClient> mem_controller_client;
  if (cfg.virtio_mem_enable()) {
      mem_controller_client = std::make_unique<machina::MemControllerClient>(&guest);
  }

  guest.InitDump();

  machina::IpcMessageServiceSyncPtr IpcMessClient;
  component::ConnectToEnvironmentService(IpcMessClient.NewRequest());
  zx_vaddr_t tmp_mem_pa_;
  size_t tmp_mem_sz_;
  IpcMessClient->GetTraceBuf(&tmp_mem_pa_, &tmp_mem_sz_);
  guest.SetTraceMem(tmp_mem_pa_, tmp_mem_sz_);

#ifdef VHE_MMIO_TRAP_DEBUG
  IpcMessClient->GetMonitorVirtioMem(&tmp_mem_pa_, &tmp_mem_sz_);
  guest.SetMonitorVirtioMem(tmp_mem_pa_, tmp_mem_sz_);
#endif

  machina::GpuPingClient gpu_ping_client(&guest);
  component::ConnectToEnvironmentService(gpu_ping_client.NewRequest());
  gpu_ping_client.MapVmo();
  gpu_ping_client.GetPingIrq();
  IpcMessClient->GetCvmdBuf(&tmp_mem_pa_, &tmp_mem_sz_);
  guest.SetCvmdMem(tmp_mem_pa_, tmp_mem_sz_);
  guest.InitCvmdDump();


  machina::RprocServiceSyncPtr rproc_svc;
  component::ConnectToEnvironmentService(rproc_svc.NewRequest());
  RprocClient rproc_client(vmid, std::move(rproc_svc), guest,
                           interrupt_controller);

  std::unique_ptr<machina::CrossVMLockSOSClient> cvm_lock_sos_client;
  if (features.cvm_lock_enabled()) {
    cvm_lock_sos_client = std::make_unique<machina::CrossVMLockSOSClient>(&guest);
    component::ConnectToEnvironmentService(cvm_lock_sos_client->NewRequest());
    cvm_lock_sos_client->MapVmo();

    zx::vmo dup_vmo;
    status = cvm_lock_sos_client->GetVmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
    FXL_CHECK(status == ZX_OK);
    guest.SetCvmLockVMO(dup_vmo.get());
  }

  auto vsock_vq_cnt = cfg.vsock_irqs_size() / 2;
  if (vsock_vq_cnt) {
    std::vector<uint16_t> irqs;
    for (int qidx = 0; qidx < vsock_vq_cnt; qidx++) {
      auto irq = cfg.vsock_irqs(2 * qidx + 1);
      irqs.push_back(irq);
    }
    status = interrupt_controller.PassThroughInterrupts(irqs);
    FXL_CHECK(status == ZX_OK);
  }

  auto apu_vq_cnt = cfg.apu_irqs_size() / 2;
  if (apu_vq_cnt) {
    std::vector<uint16_t> irqs;
    for (int qidx = 0; qidx < apu_vq_cnt; qidx++) {
      auto irq = cfg.apu_irqs(2 * qidx + 1);
      irqs.push_back(irq);
    }
    status = interrupt_controller.PassThroughInterrupts(irqs);

    FXL_CHECK(status == ZX_OK);
  }

  auto cmdq_vq_cnt = cfg.cmdq_irqs_size() / 2;
  if (cmdq_vq_cnt) {
    std::vector<uint16_t> irqs;
    for (int qidx = 0; qidx < cmdq_vq_cnt; qidx++) {
      auto irq = cfg.cmdq_irqs(2 * qidx + 1);
      irqs.push_back(irq);
    }
    status = interrupt_controller.PassThroughInterrupts(irqs);

    FXL_CHECK(status == ZX_OK);
  }

  std::unique_ptr<VmlogSrv> vmlog_sink_;
  std::unique_ptr<TipcVqueueNotifier> vqueue_notifier_;
  if (features.vmlog_enabled()) {
    // Setup vmlog sinking
    FXL_LOG(INFO) << "create VmlogSrv BE";
    fxl::UniqueFD alloc_fd(open(kGuestMemoryAllocator, O_RDWR));
    if (!alloc_fd.is_valid()) {
      FXL_LOG(ERROR) << "Failed to open guest memory allocator";
      return ZX_ERR_INTERNAL;
    }
    vmlog_sink_ =
        std::make_unique<VmlogSrv>(alloc_fd, guest.vmid(), log_store_.get());
    if (!vmlog_sink_) {
      FXL_LOG(ERROR) << "Failed to create vmlog_sink_";
      return ZX_ERR_NO_MEMORY;
    }
    vmlog_sink_->Initialize();
    if (machina::IsAlpsVmid(guest.vmid())) {
      // for alps vmlog sink driver
      vqueue_notifier_ =
          std::make_unique<TipcVqueueNotifier>(UINT32_MAX, guest.vmid());
      if (vqueue_notifier_ == nullptr) {
        FXL_LOG(ERROR) << "Failed to create vqueue notifier,vmid: "
                       << zx_log_guest_vmid(guest.vmid());
        return ZX_ERR_NO_MEMORY;
      }
      vmlog_sink_->Start(vqueue_notifier_.get());
    }
  }

  zx::resource root_resource;
  status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "[swa-aee]main_uos failed to get root resource:" << status
                   << "\n";
    return status;
  }

  fbl::unique_ptr<machina::GpuSchedulerClient> gpu_sched;
  machina::GpuSchedulerClient* gpu_sched_raw = nullptr;
  gpu_sched = fbl::unique_ptr<machina::GpuSchedulerClient>(
      new machina::GpuSchedulerClient(&guest));
  component::ConnectToEnvironmentService(gpu_sched->NewRequest());
  gpu_sched->SetVmid(guest.vmid() & 0xff);
  status = gpu_sched->prepareGpuSched(root_resource.get());
  if (status == ZX_OK) {
    gpu_sched_raw = gpu_sched.get();

    uint64_t guest_ipa;
    uint32_t ipa_size;
    uint32_t notify_irq[MAX_NOTIFY_IRQ];
    uint32_t gpu_irq[MAX_GPU_IRQ];
    std::vector<uint64_t> gpu_regions;
    bool gpu_reserved_mem;

    gpu_sched_raw->GetGpuSchedInfo(guest_ipa, ipa_size, notify_irq, gpu_irq,
                                   gpu_regions, gpu_reserved_mem);

    status = interrupt_controller.ConfigureTimeSlicedPtIrqForwarding({
        gpu_irq[JOB_IRQ_INX],
        gpu_irq[MMU_IRQ_INX],
        gpu_irq[GPU_IRQ_INX],
        gpu_irq[EVT_IRQ_INX],
        gpu_irq[PWR_IRQ_INX],
    });
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to configure Android/UOS GPU IRQ forwarding: "
                     << status;
      return status;
    }
  }

  // In the case fdt blob is directly loaded from SOS filesystem, we can
  // patch fdt directly. (extra_params[0] is assumed to be GPA of fdt blob)
  if (!cfg.extra_params().empty()) {
    auto dtb_base = cfg.extra_params()[0];
    auto phys_base = get_phys_base(cfg);
    uint64_t dtb_size = 2 * PAGE_SIZE;
    machina::DeviceTreeSpec dtb = {dtb_base, dtb_size};
    rproc_client.PatchDeviceTree(dtb, phys_base);

    uintptr_t dtb_offset = dtb.base - phys_base;
    void* dtb_ptr = guest.phys_mem().as<void>(dtb_offset, dtb_size);
    status = update_cmdline(cfg, guest.vmid(), dtb_ptr, dtb_size, features);
    FXL_CHECK(status == ZX_OK);
    update_virtio_gpu_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_vsock_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_apu_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_cmdq_irq(cfg, dtb_ptr, dtb_size);
    if (features.smc_enabled()) {
      update_smc_irq(cfg, dtb_ptr, dtb_size);
    }
    if (features.cvm_lock_enabled()) {
      update_cvm_lock_node(cfg, dtb_ptr, dtb_size,
                           cvm_lock_sos_client->mem_gpaddr(),
                           cvm_lock_sos_client->mem_size());
    }
    if (features.pci_bus_enabled()) {
      create_virtio_pci_nodes(cfg, dtb_ptr, dtb_size);
    }
    status = create_pvblk_nodes_for_guest(cfg, guest.vmid(), dtb_ptr, dtb_size,
                                          features);
    FXL_CHECK(status == ZX_OK);
    probe_vtee(cfg, dtb_ptr, dtb_size);
    if (features.vmlog_enabled()) {
      vmlog_sink_->CreateAlpsVmlogSinkNodes(dtb_ptr, dtb_size);
    }
    add_arbiter_dts(dtb_ptr, dtb_size, gpu_sched_raw);
  }
  guest.RegisterDeviceTreePatcher([&cfg, &ipc_mbox_client, &guest, &cvm_lock_sos_client,
                                   &rproc_client,
                                   &vmlog_sink_, &features, &gpu_ping_client, gpu_sched_raw](
                                      uint64_t dtb_base, uint64_t dtb_size) {
    machina::DeviceTreeSpec dtb{dtb_base, dtb_size};
    auto phys_base = get_phys_base(cfg);
    auto status = ipc_mbox_client.CreateMboxDeviceTreeNodes(dtb, phys_base, 0);
    FXL_CHECK(status == ZX_OK);

    guest.CreateDtbSpec(dtb);
    gpu_ping_client.CreateGpuPingTreeNodes(get_phys_base(cfg));
#ifdef VHE_MMIO_TRAP_DEBUG
    auto monitor_virtio = machina::Monitor_virtio::GetInstance();
    FXL_CHECK(monitor_virtio != nullptr);

    monitor_virtio->SetMonitorVirtioMem(guest.monitor_virtio_pa(),
                                        guest.monitor_virtio_size());
    status = monitor_virtio->MapMonitorVirtioMem(guest, false);
    FXL_CHECK(status == ZX_OK);
    if (status != ZX_OK)
      FXL_LOG(ERROR) << "Failed to map Monitor Memory!";
    monitor_virtio->PatchMonitorVirtioDts(guest, phys_base);
#endif
    auto trace = machina::Utrace::GetInstance();
    FXL_CHECK(trace != nullptr);
    trace->SetTraceMem(guest.trace_pa(), guest.trace_size());
    status = trace->MapTraceBuf(guest, false);
    // FXL_CHECK(status == ZX_OK);
    if (ZX_OK == status) {
      trace->PatchTraceDts(guest, phys_base);
    } else {
      FXL_LOG(INFO) << "May not support nebula_trace!";
    }

    guest.InitSched(phys_base, false);
    guest.RegSchedIRQ();
    guest.InitCDump(phys_base, false);
    guest.RegCVMDIRQ();

    status = guest.AddVmemDeviceTreeNodes(dtb, phys_base);
    FXL_CHECK(status == ZX_OK);

    uintptr_t dtb_offset = dtb.base - phys_base;
    void* dtb_ptr = guest.phys_mem().as<void>(dtb_offset, dtb_size);
    status = update_cmdline(cfg, guest.vmid(), dtb_ptr, dtb_size, features);
    FXL_CHECK(status == ZX_OK);

    update_virtio_gpu_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_vsock_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_apu_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_cmdq_irq(cfg, dtb_ptr, dtb_size);
    if (features.smc_enabled()) {
      update_smc_irq(cfg, dtb_ptr, dtb_size);
    }
    if (features.cvm_lock_enabled()) {
      update_cvm_lock_node(cfg, dtb_ptr, dtb_size,
                           cvm_lock_sos_client->mem_gpaddr(),
                           cvm_lock_sos_client->mem_size());
    }
    if (features.pci_bus_enabled()) {
      status = create_virtio_pci_nodes(cfg, dtb_ptr, dtb_size);
      FXL_CHECK(status == ZX_OK);
    }
    status = create_pvblk_nodes_for_guest(cfg, guest.vmid(), dtb_ptr, dtb_size,
                                          features);
    FXL_CHECK(status == ZX_OK);

    rproc_client.PatchDeviceTree(dtb, phys_base);
    probe_vtee(cfg, dtb_ptr, dtb_size);
    if (features.vmlog_enabled()) {
      vmlog_sink_->CreateAlpsVmlogSinkNodes(dtb_ptr, dtb_size);
    }

    add_arbiter_dts(dtb_ptr, dtb_size, gpu_sched_raw);
  });

  guest.RegisterDeviceTreeMdPatcher(
    [&cfg, &guest, &IpcMessClient](
    uint64_t dtb_base, uint64_t dtb_size) {
      machina::DeviceTreeSpec dtb{dtb_base, dtb_size};
      auto phys_base = get_phys_base(cfg);
      uintptr_t dtb_offset = dtb.base - phys_base;
      void* dtb_ptr = guest.phys_mem().as<void>(dtb_offset, dtb_size);
      if (guest.vmid() == 1) {
        machina::DeviceTreeSpec sos_dtb;
        IpcMessClient->GetDeviceTree(&sos_dtb.base, &sos_dtb.size);

        copy_sos_dtb_node(sos_dtb, zx_vmar_root_self(), dtb_ptr, dtb_size);
      }
    }
  );

  auto initialize_vcpu = [&cfg, &interrupt_controller, &bind_pcpus, gpu_sched_raw](
                             machina::Guest* guest, uintptr_t guest_ip,
                             uint64_t id, machina::Vcpu* vcpu) {
    zx_status_t status =
        vcpu->Create(guest, guest_ip, id, kUosVcpuPriority, kUosVcpuTimeSlice);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create VCPU";
      return status;
    }

    // percpu interrupts should be bound in vcpu thread
    vcpu->RegisterStartCallback([&cfg, vcpu, id, &interrupt_controller] {
      auto& redistributors = interrupt_controller.redistributors();
      auto vgic = vgic_build_from(cfg);
      zx_status_t status = redistributors[id]->Init(vgic.percpu_irqs);
      FXL_CHECK(status == ZX_OK);
      redistributors[id]->BindVcpu(vcpu);
    });

    // Register VCPU with ID 0.
    status = interrupt_controller.RegisterVcpu(id, vcpu);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to register VCPU with interrupt controller";
      return status;
    }
    // Setup initial VCPU state.
    zx_vcpu_state_t vcpu_state = {};

    int reg = 0;
    auto extra_params = cfg.extra_params();
    for (auto& param : extra_params) {
      FXL_LOG(INFO) << "x[" << reg << "]=0x" << std::hex << param;
      vcpu_state.x[reg++] = param;
    }

    // Config VCPU group.
    if (id < bind_pcpus.size()) {
      vcpu->SetGroup(bind_pcpus[id]);
    } else {
      vcpu->SetGroup(id);
    }

    if (guest->can_enable_crossvmdump()) {
      vcpu->SetCvmDump();
    }

    // Config VCPU budget
    auto budgets = cfg.budgets();
    if (id < budgets.size()) {
      vcpu->SetBudget(budgets[id]);
    }

    // Set VCPU Thread priority
    auto vcpus_priority = cfg.vcpus_priority();
    if (id < vcpus_priority.size()) {
      vcpu->SetPriority(vcpus_priority[id]);
    }

    if (gpu_sched_raw != nullptr) {
      bool priority =
          machina::IsGpuSchedVcpuSelected(cfg.gpu_sched_vcpus(), id);

      if (cfg.gpu_sched_vcpus().size() == 0 && id == 4) {
        priority = true;
      }

      status = gpu_sched_raw->addVcpu(vcpu->object(), priority);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to add VCPU " << id << " to gpu scheduler";
        return status;
      }
    }

    // Begin VCPU execution.
    return vcpu->Start(&vcpu_state);
  };

  guest.RegisterVcpuFactory(initialize_vcpu);

  status = guest.StartVcpu(guest_ip, 0 /* id */);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start VCPU-0 " << status;
    loop.PostQuitTask();
  }

  // Vcpu-0 started and ready to receive VMCall from NBL_VMM
  VmMessageHandler handler(&guest, application_context.get());
  vhm_device.RegisterMessageListener(handler.NewBinding());

  loop.Run();

  interrupt_controller.Shutdown();
  int ret = guest.Join();
  shutdown_vhm_device.call();
  rtc.reset();
  eint.reset();
  i2c.reset();
  spi.reset();
  virtio_rpmb.reset();
  pv_blocks.clear();
  virtio_blocks.clear();
  inspect_svc.reset();
  pci_bus.reset();
  mmio_bus.reset();
  guest.ShutdownAsyncLoops();
  guest.TeeVmDestroy(guest.vmid());
  if (vmlog_sink_) {
    vmlog_sink_->Shutdown();
  }
  if (log_store_) {
    log_store_->Shutdown();
  }

  return ret;
}
