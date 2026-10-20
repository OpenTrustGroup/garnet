// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest/bootloader/linux.h"

#include "lib/fxl/logging.h"

static void device_tree_error_msg(const char* property_name) {
  FXL_LOG(ERROR) << "Failed to add \"" << property_name << "\" to device "
                 << "tree, space must be reserved in the device tree";
}

zx_status_t BootloaderLinux::Setup(GuestConfig& cfg,
                                   uintptr_t* guest_ip,
                                   std::vector<uint64_t>& extra_params) {
  auto dtb_image = find_loaded_image("kernel");
  FXL_CHECK(dtb_image != nullptr);

  auto initrd_image = find_loaded_image("rootfs");
  FXL_CHECK(initrd_image != nullptr);

  uint64_t initrd_start = initrd_image->phys_base;
  uint64_t initrd_end = initrd_image->phys_base + initrd_image->size;
  uint64_t dtb_size = 0x10000;
  uint64_t dtb_start = dtb_image->phys_base;
  uint64_t dtb_end = dtb_image->phys_base + dtb_size;
  FXL_LOG(INFO) << "DTB: " << std::hex << dtb_start << "-" << std::hex
                << dtb_end;
  FXL_LOG(INFO) << "Ramdisk: " << std::hex << initrd_start << "-" << std::hex
                << initrd_end;

  cfg.set_dtb(dtb_start, dtb_size);
  extra_params.push_back(dtb_start);
  *guest_ip = dtb_end;

  auto fdt = InspectMemory<void*>(dtb_image->phys_base, dtb_size);
  int ret = fdt_open_into(fdt, fdt, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int mem_off = fdt_path_offset(fdt, "/memory");
  if (mem_off < 0) {
    FXL_LOG(ERROR) << "Failed to find \"/memory\" in device tree";
    return ZX_ERR_BAD_STATE;
  }

  uint64_t mem_base = phys_mem_.phys_base();
  uint64_t mem_size = phys_mem_.size();
  ret = fdt_setprop_cells_u64(fdt, mem_off, "reg", 2, mem_base, mem_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to set \"reg\" of /memory in device tree";
    return ZX_ERR_BAD_STATE;
  }

  int off = fdt_path_offset(fdt, "/chosen");
  if (off < 0) {
    FXL_LOG(ERROR) << "Failed to find \"/chosen\" in device tree";
    return ZX_ERR_BAD_STATE;
  }
  ret = fdt_setprop_u64(fdt, off, "linux,initrd-start", initrd_start);
  if (ret < 0) {
    device_tree_error_msg("linux,initrd-start");
    return ZX_ERR_BAD_STATE;
  }
  ret = fdt_setprop_u64(fdt, off, "linux,initrd-end", initrd_end);
  if (ret < 0) {
    device_tree_error_msg("linux,initrd-end");
    return ZX_ERR_BAD_STATE;
  }
  if (!cfg.cmdline().empty()) {
    FXL_LOG(INFO) << "cmdline: " << cfg.cmdline();
    ret = fdt_setprop_string(fdt, off, "bootargs", cfg.cmdline().c_str());
    if (ret < 0) {
      device_tree_error_msg("bootargs");
      return ZX_ERR_BAD_STATE;
    }
  }

  fdt_pack(fdt);
  return ZX_OK;
}

void BootloaderLinux::DumpStateHandler(const nbl_vmm::GuestDumpState& state,
                                       uint32_t reason) {
  int id = 0;
  for (auto& vcpu_state : state.vcpus()) {
    fprintf(stderr, "vcpu%d:\n", id++);
    fprintf(stderr, "  pc: 0x%lx\n", vcpu_state.pc());
    fprintf(stderr, "  lr: 0x%lx\n", vcpu_state.lr());
    fprintf(stderr, "  sp_el1: 0x%lx\n", vcpu_state.sp_el1());
    fprintf(stderr, "  cpsr: 0x%x\n", vcpu_state.cpsr());
    fprintf(stderr, "  cntvoff_el2: 0x%lx\n", vcpu_state.cntvoff_el2());
    fprintf(stderr, "\n");
  }
}