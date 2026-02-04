// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <ios>
#include <vector>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/task.h>
#include <libfdt.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/device/sysinfo.h>
#include <threads.h>
#include <zircon/threads.h>
#include <trusty_std.h>
#include <uapi/err.h>
#include <pal/platform_defs.h>

// _trusty_ioctl deps
#include <trusty_std.h>
#include <trusty_syscalls.h>

#include "garnet/bin/guest/guest_config.h"
#include "garnet/bin/guest/tee-smc.h"
#include "garnet/lib/machina/fdt_utils.h"
#include "garnet/lib/machina/inspect_service_impl.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/ipc_service_impl.h"
#include "garnet/lib/machina/utrace.h"
#include "garnet/lib/machina/cam_dmabuf.h"
#include "garnet/lib/machina/pci.h"
#include "garnet/lib/machina/shared_irq_service_impl.h"
#include "garnet/lib/machina/audio_irq_service_impl.h"
#include "garnet/lib/machina/vcpu.h"
#include "garnet/lib/machina/remoteproc_manager.h"
#include "garnet/lib/machina/remoteproc.h"
#include "garnet/lib/machina/ipc_message.h"

#include "garnet/public/lib/fxl/files/file.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "garnet/bin/guest/top.h"
#include "garnet/bin/guest/rev.h"
#include "garnet/lib/machina/arch/arm64/smmu_v3.h"
#include <zircon/device/grt-wdt.h>


static constexpr char kRprocConfigFilePath[] = "/system/data/rproc.pb.txt";

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
static constexpr char kDefaultPath[] = "/system/data/";
static constexpr char kDefaultFile[] = "sos.json";

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

static zx_status_t read_guest_cfg(const char* cfg_path,
                                  int argc,
                                  char** argv,
                                  GuestConfig* cfg) {
  GuestConfigParser parser(cfg);
  std::string cfg_str;
  if (files::ReadFileToString(cfg_path, &cfg_str)) {
    zx_status_t status = parser.ParseConfig(cfg_str);
    if (status != ZX_OK) {
      return status;
    }
  } else {
    FXL_LOG(WARNING) << "Failed to read config: " << cfg_path;
  }

  return parser.ParseArgcArgv(argc, argv);
}

static zx_status_t read_product_cfg(const char* cfg_path,
                                    GuestConfig* cfg) {
  GuestConfigParser parser(cfg);
  std::string cfg_str;
  if (files::ReadFileToString(cfg_path, &cfg_str)) {
    zx_status_t status = parser.ParseProductConfig(cfg_str);
    if (status != ZX_OK) {
      return status;
    }
  } else {
    FXL_LOG(WARNING) << "Failed to read config: " << cfg_path;
    return ZX_ERR_NOT_FILE;
  }
  return ZX_OK;
}

static constexpr uint64_t kMegaBytes = 1ull << 20;
static constexpr uintptr_t kReservedGuestMemorySize = 1536 * kMegaBytes;
static constexpr int kMaxReservedMemorySeg = 70;
static constexpr uint32_t kMaxNameLen = 100;

static int get_reserve_memory(void* dtb, const char* suffix) {
  char path_name[kMaxNameLen];
  int i;
  int gz_off;

  for (i = 1; i < kMaxReservedMemorySeg; i++) {
    memset(path_name, 0, kMaxNameLen);
    snprintf(path_name, kMaxNameLen - 1, "/reserved-memory/mblock-%d-%s", i,
             suffix);
    gz_off = fdt_path_offset(dtb, path_name);
    if (gz_off > 0) {
      return gz_off;
    }
    snprintf(path_name, kMaxNameLen - 1, "/reserved-memory/mblock-%s", suffix);
    gz_off = fdt_path_offset(dtb, path_name);
    if (gz_off > 0) {
      return gz_off;
    }
    snprintf(path_name, kMaxNameLen - 1, "/reserved-memory/%s", suffix);
    gz_off = fdt_path_offset(dtb, path_name);
    if (gz_off > 0) {
      return gz_off;
    }
  }

  return -1;
}

static zx_status_t probe_guest_reserved_memory(void* dtb,
                                               uint64_t* base,
                                               uint64_t* size) {
  uint64_t rk1_base, rk1_size;
  zx_status_t status = ZX_ERR_BAD_STATE;

  int gz_guest_off1 = get_reserve_memory(dtb, "gz-guest-rk1");
  if (gz_guest_off1 > 0) {
    fdt_getprop_cells_u64(dtb, gz_guest_off1, "reg", 2, &rk1_base, &rk1_size);
  }

  uint64_t rk0_base, rk0_size;
  int gz_guest_off0 = get_reserve_memory(dtb, "gz-guest-rk0");
  if (gz_guest_off0 > 0) {
    fdt_getprop_cells_u64(dtb, gz_guest_off0, "reg", 2, &rk0_base, &rk0_size);
  }

  if ((gz_guest_off1 > 0) && (gz_guest_off0 > 0)) {
    FXL_CHECK(rk0_base + rk0_size == rk1_base)
        << "guest reserved memory is not contiguous";
    *base = rk0_base;
    *size = rk0_size + rk1_size;
  } else if (gz_guest_off1 > 0) {
    *base = rk1_base;
    *size = rk1_size;
  } else if (gz_guest_off0 > 0) {
    *base = rk0_base;
    *size = rk0_size;
  }

  if (gz_guest_off1 > 0) {
    fdt_del_node(dtb, gz_guest_off1);
    status = ZX_OK;
  }
  if (gz_guest_off0 > 0) {
    fdt_del_node(dtb, gz_guest_off0);
    status = ZX_OK;
  }

  return status;
}


