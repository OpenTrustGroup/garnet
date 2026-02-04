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
#include <sys/types.h>
#include <unistd.h>
#include <ios>
#include <vector>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/task.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/threads.h>
#include <pal/platform_defs.h>

#include "garnet/bin/guest/guest_config.h"
#include "garnet/bin/guest/linux.h"
#include "garnet/bin/guest/message_handler.h"
#include "garnet/bin/guest/zircon.h"
#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/utrace.h"
#include "garnet/lib/machina/cam_dmabuf.h"
#include "garnet/lib/machina/inspect_service_impl.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/ipc_client.h"
#include "garnet/lib/machina/pci.h"
#include "garnet/lib/machina/uart.h"
#include "garnet/lib/machina/vcpu.h"
#include "garnet/lib/machina/vhm_device.h"
#include "garnet/lib/machina/shared_irq_client.h"
#include "garnet/lib/machina/audio_irq_client.h"
#include "garnet/lib/machina/fdt_utils.h"
#include "garnet/public/lib/fxl/files/file.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "trace-provider/provider.h"

#include "garnet/lib/machina/arch/arm64/pl031.h"
#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/remoteproc.h"
#include "garnet/lib/machina/remoteproc_vm_monitor.h"
#include "garnet/lib/machina/ipc_message.h"
#include "garnet/lib/machina/arch/arm64/smmu_v3.h"
#include "garnet/bin/guest/proto/vm_config.pb.h"
#include "third_party/rapidjson/rapidjson/document.h"

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <trusty_std.h>
#include <nbl_fwk/nmt_tee.h>

#define LK2IMG_SIZE_OFFSET 20

//0:UOS image integrity will be verified even without license.
//1:UOS image verification will only be performed when the license has been activated
#define SECURE_BOOT_WITH_LICENSE 0

#include "garnet/lib/machina/arch/arm64/gic_its.h"

static constexpr char kVerifyKeyPath[] = "/system/data/uos_lk2_sign_key.pub";
static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
static constexpr size_t kNumUarts = 1;
static constexpr uint64_t kUartBases[kNumUarts] = {
    // TODO(abdulla): Considering parsing this from the MDI.
    machina::kPl011PhysBase,
};

namespace {
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
    };
    vmems.push_back(vmem);
  }

  return vmems;
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

static std::vector<machina::VsmmuSpec> vsmmu_build_from(nbl_vmm::VmConfig& cfg) {
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

}  // namespace