void *get_dtb(const machina::Guest& guest, GuestConfig& cfg) {
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;

  return guest.phys_mem().as<void>(dtb_offset, dtb_size);
}

static bool file_exists(const char* filename) {
  struct stat statbuf;
  return stat(filename, &statbuf) == 0;
}

static void create_rproc_manager(
    component::ApplicationContext* context,
    machina::Guest& guest,
    GuestConfig& cfg,
    std::unique_ptr<machina::RprocManager>* out_mgr) {
  std::unique_ptr<machina::RprocManager> mgr;

  if (file_exists(kRprocConfigFilePath)) {
    mgr = machina::RprocManager::BuildFromFile(context, &guest,
                                               kRprocConfigFilePath);
  } else {
    FXL_LOG(INFO) << "Remoteproc config file not found, using default";
    mgr = machina::RprocManager::BuildWithDefaultConfig(context, &guest);
  }
  FXL_CHECK(mgr != nullptr);

  void* dtb = get_dtb(guest, cfg);
  mgr->AllocateSharedMemory([dtb](std::string& resv_mem) {
    int rproc_resv = get_reserve_memory(dtb, resv_mem.c_str());
    zx_status_t status;

    zx::vmo shm_vmo;
    if (rproc_resv > 0) {
      uint64_t shm_base, shm_size;
      fdt_getprop_cells_u64(dtb, rproc_resv, "reg", 2, &shm_base, &shm_size);

      zx::resource root_resource;
      status = get_root_resource(&root_resource);
      FXL_CHECK(status == ZX_OK);

      status = zx_vmo_create_physical(root_resource.get(), shm_base, shm_size,
                                      shm_vmo.reset_and_get_address());
      FXL_CHECK(status == ZX_OK);

      FXL_LOG(INFO) << "Remoteproc reserved memory: " << shm_size / 0x100000
                    << " MiB";
    } else {
      FXL_LOG(INFO) << "Remoteproc reserved memory not found";
      status = zx::vmo::create(/*size=*/0x200000, /*option=*/0, &shm_vmo);
      FXL_CHECK(status == ZX_OK);
    }

    status = shm_vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
    FXL_CHECK(status == ZX_OK);

    return shm_vmo;
  });

  *out_mgr = std::move(mgr);
}

static bool is_qemu_platform() {
    static uint32_t product_id;
    _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_PRODUCT_ID,&product_id);
    return product_id == BOARD_VID_QEMU;
}

static zx_status_t create_acrn_vhm_node(const machina::Guest& guest,
                                        GuestConfig& cfg) {
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;
  uint32_t cells_size = 3;

  // Validate device tree.
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  guest.GetGICInterruptCellSize(dtb, cells_size);

  int root_offset = fdt_path_offset(dtb, "/");
  check_status(root_offset, "root");

  int hsm_offset = fdt_add_subnode(dtb, root_offset, "acrn_hsm");
  check_status(hsm_offset, "acrn_hsm");

  ret = fdt_setprop_string(dtb, hsm_offset, "compatible", "nbl,acrn-hsm");
  check_status(ret, "compatible");

  uint32_t props[] = {cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI),
                      cpu_to_fdt32(cfg.vhm_irq() - 32),
                      cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI),
                      cpu_to_fdt32(0)};
  ret = fdt_setprop(dtb, hsm_offset, "interrupts", props,
                    cells_size * sizeof(uint32_t));
  check_status(ret, "interrupts");

  if (cfg.gpu_irq() > 0) {
    int irq_notify_offset = fdt_add_subnode(dtb, hsm_offset, "irq_notify_gpu");
    check_status(irq_notify_offset, "irq_notify");

    ret = fdt_setprop_string(dtb, irq_notify_offset, "compatible",
                             "acrn,irq_notify");
    check_status(ret, "compatible");

    // clang-format off
    uint32_t irq_notify[] = {cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI),
                            cpu_to_fdt32(cfg.gpu_irq() - 32),
                            cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI),
                            cpu_to_fdt32(0)};
    // clang-format on
    ret = fdt_setprop(dtb, irq_notify_offset, "interrupts", irq_notify,
                      cells_size * sizeof(uint32_t));
    check_status(ret, "interrupts");
  }

  auto vsock_irqs = cfg.vsock_irqs();
  if (!vsock_irqs.empty()) {
    int irq_notify_offset =
        fdt_add_subnode(dtb, hsm_offset, "irq_notify_vsock");
    check_status(irq_notify_offset, "irq_notify");

    ret = fdt_setprop_string(dtb, irq_notify_offset, "compatible",
                             "acrn,irq_notify");
    check_status(ret, "compatible");

    std::vector<uint32_t> props;
    for (auto& irq : vsock_irqs) {
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
      props.push_back(cpu_to_fdt32(irq - 32));
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
      if (!is_qemu_platform())
        props.push_back(0);
    }
    ret = fdt_setprop(dtb, irq_notify_offset, "interrupts", props.data(),
                      props.size() * sizeof(uint32_t));
    check_status(ret, "interrupts");
  }

  auto apu_irqs = cfg.apu_irqs();
  if (!apu_irqs.empty()) {
    int irq_notify_offset =
        fdt_add_subnode(dtb, hsm_offset, "irq_notify_apu");
    check_status(irq_notify_offset, "irq_notify");

    ret = fdt_setprop_string(dtb, irq_notify_offset, "compatible",
                        "acrn,irq_notify");
    check_status(ret, "compatible");

    std::vector<uint32_t> props;
    for (auto& irq : apu_irqs) {
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
      props.push_back(cpu_to_fdt32(irq - 32));
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
      if (!is_qemu_platform())
        props.push_back(0);
    }
    ret = fdt_setprop(dtb, irq_notify_offset, "interrupts", props.data(),
                props.size() * sizeof(uint32_t));
    check_status(ret, "interrupts");
  }

  auto cmdq_irqs = cfg.cmdq_irqs();
  if (!cmdq_irqs.empty()) {
    int irq_notify_offset =
        fdt_add_subnode(dtb, hsm_offset, "irq_notify_cmdq");
    check_status(irq_notify_offset, "irq_notify");

    ret = fdt_setprop_string(dtb, irq_notify_offset, "compatible",
                        "acrn,irq_notify");
    check_status(ret, "compatible");

    std::vector<uint32_t> props;
    for (auto& irq : cmdq_irqs) {
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
      props.push_back(cpu_to_fdt32(irq - 32));
      props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_LEVEL_HI));
      if (!is_qemu_platform())
        props.push_back(0);
    }
    ret = fdt_setprop(dtb, irq_notify_offset, "interrupts", props.data(),
                props.size() * sizeof(uint32_t));
    check_status(ret, "interrupts");
  }

  int reserved_mem_offset = fdt_path_offset(dtb, "/reserved-memory");
  check_status(ret, "reserved_mem_offset");

  int guest_mem_offset =
      fdt_add_subnode(dtb, reserved_mem_offset, "acrn_guest_memory");
  check_status(guest_mem_offset, "acrn_guest_memory");

  ret = fdt_setprop_string(dtb, guest_mem_offset, "compatible",
                           "nbl,guest-memory");
  check_status(ret, "compatible");

  ret = fdt_setprop(dtb, guest_mem_offset, "no_map", nullptr, 0);
  check_status(ret, "no-map");

  uint64_t base, size;
  if (probe_guest_reserved_memory(dtb, &base, &size) == ZX_OK) {
    ret = fdt_setprop_cells_u64(dtb, guest_mem_offset, "reg", 2, base, size);
    check_status(ret, "reg");
  } else {
    if (cfg.guest_reserved_memory() != 0) {
      ret = fdt_setprop_u64(dtb, guest_mem_offset, "size",
                            cfg.guest_reserved_memory() * kMegaBytes);
    } else {
      ret = fdt_setprop_u64(dtb, guest_mem_offset, "size",
                            kReservedGuestMemorySize);
    }

    check_status(ret, "size");
    ret = fdt_setprop_u64(dtb, guest_mem_offset, "alignment", 2 * kMegaBytes);
    check_status(ret, "alignment");

    uint64_t value[] = {cpu_to_fdt64(cfg.phys_base()),
                        cpu_to_fdt64(cfg.phys_size())};
    ret = fdt_setprop(dtb, guest_mem_offset, "alloc-ranges", value,
                      sizeof(value));
    check_status(ret, "alloc-ranges");
  }

  fdt_pack(dtb);

  return ZX_OK;
}

static const std::string get_product_path(const machina::Guest& guest,
                                        GuestConfig& cfg) {
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;
  int lenp = 0;

  // Validate device tree.
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return std::string(kDefaultPath) + std::string(kDefaultFile);
  }

  int root_offset = fdt_path_offset(dtb, "/");
  if (root_offset < 0) {
    FXL_LOG(ERROR) << "Didn't find root node";
    return std::string(kDefaultPath) + std::string(kDefaultFile);
  }

  const char* pstr = (const char *)fdt_getprop(dtb, root_offset, "product", &lenp);
  if (!lenp || !pstr) {
    return std::string(kDefaultPath) + std::string(kDefaultFile);
  }

  const std::vector<machina::ProductSpec>& spec = cfg.product_info();
  for(auto it : spec) {
    if (strcmp(it.name.c_str(), pstr) == 0) {
      return std::string(kDefaultPath) + it.file;
    }
  }

  return std::string(kDefaultPath) + std::string(kDefaultFile);
}