zx_status_t update_cmdline(nbl_vmm::VmConfig& cfg, void* dtb, size_t dtb_size) {
  auto cmdline = cfg.cmdline();
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

  int length = 0;
  const void* property = fdt_getprop(dtb, offset, "bootargs", &length);
  if (property) {
    cmdline += " ";
    cmdline += std::string((const char *)property, length);
  }

  FXL_LOG(INFO) << "new cmdline is " << cmdline.data();

  ret = fdt_setprop(dtb, offset, "bootargs", cmdline.data(),
                    cmdline.length());
  if (ret < 0) {
    FXL_LOG(INFO) << "cannot modify bootargs";
    fdt_pack(dtb);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  fdt_pack(dtb);
  return ZX_OK;
}

static bool is_qemu_platform() {
    static uint32_t vid;
    _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_VENDOR_ID,&vid);
    return vid == BOARD_VID_QEMU;
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

  ret = fdt_setprop(dtb, irq_offset, "interrupts", props.data(), props.size() * sizeof(uint32_t));
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

  ret =
      fdt_setprop_string(dtb, irq_offset, "compatible", "nbl,virtio_apu_irq");
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

static int update_nodes(void *src_dtb,
                         void *dest_dtb,
                         std::vector<std::string> &nodes) {
  int src_root = fdt_path_offset(src_dtb, "/");
  if (src_root < 0) {
    FXL_LOG(INFO) << "Failed to get src_dtb root";
  }

  int dest_root = fdt_path_offset(dest_dtb, "/");
  if (dest_root < 0) {
    FXL_LOG(INFO) << "Failed to get dest_dtb root";
  }

  for (std::string &node : nodes) {
    int src_node = fdt_node_offset_by_compatible(src_dtb, 0, node.c_str());
    if (src_node < 0) {
      FXL_LOG(INFO) << "Failed to get src_dtb root";
    }

    int dest_node = fdt_node_offset_by_compatible(dest_dtb, 0, node.c_str());
    if (dest_node == -FDT_ERR_NOTFOUND) {
      int new_node = fdt_add_subnode(dest_dtb, dest_root, fdt_get_name(src_dtb, src_node, NULL));
      if (new_node < 0) {
        FXL_LOG(INFO) << "Failed to add node in dest DTB";
        return new_node;
      }

      int src_prop = fdt_first_property_offset(src_dtb, src_node);
      while(src_prop > 0) {
        const char *prop_name;
        int len = 0;

        const void* prop_value = fdt_getprop_by_offset(src_dtb, src_prop, &prop_name, &len);
        if (prop_name == NULL || len == 0) {
          FXL_LOG(INFO) << "Not found any prop in node";
        }

        if (strcmp(prop_name, "phandle") != 0) {
          int result = fdt_setprop(dest_dtb, new_node, prop_name, prop_value, len);
          if (result < 0) {
            FXL_LOG(INFO) << "Failed to set node properties";
            return result;
          }
        }

        src_prop = fdt_next_property_offset(src_dtb, src_prop);
      }
    } else if (dest_node >= 0) {
      int src_prop = fdt_first_property_offset(src_dtb, src_node);
      while(src_prop > 0) {
        const char *prop_name;
        int len = 0;

        const void* prop_value = fdt_getprop_by_offset(src_dtb, src_prop, &prop_name, &len);
        if (prop_name == NULL || len == 0) {
          FXL_LOG(INFO) << "Not found any prop in node";
        }

        if (strcmp(prop_name, "compatible") == 0) {
          break;
        }

        int result = fdt_setprop(dest_dtb, dest_node, prop_name, prop_value, len);
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

static zx_status_t get_sos_dtb_addr(machina::DeviceTreeSpec& dtb) {
  std::string cfg_str;
  if (files::ReadFileToString("/system/data/product.json", &cfg_str)) {
    rapidjson::Document doc;
    doc.Parse(cfg_str);
    if (!doc.IsObject()) {
      return ZX_ERR_INVALID_ARGS;
    }

    if (doc.HasMember("device-tree") && doc["device-tree"].IsString()) {
      const std::string dtbStr = doc["device-tree"].GetString();
	    int ret = sscanf(dtbStr.c_str(), "%lx,%lx", &dtb.base, &dtb.size);
	    if (ret != 2) {
		    FXL_LOG(ERROR) << "Invalid device-tree string: " << doc["device-tree"].GetString();
		    return ZX_ERR_INVALID_ARGS;
	    }
    }

    return ZX_OK;
  } else {
    FXL_LOG(WARNING) << "Failed to read product config.";
    return ZX_ERR_NOT_FILE;
  }
}

static void copy_sos_dtb_node(zx_handle_t handle, void* dtb, size_t dtb_size) {
  zx::vmo vmo;
  machina::DeviceTreeSpec dtb_spec;
  zx::resource root_resource;
  std::vector<std::string> append_nodes = {"mediatek,mddriver", "mediatek,md_attr_node"};

  zx_status_t status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource " << status;
    return;
  }

  status = get_sos_dtb_addr(dtb_spec);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get SOS dtb info.";
  }

  FXL_LOG(ERROR) << "get sos dtb base=" << dtb_spec.base <<" size=" << dtb_spec.size;
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
  status = zx_vmar_map(handle, map_flags, 0, vmo.get(),
                       0, size, &mapped_addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map trace physical memory " << status;
    return;
  }
  FXL_CHECK(vmo);

  void *sos_dtb = reinterpret_cast<void*>(mapped_addr);

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

static void add_vtee_irq_to_vgic(nbl_vmm::VmConfig& cfg, machina::VgicSpec& vgic) {
  if (cfg.vtee_notifier_irq() == 0) {
    return;
  }

  auto &irq = vgic.irqs;
  for (auto i = 0; i < irq.size(); i++) {
    if (irq[i] == cfg.vtee_notifier_irq()) {
      return;
    }
  }

  irq.push_back(cfg.vtee_notifier_irq());
}


static zx_status_t create_notify_irq_node(
    nbl_vmm::VmConfig& cfg,
    void* dtb, size_t dtb_size,
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

  int trustonic_tee_offset = fdt_path_offset(dtb, node.c_str());
  if (trustonic_tee_offset < 0) {
    FXL_LOG(WARNING) << "Trustonic vtee is not supported, skipped";
    fdt_pack(dtb);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  uint32_t props[] = {cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI),
                      cpu_to_fdt32(cfg.vtee_notifier_irq() - 32),
                      cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI),
                      cpu_to_fdt32(0)};
  ret = fdt_setprop(dtb, trustonic_tee_offset, "interrupts", props,
                    sizeof(props));
  check_status(ret, "interrupts");

  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t probe_vtee(nbl_vmm::VmConfig& cfg, void* dtb, size_t dtb_size) {
  int status = ZX_OK;
  uint32_t tee_vendor = zx_system_get_tee_vendor_id();
  std::string tee_node;
  switch (tee_vendor) {
    case ZX_TEE_VENDOR_NEBULA:
      tee_node = "/nebula-vtee/virtio";
      status = create_notify_irq_node(cfg, dtb, dtb_size, tee_node, cfg.vtee_notifier_irq());
      if (status != ZX_OK) {
        FXL_CHECK(status == ZX_OK) << "Failed to create nebula tee vqueue notify irq node " << status;
      } else {
        FXL_LOG(INFO) << "Nebula vTEE probe ok";
      }
      break;
    case ZX_TEE_VENDOR_TRUSTONIC:
      tee_node = "/mobicore";
      status = create_notify_irq_node(cfg, dtb, dtb_size, tee_node, cfg.vtee_notifier_irq());
      if (status != ZX_OK) {
        FXL_CHECK(status == ZX_OK) << "Failed to create trustonic irq node " << status;
      } else {
        FXL_LOG(INFO) << "Trustonic vTEE probe ok";
      }
      break;
    default:
      break;
  }
  return status;
}

static zx_status_t load_image_verify_key(std::string& pub_key_str) {
  struct stat stat;
  int status = ZX_OK;

  int fd = open(kVerifyKeyPath, O_RDONLY);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open key file: " << kVerifyKeyPath;
    return ZX_ERR_IO;
  }

  int ret = fstat(fd, &stat);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to get key file status: " << kVerifyKeyPath;
    status = ZX_ERR_IO;
    goto out;
  }

  if (stat.st_size <= 0) {
    FXL_LOG(ERROR) << "Invalid key file size: " << kVerifyKeyPath;
    status = ZX_ERR_IO_DATA_INTEGRITY;
    goto out;
  }

  pub_key_str.resize(stat.st_size + 1);
  ret = read(fd, (char *)pub_key_str.data(), stat.st_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to read key file: " << kVerifyKeyPath;
    status = ZX_ERR_IO;
  }

out:
  close(fd);
  return status;
}

static zx_status_t sec_boot_uos_lk2(uintptr_t guest_ip, machina::Guest& guest){

  uint32_t vid;
  uint32_t sec_boot;
  std::string pub_key;
  zx_status_t status = ZX_OK;

  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_VENDOR_ID, &vid);
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_SEC_BOOT, &sec_boot);
  FXL_LOG(INFO) << "vendor_id:" << vid << "   vmid:" << guest.vmid()<<"   sec_boot:" << sec_boot;

#if SECURE_BOOT_WITH_LICENSE == 1
  int rc = nbl_act_status(NMT_ACT_PRODUCT_BASIC);
  if (rc != 0) {
    FXL_LOG(WARNING) << "SECURE BOOT DISABLED due to License not activated";
    return ZX_OK;
  }
#endif

  if (sec_boot == 0) {
    FXL_LOG(INFO) << "SECURE BOOT DISABLED";
    return ZX_OK;
  }

  switch (vid) {
  case BOARD_VID_QEMU:
    FXL_LOG(INFO) << "QEMU does not support secure boot";
    break;
  case BOARD_VID_MTK: {
    status = load_image_verify_key(pub_key);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to load image verify key";
      return status;
    }

    uintptr_t image_addr_start_pre = (uintptr_t)(guest_ip - 0x2000);

    status = guest.Init_Sec_Boot(image_addr_start_pre);
    if(status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map sec_boot mem";
      return status;
    }

    zx_vaddr_t image_addr_start = guest.va_sec_boot + 0xd00;
    zx_vaddr_t addr_lk2img_size = image_addr_start + LK2IMG_SIZE_OFFSET;
    char* str_size = reinterpret_cast<char*>(addr_lk2img_size);
    /* deal string */
    size_t size_Value = std::stoi(str_size);
    // get signature
    zx_vaddr_t addr_signature = image_addr_start + (zx_vaddr_t)size_Value;
    unsigned char* str_signature = reinterpret_cast<unsigned char*>(addr_signature);

    OpenSSL_add_all_algorithms();
    ERR_load_BIO_strings();
    ERR_load_crypto_strings();
    BIO* bio = BIO_new(BIO_s_mem());
    BIO_puts(bio, pub_key.c_str());
    EVP_PKEY* pubKey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pubKey);
    void* file_addr_start = reinterpret_cast<void*>(image_addr_start);
    EVP_DigestVerifyUpdate(ctx, file_addr_start, size_Value);
    size_t signature_len = 256 ;  //signature len
    int result = EVP_DigestVerifyFinal(ctx, str_signature, signature_len);
    EVP_MD_CTX_free(ctx);
    BIO_free_all(bio);
    EVP_PKEY_free(pubKey);
    status = guest.Release_Init_Sec_Boot();
    if(status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to unmap sec_boot mem";
    }
    if(result != 1) {
      FXL_LOG(ERROR) << "loading bl-an.img sec boot fail";
      return ZX_ERR_IO;
    }
    FXL_LOG(INFO) << "loading bl-an.img sec boot success";
    break;
  }
  default:
    FXL_LOG(INFO) << "Unsupported vendor_id:" << vid;
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
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

  //Setup multi SMMUv3
  std::vector<std::unique_ptr<machina::Smmuv3>> vsmmus;
  auto smmu_specs = vsmmu_build_from(cfg);
  if(smmu_specs.empty()){
    FXL_LOG(INFO) << "Failed to initialize UOS vSmmu devices: no valid configurations provided.";
  }
  for(const auto& spec : smmu_specs) {
    auto smmu_type = machina::Smmuv3::GetSmmuType(spec.paddr);
    auto vsmmu = std::make_unique<machina::Smmuv3>(&guest, static_cast<uint8_t>(smmu_type));

    status = vsmmu->Init(spec);
    if(status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create UOS vSmmu device";
      return status;
    }
    vsmmus.push_back(std::move(vsmmu));
  }

  if (vsmmus.empty())
    FXL_LOG(ERROR) << "All UOS vSmmu device initializations failed.";
  else
    FXL_LOG(INFO) << "Successfully created multi UOS vSmmu devices.";

  guest.SetSchedIRQ(cfg.sched_irq());
  guest.SetDumpIRQ(cfg.dump_irq());

  uintptr_t guest_ip = cfg.entry();

  status = sec_boot_uos_lk2(guest_ip ,guest);
  FXL_CHECK(status == ZX_OK) << "failed to init guest:" << status;
  if (status != ZX_OK){
    return status;
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
  std::vector<uint32_t> wakeup_irqs;
  auto wakeup_irq_num = cfg.wakeup_irqs_size();
  for (int i = 0; i < wakeup_irq_num; i++) {
    wakeup_irqs.push_back(cfg.wakeup_irqs(i));
  }
  
  guest.SetWakeupIrqs(wakeup_irqs.data(), wakeup_irq_num);

  // Instantiate the inspect service.
  machina::InspectServiceImpl inspect_svc(application_context.get(),
                                          guest.phys_mem());

  // Setup UARTs.
  machina::Uart uart[kNumUarts];
  for (size_t i = 0; i < kNumUarts; i++) {
    status = uart[i].Init(&guest, kUartBases[i]);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create UART at " << std::hex
                     << kUartBases[i];
      return status;
    }
  }

  // Setup interrupt controller.
  machina::InterruptController interrupt_controller(&guest);
  machina::GicIts gic_its(&guest, &interrupt_controller);
  if (cfg.has_vgic()) {
    auto vgic = vgic_build_from(cfg);
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

  status = vhm_device.Init(&guest, &interrupt_controller, &gic_its);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize vhm device";
    return status;
  }

  auto shared_irq_client = machina::SharedIrqClient::Instance();
  component::ConnectToEnvironmentService(shared_irq_client->NewRequest());
  shared_irq_client->Init(&guest);

  machina::AudioIrqClient audio_irq_client;
  component::ConnectToEnvironmentService(audio_irq_client.NewRequest());
  audio_irq_client.Init(&guest);
  guest.InitializeAudioIrqClient(&audio_irq_client);

  machina::IpcClient ipc_mbox_client(&guest);
  component::ConnectToEnvironmentService(ipc_mbox_client.NewRequest());
  ipc_mbox_client.Init(vmid);
  guest.RegisterIpcRequestHandler(&ipc_mbox_client);

  machina::IpcMessageServiceSyncPtr IpcMessClient;
  component::ConnectToEnvironmentService(IpcMessClient.NewRequest());
  zx_vaddr_t tmp_mem_pa_;
  size_t tmp_mem_sz_;
  IpcMessClient->GetTraceBuf(&tmp_mem_pa_, &tmp_mem_sz_);
  guest.SetTraceMem( tmp_mem_pa_,  tmp_mem_sz_) ;

  IpcMessClient->GetCamDmaBuf(&tmp_mem_pa_, &tmp_mem_sz_);
  guest.SetCamDmabufMem( tmp_mem_pa_,  tmp_mem_sz_) ;

  machina::RprocServiceSyncPtr rproc_svc;
  component::ConnectToEnvironmentService(rproc_svc.NewRequest());
  RprocClient rproc_client(vmid, std::move(rproc_svc), guest, interrupt_controller);

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
  // In the case fdt blob is directly loaded from SOS filesystem, we can
  // patch fdt directly. (extra_params[0] is assumed to be GPA of fdt blob)
  if (!cfg.extra_params().empty()) {
    auto dtb_base = cfg.extra_params()[0];
    auto phys_base = get_phys_base(cfg);
    uint64_t dtb_size = 2 * PAGE_SIZE;
    machina::DeviceTreeSpec dtb = { dtb_base, dtb_size };
    rproc_client.PatchDeviceTree(dtb, phys_base);

    uintptr_t dtb_offset = dtb.base - phys_base;
    void* dtb_ptr = guest.phys_mem().as<void>(dtb_offset, dtb_size);
    update_virtio_gpu_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_vsock_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_apu_irq(cfg, dtb_ptr, dtb_size);
    update_virtio_cmdq_irq(cfg, dtb_ptr, dtb_size);
    probe_vtee(cfg, dtb_ptr, dtb_size);
  }

  guest.RegisterDeviceTreePatcher(
      [&cfg, &ipc_mbox_client, &guest, &rproc_client](
          uint64_t dtb_base, uint64_t dtb_size) {
        machina::DeviceTreeSpec dtb{dtb_base, dtb_size};
        auto phys_base = get_phys_base(cfg);
        auto status = ipc_mbox_client.CreateMboxDeviceTreeNodes(dtb, phys_base, 0);
        FXL_CHECK(status == ZX_OK);

        guest.CreateDtbSpec(dtb);

        auto trace = machina::Utrace::GetInstance();
        FXL_CHECK(trace != nullptr);
        trace->SetTraceMem(guest.trace_pa(), guest.trace_size());
        status = trace->MapTraceBuf(guest, false);
        FXL_CHECK(status == ZX_OK);
        if (status != ZX_OK)
          FXL_LOG(ERROR) << "Failed to map trace buffer!";
        trace->PatchTraceDts(guest, phys_base);

        auto camdmabuf = machina::CamDmabuf::GetInstance();
        FXL_CHECK(camdmabuf != nullptr);
        if(guest.camdmabuf_pa()&&guest.camdmabuf_size()){
          camdmabuf->SetCamDmabuf(guest.camdmabuf_pa(), guest.camdmabuf_size());
          status = camdmabuf->MapCamDmabuf(guest, false);
          FXL_CHECK(status == ZX_OK);
          if (status != ZX_OK)
            FXL_LOG(ERROR) << "Failed to map camdmabuf buffer!";
          camdmabuf->PatchCamDmabufDts(guest, phys_base);
        }else{
          FXL_LOG(INFO) << "No SetCamDmabufMem!";
        }
        guest.InitSched(phys_base, false);
        guest.RegSchedIRQ();

        status = guest.AddVmemDeviceTreeNodes(dtb, phys_base);
        FXL_CHECK(status == ZX_OK);

        uintptr_t dtb_offset = dtb.base - phys_base;
        void* dtb_ptr = guest.phys_mem().as<void>(dtb_offset, dtb_size);
        status = update_cmdline(cfg, dtb_ptr, dtb_size);
        FXL_CHECK(status == ZX_OK);

        update_virtio_gpu_irq(cfg, dtb_ptr, dtb_size);
        update_virtio_vsock_irq(cfg, dtb_ptr, dtb_size);
        update_virtio_apu_irq(cfg, dtb_ptr, dtb_size);
        update_virtio_cmdq_irq(cfg, dtb_ptr, dtb_size);

        rproc_client.PatchDeviceTree(dtb, phys_base);
        if (guest.vmid() == 1) {
          copy_sos_dtb_node(zx_vmar_root_self(), dtb_ptr, dtb_size);
        }
        probe_vtee(cfg, dtb_ptr, dtb_size);
      });

  auto initialize_vcpu = [&cfg, &interrupt_controller, &bind_pcpus](
                             machina::Guest* guest, uintptr_t guest_ip,
                             uint64_t id, machina::Vcpu* vcpu) {
    zx_status_t status = vcpu->Create(guest, guest_ip, id, kUosVcpuPriority,
        kUosVcpuTimeSlice);
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

    // Config VCPU budget
    auto budgets = cfg.budgets();
    if (id < budgets.size()) {
      vcpu->SetBudget(budgets[id]);
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

  async::Loop trace_looper;
  trace::TraceProvider provider(trace_looper.async());
  status = trace_looper.StartThread();
  FXL_CHECK(status == ZX_OK);

  // Vcpu-0 started and ready to receive VMCall from NBL_VMM
  VmMessageHandler handler(&guest, application_context.get());
  vhm_device.RegisterMessageListener(handler.NewBinding());

  loop.Run();

  return guest.Join();
}