static zx_status_t add_pmu_irq_node(
    const machina::Guest& guest,
    GuestConfig& cfg) {
  int pmu_use = 0;
  auto irq = cfg.vgic().percpu_irqs;
  for (auto it = irq.begin(); it != irq.end(); ++it) {
        if(*it == 23)
        {
             pmu_use = 1;
             break;
        }
  }
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;
  // Validate device tree.
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);
  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  int pmu_blackhawk_offset = fdt_path_offset(dtb, "/pmu-blackhawk");
  if (pmu_blackhawk_offset < 0) {
    FXL_LOG(WARNING) << "pmu is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }
  if(pmu_use == 0)
    ret = fdt_setprop_string(dtb, pmu_blackhawk_offset, "status","disabled");
  else
    ret = fdt_setprop_string(dtb, pmu_blackhawk_offset, "status","okay");
  check_status(ret, "status");
  int pmu_hunter_elp_offset = fdt_path_offset(dtb, "/pmu-hunter-elp");
  if (pmu_hunter_elp_offset < 0) {
    FXL_LOG(WARNING) << "pmu_hunter_elp_offset is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }
  if(pmu_use == 0)
    ret = fdt_setprop_string(dtb, pmu_hunter_elp_offset, "status","disabled");
  else
    ret = fdt_setprop_string(dtb, pmu_hunter_elp_offset, "status","okay");
  int pmu_hunter_offset = fdt_path_offset(dtb, "/pmu-hunter");
  if (pmu_hunter_offset < 0) {
    FXL_LOG(WARNING) << "pmu_hunter_offset is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }
  if(pmu_use == 0)
    ret = fdt_setprop_string(dtb, pmu_hunter_offset, "status","disabled");
  else
    ret = fdt_setprop_string(dtb, pmu_hunter_offset, "status","okay");
  check_status(ret, "status");
  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t add_pmu76_irq_node(
    const machina::Guest& guest,
    GuestConfig& cfg) {
  int pmu_use = 0;
  auto irq = cfg.vgic().percpu_irqs;
  for (auto it = irq.begin(); it != irq.end(); ++it) {
        if(*it == 23)
        {
             pmu_use = 1;
             break;
        }
  }
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;
  // Validate device tree.
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);
  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  int pmu_blackhawk_offset = fdt_path_offset(dtb, "/pmu-a715");
  if (pmu_blackhawk_offset < 0) {
    FXL_LOG(WARNING) << "pmu is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }
  if(pmu_use == 0)
    ret = fdt_setprop_string(dtb, pmu_blackhawk_offset, "status","disabled");
  else
    ret = fdt_setprop_string(dtb, pmu_blackhawk_offset, "status","okay");
  check_status(ret, "status");
  int pmu_hunter_elp_offset = fdt_path_offset(dtb, "/pmu-a510");
  if (pmu_hunter_elp_offset < 0) {
    FXL_LOG(WARNING) << "pmu_hunter_elp_offset is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }
  if(pmu_use == 0)
    ret = fdt_setprop_string(dtb, pmu_hunter_elp_offset, "status","disabled");
  else
    ret = fdt_setprop_string(dtb, pmu_hunter_elp_offset, "status","okay");
  check_status(ret, "status");
  fdt_pack(dtb);
  return ZX_OK;
}

// TODO: read vmlog mblock table from sos.json config file
// format: {mblock_name, log_prefix}
static std::map<std::string, std::string> vmlog_mblocks = {
  {"vmlog0", "alps"},
  {"vmlog1", "tbox"},
};

static zx_status_t create_vmlog_nodes(const machina::Guest& guest, GuestConfig& cfg) {
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;
  std::string tmp_node_name;

  // Validate device tree.
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int nebula_node_offset = fdt_path_offset(dtb, "/nebula");
  check_status(nebula_node_offset, "nebula");

  for (auto m : vmlog_mblocks) {
    const char* name = m.first.c_str();
    const char* log_prefix = m.second.c_str();
    int tmp_node_offset = get_reserve_memory(dtb, name);
    if (tmp_node_offset < 0) {
      continue;
    }

    const char *node_name = fdt_get_name(dtb, tmp_node_offset, NULL);
    FXL_CHECK(node_name != NULL) << "failed to get reserved memory node name: " << name;

    // the memory content of the node_name point to might be change after
    // the device tree is modified, we should copy the node_name first.
    tmp_node_name.assign(node_name);

    int vmlog_node_offset = fdt_add_subnode(dtb, nebula_node_offset, name);
    check_status(vmlog_node_offset, name);

    ret = fdt_setprop_string(dtb, vmlog_node_offset, "compatible", "grt,vm-log-srv");
    check_status(ret, "compatible");

    ret = fdt_setprop_string(dtb, vmlog_node_offset, "log_prefix", log_prefix);
    check_status(ret, "log_prefix");

    ret = fdt_setprop_string(dtb, vmlog_node_offset, "reserved_mem", tmp_node_name.c_str());
    check_status(ret, "reserved_mem");

    FXL_LOG(INFO) << "created vmlog dts node: " << name;
  }

  fdt_pack(dtb);
  return ret;
}

static void add_vtee_irq_to_vgic(GuestConfig& cfg) {
  if (cfg.vtee_notifier_irq() == 0) {
    return;
  }

  auto &irq = cfg.vgic().irqs;
  for (auto i = 0; i < irq.size(); i++) {
    if (irq[i] == cfg.vtee_notifier_irq()) {
      return;
    }
  }

  irq.push_back(cfg.vtee_notifier_irq());
}

static zx_status_t create_notify_irq_node(
    const machina::Guest& guest,
    GuestConfig& cfg,
    const std::string& node,
    uint32_t irq) {
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

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
    FXL_LOG(WARNING) << "Nebula is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }

  uint32_t props[] = {cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI),
                      cpu_to_fdt32(irq - 32),
                      cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI),
                      cpu_to_fdt32(0)};

  ret = fdt_setprop(dtb, vtee_virtio_offset, "interrupts", props,
                    sizeof(props));
  check_status(ret, "interrupts");

  fdt_pack(dtb);
  return ZX_OK;
}

static zx_status_t delete_nebula_tee_node(
    const machina::Guest& guest,
    GuestConfig& cfg) {
  auto dtb_spec = cfg.dtb();
  uintptr_t dtb_offset = dtb_spec.base - cfg.phys_base();
  size_t dtb_size = dtb_spec.size;
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

  // Validate device tree.
  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int vtee_offset = fdt_path_offset(dtb, "/nebula-vtee");
  if (vtee_offset < 0) {
    goto out;
  }

  FXL_LOG(INFO) << "Nebula vTEE probe failed, remove nebula-vtee device node";
  fdt_del_node(dtb, vtee_offset);

out:
  fdt_pack(dtb);
  return ZX_OK;
}

[[maybe_unused]] static constexpr uint32_t CreateFastNebulaCoreFuncId(uint16_t func_num) {
  return tee_smc::CreateFunctionId(tee_smc::kFastCall, tee_smc::kSmc32CallConv,
                                   tee_smc::kNebulaCore, func_num);
}

static zx_status_t probe_vtee(const machina::Guest& guest, GuestConfig& cfg) {
  int status = ZX_OK;
  uint32_t tee_vendor = zx_system_get_tee_vendor_id();
  std::string tee_node;
  switch (tee_vendor) {
    case ZX_TEE_VENDOR_NEBULA:
      tee_node = "/nebula-vtee/virtio";
      status = create_notify_irq_node(guest, cfg, tee_node, cfg.vtee_notifier_irq());
      if (status != ZX_OK) {
        FXL_CHECK(status == ZX_OK) << "Failed to create nebula tee vqueue notify irq node " << status;
      } else {
        FXL_LOG(INFO) << "Nebula vTEE probe ok";
      }
      break;
    case ZX_TEE_VENDOR_TRUSTONIC:
      tee_node = "/mobicore";
      status = create_notify_irq_node(guest, cfg, tee_node, cfg.vtee_notifier_irq());
      if (status != ZX_OK) {
        FXL_CHECK(status == ZX_OK) << "Failed to create trustonic tee vqueue notify irq node " << status;
      } else {
        FXL_LOG(INFO) << "Trustonic vTEE probe ok";
      }
      break;
    default:
      break;
  }
  if (tee_vendor != ZX_TEE_VENDOR_NEBULA) {
    delete_nebula_tee_node(guest, cfg);
  }
  return status;
}

static void config_irq_monitor(machina::IrqMonitorSpec& spec) {
  //if (spec.irqs.size() == 0)
  //  return;

  struct irq_monitor_cfg cfg = {
    .enable = spec.enable,
    .irq_cnt_threshold = spec.threshold,
    .irqs = {0}
  };

  for (auto irq : spec.irqs) {
    if (irq < 0) {
      memset(cfg.irqs, 0xff, sizeof(cfg.irqs));
      break;
    }
    cfg.irqs[irq / 64] |= 1 << (irq & 63);
  }

  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_IRQ_MONITOR_CONFIG, (void*)&cfg);
}

class TipcVqueueNotifier {
 public:
  TipcVqueueNotifier(uint32_t irq) {
    zx_status_t status = zx_vqueue_notifier_create(irq, &notifier_);
    FXL_CHECK(status == ZX_OK) << "Failed to create vqueue notifier";
  }

  void BindVcpu(uint64_t id, machina::Vcpu* vcpu) {
    zx_status_t status =
        zx_vqueue_notifier_bind_vcpu(notifier_, id, vcpu->object());
    FXL_CHECK(status == ZX_OK) << "Failed to bind vcpu with vqueue notifier";
  }

 private:
  zx_handle_t notifier_;
};

#if 0
static int kick_nbl_mtk_wdt(void *wdt_fd) {
  if (!wdt_fd) {
    return ZX_ERR_INVALID_ARGS;
  }
  int *fd = static_cast<int*>(wdt_fd);

  if (*fd <= 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (;;) {
    zx_nanosleep(zx_deadline_after(ZX_SEC(15)));
    ioctl_grt_wdt_ping(*fd);
  }

  return ZX_OK;
}
#endif

static int nbl_loading_show_task_start(void* ctx) {
    return nbl_loading_show_task(ctx);
}

static int nbl_update_rev_task_start(void* ctx) {
    return nbl_update_rev_task(ctx);
}

int main(int argc, char** argv) {

  fsl::MessageLoop loop;
  std::unique_ptr<component::ApplicationContext> application_context =
      component::ApplicationContext::CreateFromStartupInfo();

  GuestConfig cfg;
  zx_status_t status  =
      read_product_cfg("/system/data/product.json", &cfg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read product config " << status;
    return status;
  }

  machina::Guest guest;
  guest.SetVmid((uint32_t)SOS_VMID);
  status = guest.Init(cfg.vmem());
  FXL_CHECK(status == ZX_OK) << "Failed to init guest " << status;
  if (status != ZX_OK) {
    return status;
  }
  guest.SetUserVmid(SOS_VMID);
  status = guest.TeeVmCreate(SOS_VMID);
  FXL_CHECK(status == ZX_OK) << "Tee vm create failed " << status;

  guest.CreateDtbSpec(cfg.dtb());
  const std::string& pfile = get_product_path(guest, cfg);
  status =
    read_guest_cfg(pfile.c_str(), argc, argv, &cfg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read sos config " << status;
    return status;
  }

  auto trace = machina::Utrace::GetInstance();
  FXL_CHECK(trace != nullptr);

  status = trace->TraceMemFromDtb(guest, cfg.phys_base());
  FXL_CHECK(status == ZX_OK);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "Trace: buffer not found! " << status;

  status = trace->MapTraceBuf(guest, true);
  FXL_CHECK(status == ZX_OK);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "Trace: Map trace buffer failed! " << status;

  status = trace->NotifyKernel();
  FXL_CHECK(status == ZX_OK);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "Trace: Failed to notify kernel! " << status;

  guest.SetTraceMem(trace->pa(), trace->size());
  auto camdmabuf = machina::CamDmabuf::GetInstance();
  FXL_CHECK(trace != nullptr);

  status = camdmabuf->CamDmabufMemFromDtb(guest, cfg.phys_base());
  //FXL_CHECK(status == ZX_OK);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "CamDmabuf: buffer not found! " << status;

  status = camdmabuf->MapCamDmabuf(guest, true);
  //FXL_CHECK(status == ZX_OK);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "CamDmabuf: Map  buffer failed! " << status;

  guest.SetCamDmabufMem(camdmabuf->pa(), camdmabuf->size());

  guest.SetSchedIRQ(cfg.sched_irq());
  guest.SetDumpIRQ(cfg.dump_irq());
  status = guest.SchedMemFromDtb(cfg.phys_base());
  if (status == ZX_OK) {
    guest.InitSched(cfg.phys_base(), true);
    guest.RegSchedIRQ();
  }

  guest.SetSchedId(cfg.sched_id());
  std::vector<uint32_t> &bind_pcpus = cfg.bind_pcpus();
  int bind_cpu_size = bind_pcpus.size();
  if (bind_cpu_size != 0) {
    FXL_CHECK(bind_cpu_size == cfg.num_cpus())
        << "cpu num mismtach:" << bind_cpu_size << "," << cfg.num_cpus();

    status = guest.SetCpuAffinity(bind_pcpus.data(), bind_cpu_size);
    FXL_CHECK(status == ZX_OK) << "failed to set cpu affinity:" << status;
  }

  guest.SetCpuNums(cfg.num_cpus());

  status = create_acrn_vhm_node(guest, cfg);
  FXL_CHECK(status == ZX_OK) << "Failed to create acrn vhm node" << status;
  if (status != ZX_OK) {
    return status;
  }

  std::string tipc_vqueue_node = "/nebula/virtio";
  status = create_notify_irq_node(guest, cfg, tipc_vqueue_node, cfg.tipc_vq_notifier_irq());
  FXL_CHECK(status == ZX_OK) << "Failed to create tipc vqueue notify irq node" << status;
  if (status != ZX_OK) {
    return status;
  }

  status = create_vmlog_nodes(guest, cfg);
  FXL_DCHECK(status == ZX_OK) << "Failed to add vmlog node: " << status;
  if (status != ZX_OK) {
    return status;
  }

  status = add_pmu_irq_node(guest, cfg);
  FXL_CHECK(status == ZX_OK) << "Failed to add_pmu_irq_node node" << status;
  if (status != ZX_OK) {
    return status;
  }

  status = add_pmu76_irq_node(guest, cfg);
  FXL_CHECK(status == ZX_OK) << "Failed to 76 add_pmu_irq_node node" << status;
  if (status != ZX_OK) {
    return status;
  }

  add_vtee_irq_to_vgic(cfg);

  status = probe_vtee(guest, cfg);
  FXL_CHECK(status == ZX_OK) << "Failed to probe vtee" << status;
  if (status != ZX_OK) {
    return status;
  }

  // Instantiate the inspect service.
  machina::InspectServiceImpl inspect_svc(application_context.get(),
                                          guest.phys_mem());

  const auto& vcpu = cfg.vcpu();
  // Setup kernel.
  uintptr_t guest_ip = vcpu.entry;
  uintptr_t boot_ptr = cfg.dtb().base;

  // Setup interrupt controller.
  machina::InterruptController interrupt_controller(&guest);
  interrupt_controller.SetSPIVcpuMask(0xFF);
  status =
      interrupt_controller.Init(cfg.num_cpus(), cfg.gic_version(), cfg.vgic());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create interrupt controller";
    return status;
  }


    FXL_LOG(INFO) << "create multi SOS vSmmu device";
    std::vector<std::unique_ptr<machina::Smmuv3>> vsmmus;

    auto smmu_specs = cfg.vsmmus();
    if(smmu_specs.empty()){
      FXL_LOG(INFO) << "Failed to initialize SOS vSmmu devices: no valid configurations provided.";
    }

    for(const auto& spec : smmu_specs) {
      auto smmu_type = machina::Smmuv3::GetSmmuType(spec.paddr);
      auto vsmmu = std::make_unique<machina::Smmuv3>(&guest,static_cast<uint8_t>(smmu_type));

      status = vsmmu->Init(spec);
      if(status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to create SOS vSmmu device";
        return status;
      }
      vsmmus.push_back(std::move(vsmmu));
    }

    if (vsmmus.empty())
      FXL_LOG(ERROR) << "All SOS vSmmu device initializations failed.";
    else
      FXL_LOG(INFO) << "Successfully created multi SOS vSmmu devices.";

  config_irq_monitor(cfg.irq_monitor());

  // Instantiate the vhm service.
  machina::VhmServiceImpl vhm_svc(application_context.get(), loop.async(),
                                  &interrupt_controller, cfg.vhm_irq(),
                                  guest.phys_mem(), cfg.phys_base(), &guest);
  guest.RegisterVhmRequestHandler(&vhm_svc);

  // Instantiate the shared irq service.
  machina::SharedIrqServiceImpl shared_irq_svc(application_context.get(),
                                               cfg.vgic().shared_irqs, &guest);
  guest.InitializeSharedIrqService(&shared_irq_svc);

  // Instantiate the audio irq service.
  std::vector<machina::AudioIrqSpecEx> audio_irqs;
  for(auto spec : cfg.vgic().audio_irqs){
    std::vector<machina::AudioIrqSpecEx>::iterator it = audio_irqs.begin();
    while (it != audio_irqs.end()){
      if (it->vector == spec.vector){
        break;
      }
      ++ it;
    }

    if (it == audio_irqs.end()){
      machina::AudioIrqSpecEx spec_ex;
      spec_ex.vector = spec.vector;
      spec_ex.ctrl_reg_base = spec.ctrl_reg_base;
      machina::AudioIrqRegSpec reg_spec;
      reg_spec.irq_status_offset = spec.irq_status_offset;
      reg_spec.irq_clear_offset = spec.irq_clear_offset;
      reg_spec.irq_regs_bit = spec.interested_bitmask;
      reg_spec.vmid = 0xffffffff;
      reg_spec.interested_bitmask = (0x1 << (0x000000000000001F & spec.interested_bitmask));
      spec_ex.regs.push_back(reg_spec);
      audio_irqs.push_back(spec_ex);
    }
    else {
      machina::AudioIrqRegSpec reg_spec;
      reg_spec.irq_status_offset = spec.irq_status_offset;
      reg_spec.irq_clear_offset = spec.irq_clear_offset;
      reg_spec.irq_regs_bit = spec.interested_bitmask;
      reg_spec.vmid = 0xffffffff;
      reg_spec.interested_bitmask = (0x1 << (0x000000000000001F & spec.interested_bitmask));
      it->regs.push_back(reg_spec);
    }
  }

  machina::AudioIrqServiceImpl audio_irq_svc(application_context.get(),
                                               audio_irqs, &guest);
  guest.InitializeAudioIrqService(&audio_irq_svc);

  machina::IpcServiceImpl ipc_mbox_svc(application_context.get(), &guest,
					                   cfg.ipc_mbox());
  guest.RegisterIpcRequestHandler(&ipc_mbox_svc);

  for (size_t i = 0; i < cfg.ipc_mbox().size(); ++i) {
    status = ipc_mbox_svc.getIpchandlerMap()[i]->CreateMboxDeviceTreeNodes(
                                             cfg.dtb(), cfg.phys_base(), i);
    FXL_CHECK(status == ZX_OK);
    //ipc_mbox_svc.getIpchandlerMap()[i]->set_trace_mem(
    //                           trace->pa(), trace->size());
    ipc_mbox_svc.getIpchandlerMap()[i]->set_sched_mem(
                               guest.sched_phys_base(), guest.sched_size());
  }

  machina::IpcMessage IpcMessSer(application_context.get(), cfg);
  IpcMessSer.set_trace_mem(trace->pa(), trace->size());
  IpcMessSer.SetCamDmabufMem(camdmabuf->pa(), camdmabuf->size());

  std::unique_ptr<machina::RprocManager> mgr;
  create_rproc_manager(application_context.get(), guest, cfg, &mgr);

  RprocClient rproc_client(machina::kSosVmid, mgr.get(), guest,
                           interrupt_controller);
  rproc_client.PatchDeviceTree(cfg.dtb(), cfg.phys_base());

  // Config each vcpu status(RT, spinlock, IRQ) priority.
  uint32_t vcpu_status = 0;
  auto sched_priority = cfg.sched_priority();
  for (auto priority : sched_priority) {
    struct sched_param_req req;
    req.type = SCHED_PARAM_TYPE_PRIORITY;
    req.priority_info.vcpu_status = vcpu_status++;
    req.priority_info.priority = priority;
    _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM, (void*) &req);
  }

  // Config each vcpu status(RT, spinlock, IRQ) time slice.
  vcpu_status = 0;
  auto sched_timeslice = cfg.sched_timeslice();
  for (auto timeslice : sched_timeslice) {
    struct sched_param_req req;
    req.type = SCHED_PARAM_TYPE_TIMESLICE;
    req.timeslice_info.vcpu_status = vcpu_status++;
    req.timeslice_info.timeslice = timeslice;
    _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM, (void*) &req);
  }

  // Config each vcpu group scheduler period.
  uint32_t group = 0;
  auto periods = cfg.periods();
  for (auto period : periods) {
    struct sched_param_req req;
    req.type = SCHED_PARAM_TYPE_PERIOD;
    req.period_info.group = group++;
    req.period_info.period = period;
    _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_SCHED_PARAM, (void*) &req);
  }

  TipcVqueueNotifier vqueue_notifier(cfg.tipc_vq_notifier_irq());
  auto initialize_vcpu = [&cfg, boot_ptr, &interrupt_controller, &vqueue_notifier,
      &bind_pcpus](machina::Guest *guest, uintptr_t guest_ip, uint64_t id,
      machina::Vcpu *vcpu) {
    zx_status_t status = vcpu->Create(guest, guest_ip, id, kSosVcpuPriority,
        kSosVcpuTimeSlice);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create VCPU";
      return status;
    }

    vqueue_notifier.BindVcpu(id, vcpu);

    // Register VCPU with ID 0.
    status = interrupt_controller.RegisterVcpu(id, vcpu);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to register VCPU with interrupt controller";
      return status;
    }

    // percpu interrupts should be bound in vcpu thread
    vcpu->RegisterStartCallback([&cfg, vcpu, id, &interrupt_controller] {
      auto& redistributors = interrupt_controller.redistributors();
      zx_status_t status = redistributors[id]->Init(cfg.vgic().percpu_irqs);
      FXL_CHECK(status == ZX_OK);
      redistributors[id]->BindVcpu(vcpu);
    });

    // Setup initial VCPU state.
    zx_vcpu_state_t vcpu_state = {};
    vcpu_state.x[0] = boot_ptr;

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

  FXL_LOG(INFO) << "Disable output logs to UART";
  if (_trusty_ioctl(NEBULA_GENERIC_FD,
                    SYS_PLATFORM_UART_ACTION_PRINT_LOG_OFF_CMD,
                    nullptr) < 0) {
      FXL_LOG(WARNING) << "platform do not support disable uart action, skip";
  }

  auto args = new top_task_args_t{(machina::Utrace *)&trace};
  thrd_t thread;
  int ret = thrd_create_with_name(&thread, nbl_loading_show_task_start, args,
                                  "nbl_loading_show");
  if (ret != thrd_success)
    FXL_LOG(ERROR) << "Failed to create nbl_loading_show thread " << ret;

  ret = thrd_detach(thread);
  if (ret != thrd_success)
    FXL_LOG(ERROR) << "Failed to detach balloon thread " << ret;

  auto args_rev = new rev_task_args_t{(machina::Utrace *)&trace};
  thrd_t thread_rev;
  ret = thrd_create_with_name(&thread_rev, nbl_update_rev_task_start, args_rev,
                                  "nbl_update_rev");
  if (ret != thrd_success)
    FXL_LOG(ERROR) << "Failed to create nbl_update_rev thread " << ret;

  ret = thrd_detach(thread_rev);
  if (ret != thrd_success)
    FXL_LOG(ERROR) << "Failed to detach balloon thread nbl_update_rev " << ret;

  // top thread start end
  guest.create_wdt_fd();

  // nebula kirnel wdt-k thread will kick dog.
#if 0
  thrd_t thd_wdt;
  guest.create_wdt_fd();
  int fd = guest.get_wdt_fd();
  ret = thrd_create_with_name(&thd_wdt, kick_nbl_mtk_wdt, static_cast<void*>(&fd),
                                  "kick_nbl_mtk_wdt");
  if (ret != thrd_success)
    FXL_LOG(ERROR) << "Failed to create kick_nbl_mtk_wdt thread " << ret;

  ret = thrd_detach(thd_wdt);
  if (ret != thrd_success)
    FXL_LOG(ERROR) << "Failed to detach kick_nbl_mtk_wdt thread " << ret;
#endif
  // kick-nbl-mtk-wdt thread start end
  zx_thread_set_priority(kLooperPriority);
  loop.Run();

  // sos don't kill, this code can't go through.
  guest.TeeVmDestroy(SOS_VMID);

  return guest.Join();
}
