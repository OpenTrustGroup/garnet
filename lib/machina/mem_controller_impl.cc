// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mem_controller_impl.h"

#include <lib/fxl/logging.h>
#include <stdio.h>
#include <sys/types.h>

#include <cstring>

#include "garnet/lib/machina/fdt_utils.h"

namespace machina {

static constexpr uint64_t kMemControllerMmioBase = 0xd10000000ULL;
static constexpr uint32_t kNumaNode0 = 0;
static constexpr uint32_t kNumaNode1 = 1;
static constexpr uint32_t kNumaLocalDistance = 10;
static constexpr uint32_t kNumaRemoteDistance = 20;



// The CMA range is selected by the Lua configuration.
static constexpr uint64_t kBlockSize = 2 * 1024 * 1024;
[[maybe_unused]] static constexpr uint64_t kDefaultMemoryBlockSize =
    128 * 1024 * 1024;

#ifndef MEM_CONTROLLER_USE_NUMA
#define MEM_CONTROLLER_USE_NUMA 1
#endif

// Disabled until vhost_mem can assign a usable GPA for this CMA region.
#ifndef ENABLE_DYNAMIC_CMA_FALLBACK
#define ENABLE_DYNAMIC_CMA_FALLBACK 0
#endif

// Used only when ENABLE_DYNAMIC_CMA_FALLBACK is enabled.
#ifndef DYNAMIC_CMA_FALLBACK_SIZE
#define DYNAMIC_CMA_FALLBACK_SIZE (1ULL * 1024 * 1024 * 1024)
#endif

static constexpr uint64_t kMaxCmdBufSize = PAGE_SIZE;

static constexpr uint32_t kDeviceId = 0x1000;

#if MEM_CONTROLLER_USE_NUMA
struct FdtMemoryRange {
  uint64_t base;
  uint64_t size;
};

// This code only supports 64-bit address and size values in "reg".
static int validate_reg_cell_counts(const void* fdt, int node_offset) {
  int parent_offset = fdt_parent_offset(fdt, node_offset);
  if (parent_offset < 0) {
    return parent_offset;
  }

  // The "reg" cell counts are defined by the parent node.
  int address_cells = fdt_address_cells(fdt, parent_offset);
  if (address_cells < 0) {
    return address_cells;
  }

  int size_cells = fdt_size_cells(fdt, parent_offset);
  if (size_cells < 0) {
    return size_cells;
  }

  if (address_cells != 2 || size_cells != 2) {
    return -FDT_ERR_BADNCELLS;
  }

  return 0;
}

static bool get_range_end(uint64_t base, uint64_t size, uint64_t* end) {
  if (!size || base > UINT64_MAX - size) {
    return false;
  }

  *end = base + size;
  return true;
}

// MT8668 FDT root and reserved-memory nodes use two address and size cells.
static int get_memory_ranges(void* fdt,
                             int offset,
                             std::vector<FdtMemoryRange>* ranges) {
  int ret = validate_reg_cell_counts(fdt, offset);
  if (ret < 0) {
    FXL_LOG(ERROR)
        << "reg parent must have #address-cells=2 and #size-cells=2";
    return ret;
  }

  int len;
  const fdt32_t* cells =
      static_cast<const fdt32_t*>(fdt_getprop(fdt, offset, "reg", &len));

  if (!cells || len <= 0 || len % (4 * sizeof(*cells))) {
    return -FDT_ERR_BADSTRUCTURE;
  }

  ranges->clear();
  for (int i = 0; i < len / static_cast<int>(sizeof(*cells)); i += 4) {
    FdtMemoryRange range = {
        (static_cast<uint64_t>(fdt32_to_cpu(cells[i])) << 32) |
            fdt32_to_cpu(cells[i + 1]),
        (static_cast<uint64_t>(fdt32_to_cpu(cells[i + 2])) << 32) |
            fdt32_to_cpu(cells[i + 3]),
    };
    uint64_t end;
    if (!get_range_end(range.base, range.size, &end)) {
      return -FDT_ERR_BADSTRUCTURE;
    }
    ranges->push_back(range);
  }

  return 0;
}

static int set_memory_ranges(void* fdt,
                             int offset,
                             const std::vector<FdtMemoryRange>& ranges) {
  std::vector<fdt32_t> cells;
  int ret = validate_reg_cell_counts(fdt, offset);
  if (ret < 0) {
    FXL_LOG(ERROR)
        << "reg parent must have #address-cells=2 and #size-cells=2";
    return ret;
  }

  if (ranges.empty()) {
    return -FDT_ERR_BADSTRUCTURE;
  }

  cells.reserve(ranges.size() * 4);
  for (const auto& range : ranges) {
    cells.push_back(cpu_to_fdt32(static_cast<uint32_t>(range.base >> 32)));
    cells.push_back(cpu_to_fdt32(static_cast<uint32_t>(range.base)));
    cells.push_back(cpu_to_fdt32(static_cast<uint32_t>(range.size >> 32)));
    cells.push_back(cpu_to_fdt32(static_cast<uint32_t>(range.size)));
  }

  return fdt_setprop(fdt, offset, "reg", cells.data(),
                     cells.size() * sizeof(cells[0]));
}

static bool is_memory_node(const void* fdt, int offset) {
  int len;
  const char* device_type =
      static_cast<const char*>(fdt_getprop(fdt, offset, "device_type", &len));

  return device_type && len == sizeof("memory") &&
         !std::memcmp(device_type, "memory", sizeof("memory"));
}

static int split_node0_memory(void* fdt,
                              uint64_t node1_base,
                              uint64_t node1_size) {
  uint64_t node1_end;
  int offset;

  if (!get_range_end(node1_base, node1_size, &node1_end)) {
    return -FDT_ERR_BADSTRUCTURE;
  }

  for (offset = fdt_first_subnode(fdt, 0); offset >= 0;
       offset = fdt_next_subnode(fdt, offset)) {
    std::vector<FdtMemoryRange> original_ranges;
    std::vector<FdtMemoryRange> node0_ranges;
    bool found = false;
    int ret;

    if (!is_memory_node(fdt, offset)) {
      continue;
    }

    ret = get_memory_ranges(fdt, offset, &original_ranges);
    if (ret < 0) {
      return ret;
    }

    for (const auto& range : original_ranges) {
      uint64_t range_end;

      if (!get_range_end(range.base, range.size, &range_end)) {
        return -FDT_ERR_BADSTRUCTURE;
      }

      if (node1_base >= range.base && node1_end <= range_end) {
        if (found) {
          return -FDT_ERR_BADSTRUCTURE;
        }

        if (node1_base > range.base) {
          node0_ranges.push_back({range.base, node1_base - range.base});
        }
        if (node1_end < range_end) {
          node0_ranges.push_back({node1_end, range_end - node1_end});
        }
        found = true;
        continue;
      }

      if (node1_base < range_end && node1_end > range.base) {
        FXL_LOG(ERROR) << "vhost memory range overlaps multiple memory nodes";
        return -FDT_ERR_BADSTRUCTURE;
      }

      node0_ranges.push_back(range);
    }

    if (!found) {
      continue;
    }

    ret = set_memory_ranges(fdt, offset, node0_ranges);
    if (ret < 0) {
      return ret;
    }

    return fdt_setprop_cell(fdt, offset, "numa-node-id", kNumaNode0);
  }

  return -FDT_ERR_NOTFOUND;
}

static int add_node1_memory(void* fdt, uint64_t base, uint64_t size, uint32_t phandle) {
  char node_name[32];
  const std::vector<FdtMemoryRange> ranges = {{base, size}};
  int ret;
  int offset;

  if (snprintf(node_name, sizeof(node_name), "memory@%llx",
               static_cast<unsigned long long>(base)) >=
      static_cast<int>(sizeof(node_name))) {
    return -FDT_ERR_BADSTRUCTURE;
  }

  offset = fdt_subnode_offset(fdt, 0, node_name);
  if (offset >= 0) {
    FXL_LOG(ERROR) << "NUMA node1 memory node already exists: " << node_name;
    return -FDT_ERR_EXISTS;
  }
  if (offset != -FDT_ERR_NOTFOUND) {
    return offset;
  }

  offset = fdt_add_subnode(fdt, 0, node_name);
  if (offset < 0) {
    return offset;
  }

  ret = fdt_setprop_string(fdt, offset, "device_type", "memory");
  if (ret < 0) {
    return ret;
  }

  ret = set_memory_ranges(fdt, offset, ranges);
  if (ret < 0) {
    return ret;
  }

  ret = fdt_setprop_cell(fdt, offset, "numa-node-id", kNumaNode1);
  if (ret < 0) {
    return ret;
  }

  return fdt_setprop_cell(fdt, offset, "phandle", phandle);
}

static int add_numa_distance_map(void* fdt) {
  const fdt32_t matrix[] = {
      cpu_to_fdt32(kNumaNode0), cpu_to_fdt32(kNumaNode0), cpu_to_fdt32(kNumaLocalDistance),
      cpu_to_fdt32(kNumaNode0), cpu_to_fdt32(kNumaNode1), cpu_to_fdt32(kNumaRemoteDistance),
      cpu_to_fdt32(kNumaNode1), cpu_to_fdt32(kNumaNode0), cpu_to_fdt32(kNumaRemoteDistance),
      cpu_to_fdt32(kNumaNode1), cpu_to_fdt32(kNumaNode1), cpu_to_fdt32(kNumaLocalDistance),
  };
  int offset = fdt_subnode_offset(fdt, 0, "distance-map");
  int ret;

  if (offset == -FDT_ERR_NOTFOUND) {
    offset = fdt_add_subnode(fdt, 0, "distance-map");
  }
  if (offset < 0) {
    return offset;
  }

  ret = fdt_setprop_string(fdt, offset, "compatible", "numa-distance-map-v1");
  if (ret < 0) {
    return ret;
  }

  return fdt_setprop(fdt, offset, "distance-matrix", matrix, sizeof(matrix));
}

static int assign_node1_to_vhost_mem(void* fdt, const char* name, uint32_t phandle) {
  std::string path_str = std::string("/") + name;
  int offset = fdt_path_offset(fdt, path_str.c_str());
  int ret;

  if (offset < 0) {
    return offset;
  }

  ret = fdt_setprop_cell(fdt, offset, "memory-region", phandle);
  if (ret < 0) {
    FXL_LOG(ERROR) << "failed to set memory-region property for " << path_str;
    return ret;
  }
  return 0;
}
#endif  // MEM_CONTROLLER_USE_NUMA

static int find_its_node_priority(void* fdt) {
  int offset;

  // real machine
  const char* path1 = "/interrupt-controller/its@910000000";
  offset = fdt_path_offset(fdt, path1);
  if (offset >= 0) {
    return offset;
  }

  // QEMU
  const char* path2 = "/intc@8000000/its@8080000";
  offset = fdt_path_offset(fdt, path2);
  if (offset >= 0) {
    return offset;
  }

  FXL_LOG(ERROR) << "Could not find ITS node in device tree";

  return -FDT_ERR_NOTFOUND;
}

static int add_msi_map(void* fdt, const char* name, uint32_t target_dev_id) {
  int its_offset, dev_offset;
  uint32_t its_phandle;
  uint32_t map_data[2];
  int err;

  std::string path_str = std::string("/") + name;
  its_offset = find_its_node_priority(fdt);
  if (its_offset < 0) {
    FXL_LOG(ERROR) << "Could not find ITS node in device tree";
    return its_offset;
  }

  its_phandle = fdt_get_phandle(fdt, its_offset);
  if (its_phandle == 0) {
    FXL_LOG(ERROR) << "ITS node has no phandle property";
    return -FDT_ERR_BADPHANDLE;
  }

  dev_offset = fdt_path_offset(fdt, path_str.c_str());
  if (dev_offset < 0) {
    FXL_LOG(ERROR) << "Could not find device node " << path_str;
    return dev_offset;
  }

  map_data[0] = cpu_to_fdt32(its_phandle);
  map_data[1] = cpu_to_fdt32(target_dev_id);
  err = fdt_setprop(fdt, dev_offset, "msi-parent", map_data, sizeof(map_data));
  if (err < 0) {
    FXL_LOG(ERROR) << "Error setting msi-parent: " << fdt_strerror(err);
    return err;
  }

  return 0;
}

#if MEM_CONTROLLER_USE_NUMA
static int convert_cma_to_node1_memory(void* fdt,
                                       const std::string& cma_compatible,
                                       const char* name) {
  std::vector<FdtMemoryRange> cma_ranges;
  uint64_t base;
  uint64_t size;
  int cma_offset;
  int ret;
  uint32_t new_phandle = 0;

  if (cma_compatible.empty()) {
    FXL_LOG(ERROR) << "mem-controller requires a reserved-memory compatible";
    return -FDT_ERR_NOTFOUND;
  }

  ret = fdt_generate_unused_phandle(fdt, &new_phandle);
  if (ret < 0) {
    FXL_LOG(ERROR) << "new phandle failed, ret: " << ret;
    return ret;
  }

  cma_offset = find_first_compatible_under_path(fdt, "/reserved-memory",
                                                cma_compatible.c_str());
  if (cma_offset < 0) {
    FXL_LOG(ERROR) << "reserved memory compatible " << cma_compatible
                   << " not found";
    return cma_offset;
  }

  ret = get_memory_ranges(fdt, cma_offset, &cma_ranges);
  if (ret < 0 || cma_ranges.size() != 1) {
    FXL_LOG(ERROR) << "vhost reserved memory must contain one valid reg range";
    return ret < 0 ? ret : -FDT_ERR_BADSTRUCTURE;
  }

  base = cma_ranges[0].base;
  size = cma_ranges[0].size;

  // Delete before adding node1 so Linux does not reserve the range as CMA.
  ret = fdt_del_node(fdt, cma_offset);
  if (ret < 0) {
    return ret;
  }

  ret = split_node0_memory(fdt, base, size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "failed to split node0 memory for vhost range";
    return ret;
  }

  ret = add_node1_memory(fdt, base, size, new_phandle);
  if (ret < 0) {
    FXL_LOG(ERROR) << "failed to add node1 memory";
    return ret;
  }

  ret = add_numa_distance_map(fdt);
  if (ret < 0) {
    FXL_LOG(ERROR) << "failed to add NUMA distance map";
    return ret;
  }

  ret = assign_node1_to_vhost_mem(fdt, name, new_phandle);
  if (ret < 0) {
    FXL_LOG(ERROR) << "failed to assign node1 memory to vhost_mem";
    return ret;
  }

  FXL_LOG(INFO) << "converted vhost memory 0x" << std::hex << base << "-0x"
                << base + size << " to NUMA node1";
  return 0;
}

#else /* !MEM_CONTROLLER_USE_NUMA */
static int add_cma_and_assign_to_vhost_mem(void* fdt,
                                           const std::string& cma_compatible,
                                           const char* name) {
  int rmem_offset, cma_offset, vhost_offset;
  int ret;
  uint32_t cma_phandle;

  rmem_offset = fdt_path_offset(fdt, "/reserved-memory");
  if (rmem_offset < 0) {
    FXL_LOG(ERROR) << "/reserved-memory path not found";
    return rmem_offset;
  }

  std::string rmem_name = std::string(name) + "_cma_region";

  if (cma_compatible.empty()) {
#if ENABLE_DYNAMIC_CMA_FALLBACK
    FXL_LOG(INFO) << "creating dynamic CMA node";
    cma_offset = fdt_add_subnode(fdt, rmem_offset, rmem_name.c_str());
    if (cma_offset < 0) {
      FXL_LOG(ERROR) << "failed to add cma reserved memory node";
      return cma_offset;
    }

    uint64_t size_be = cpu_to_fdt64(DYNAMIC_CMA_FALLBACK_SIZE);
    ret = fdt_setprop(fdt, cma_offset, "size", &size_be, sizeof(size_be));
    if (ret < 0) {
      FXL_LOG(ERROR) << "failed to set size property for cma node";
      return ret;
    }
#else
    FXL_LOG(ERROR) << "cma_compatible is empty and dynamic CMA fallback is "
                   << "disabled (ENABLE_DYNAMIC_CMA_FALLBACK=0)";
    return -FDT_ERR_NOTFOUND;
#endif
  } else {
    cma_offset = find_first_compatible_under_path(
        fdt, "/reserved-memory", cma_compatible.c_str());
    if (cma_offset < 0) {
      FXL_LOG(ERROR) << "CMA node compatible " << cma_compatible
                     << " not found";
      return cma_offset;
    }

    /* change node name */
    ret = fdt_set_name(fdt, cma_offset, rmem_name.c_str());
    if (ret < 0) {
      FXL_LOG(ERROR) << "failed to change node name to " << rmem_name;
      return ret;
    }

    /* delete no-map if present */
    ret = fdt_delprop(fdt, cma_offset, "no-map");
    if (ret < 0 && ret != -FDT_ERR_NOTFOUND) {
      FXL_LOG(ERROR) << "failed to delete no-map";
      return ret;
    }
  }

    ret = fdt_generate_unused_phandle(fdt, &cma_phandle);
    if (ret < 0) {
      FXL_LOG(ERROR) << "new phandle failed, ret: " << ret;
      return ret;
    }

    ret = fdt_setprop_cell(fdt, cma_offset, "phandle", cma_phandle);
    if (ret < 0) {
      FXL_LOG(ERROR) << "failed to set phandle property for cma node";
      return ret;
    }

    ret = fdt_setprop_string(fdt, cma_offset, "compatible", "shared-dma-pool");
    if (ret < 0) {
      FXL_LOG(ERROR) << "failed to set compatible property for cma node";
      return ret;
    }

    ret = fdt_setprop(fdt, cma_offset, "reusable", NULL, 0);
    if (ret < 0) {
      FXL_LOG(ERROR) << "failed to set reusable property for cma node";
      return ret;
    }

    uint64_t align_be = cpu_to_fdt64(kDefaultMemoryBlockSize);
    ret = fdt_setprop(fdt, cma_offset, "alignment", &align_be, sizeof(align_be));
    if (ret < 0) {
      FXL_LOG(ERROR) << "failed to set alignment property for cma node";
      return ret;
    }

    std::string path_str = std::string("/") + name;
    vhost_offset = fdt_path_offset(fdt, path_str.c_str());
    if (vhost_offset < 0) {
      FXL_LOG(ERROR) << "failed to find " << path_str;
      return vhost_offset;
    }

    ret = fdt_setprop_cell(fdt, vhost_offset, "memory-region", cma_phandle);
    if (ret < 0) {
      FXL_LOG(ERROR) << "failed to set memory-region property for " << path_str;
      return ret;
    }

  return 0;
}

#endif /* MEM_CONTROLLER_USE_NUMA end */

MemControllerImpl::MemControllerImpl(
    component::ServiceProviderBridge* bridge,
    GuestConfig& cfg,
    Guest* guest,
    GicIts* gic_its)
    : guest_(guest),
      gic_its_(gic_its),
      mmio_base_(kMemControllerMmioBase),
      region_start_(0),
      cma_compatible_(cfg.mem_controller_cma_compatible()),
      uos_gpa_region_start_(0),
      uos_gpa_region_size_(0),
      uos_gpa_region_ready_(false),
      cmd_buf_(nullptr),
      cmd_buf_size_(0) {
  auto status = guest_->CreateMapping(TrapType::MMIO_SYNC, mmio_base_, PAGE_SIZE,
                                      0, this);
  FXL_CHECK(status == ZX_OK);

  status = guest_->CreateMapping(TrapType::MMIO_BELL, mmio_base_ + PAGE_SIZE,
                                 PAGE_SIZE, PAGE_SIZE, this);
  FXL_CHECK(status == ZX_OK);

  bridge->AddService<MemController>(
      [this](fidl::InterfaceRequest<MemController> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

zx_status_t MemControllerImpl::Read(uint64_t addr, IoValue* value) const {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MemControllerImpl::Write(uint64_t addr, const IoValue& value) {
  switch (addr) {
    case Regs::REGION_START:
      region_start_ = value.u64;
      break;

    case Regs::CMD_BUF_SIZE:
      if (value.u64 < sizeof(plug_cmd_t) || value.u64 > kMaxCmdBufSize) {
        cmd_buf_size_ = 0;
        cmd_buf_ = nullptr;
        FXL_LOG(ERROR) << "Invalid command buffer size: " << value.u64;
        break;
      }
      cmd_buf_size_ = value.u64;
      cmd_buf_ = nullptr;
      break;

    case Regs::CMD_BUF: {
      auto& phys_mem = guest_->phys_mem();
      // A synchronous MMIO write has no controller error response. Leave the
      // command buffer disabled while allowing the guest trap to complete.
      if (cmd_buf_size_ == 0) {
        cmd_buf_ = nullptr;
        FXL_LOG(ERROR) << "CMD_BUF_SIZE must be configured before CMD_BUF";
        break;
      }
      if (!phys_mem.contains_gpa(value.u64, cmd_buf_size_)) {
        cmd_buf_ = nullptr;
        FXL_LOG(ERROR) << "Invalid command buffer GPA: 0x" << std::hex
                       << value.u64 << ", size: 0x" << cmd_buf_size_;
        break;
      }
      auto off = value.u64 - phys_mem.phys_base();
      cmd_buf_ = phys_mem.as<plug_cmd_t>(off);
      break;
    }

    case Regs::UOS_GPA_REGION_START:
      uos_gpa_region_start_ = value.u64;
      break;

    case Regs::UOS_GPA_REGION_SIZE:
      uos_gpa_region_size_ = value.u64;
      break;

    case Regs::UOS_GPA_REGION_READY:
      uos_gpa_region_ready_ = value.u64 != 0;
      if (uos_gpa_region_ready_) {
        NotifyUosGpaRegion();
      } else if (listener_.is_bound()) {
        listener_.Bind(zx::channel());
      }
      break;

    case Regs::PLUG:
      SendPlugMemoryRequest();
      gic_its_->InjectMsi(kDeviceId, 0);
      break;

    case Regs::UNPLUG:
      SendUnPlugMemoryRequest();
      gic_its_->InjectMsi(kDeviceId, 0);
      break;

    default:
      return ZX_ERR_IO;
  }

  return ZX_OK;
}

void MemControllerImpl::NotifyUosGpaRegion() {
  if (!uos_gpa_region_ready_ || !listener_.is_bound()) {
    return;
  }

  if (uos_gpa_region_start_ % kBlockSize != 0 ||
      uos_gpa_region_size_ == 0 || uos_gpa_region_size_ % kBlockSize != 0) {
    FXL_LOG(ERROR) << "Invalid UOS GPA region: start=0x" << std::hex
                   << uos_gpa_region_start_ << ", size=0x"
                   << uos_gpa_region_size_;
    return;
  }

  zx_status_t res = ZX_ERR_INTERNAL;
  bool success = listener_->OnSetGpaRegion(
      uos_gpa_region_start_, uos_gpa_region_size_, &res);
  if (!success || res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to reserve UOS GPA region: " << res;
  }
}

void MemControllerImpl::SendPlugMemoryRequest() {
  if (cmd_buf_ == nullptr) {
    FXL_LOG(ERROR) << "CMD_BUF not initialized before PLUG operation";
    return;
  }

  auto cmd = cmd_buf_;
  const size_t max_block_count =
      (cmd_buf_size_ - sizeof(*cmd)) / sizeof(cmd->phys_addrs[0]);
  if (cmd->count == 0 || cmd->count > max_block_count) {
    FXL_LOG(ERROR) << "Invalid plug block count: " << cmd->count;
    cmd->status = PLUG_STATUS_ERROR_INVALID_COUNT;
    return;
  }

  fidl::VectorPtr<uint64_t> phys_addrs;
  for (uint32_t i = 0; i < cmd->count; ++i) {
    phys_addrs.push_back(cmd->phys_addrs[i]);
  }
  if (!listener_.is_bound()) {
    FXL_LOG(ERROR) << "listener_ is not bound";
    cmd->status = PLUG_STATUS_ERROR_LISTENER;
    return;
  }

  zx_status_t res = ZX_ERR_INTERNAL;
  bool success = listener_->OnPlug(cmd->gpa_base, std::move(phys_addrs), &res);
  if (!success) {
    FXL_LOG(ERROR) << "Failed to connect client channel";
  }
  if (!success || res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to plug memory blocks: " << res;
    cmd->status = PLUG_STATUS_ERROR_OPERATION;
    return;
  }

  cmd->status = PLUG_STATUS_SUCCESS;
}

void MemControllerImpl::SendUnPlugMemoryRequest() {
  if (cmd_buf_ == nullptr) {
    FXL_LOG(ERROR) << "CMD_BUF not initialized before UnPlug operation";
    return;
  }

  auto cmd = cmd_buf_;
  // UNPLUG is a contiguous GPA range described by gpa_base and count. It does
  // not consume phys_addrs, so it is not limited by the command buffer's
  // address capacity.
  if (cmd->count == 0) {
    FXL_LOG(ERROR) << "Invalid unplug block count: " << cmd->count;
    cmd->status = PLUG_STATUS_ERROR_INVALID_COUNT;
    return;
  }

  zx_status_t res = ZX_ERR_INTERNAL;
  if (!listener_.is_bound()) {
    FXL_LOG(ERROR) << "listener_ is not bound";
    cmd->status = PLUG_STATUS_ERROR_LISTENER;
    return;
  }
  bool success =
      listener_->OnUnPlug(cmd->gpa_base, cmd->count * kBlockSize, &res);
  if (!success || res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to unplug memory blocks: " << res;
    cmd->status = PLUG_STATUS_ERROR_OPERATION;
    return;
  }

  cmd->status = PLUG_STATUS_SUCCESS;
}

zx_status_t MemControllerImpl::PatchDeviceTree(const DeviceTreeSpec& dtb_spec,
                                               uintptr_t phys_base) {
  uintptr_t dtb_offset = dtb_spec.base - phys_base;
  size_t dtb_size = dtb_spec.size;

  void* dtb_orig = guest_->phys_mem().as<void>(dtb_offset, dtb_size);

  int totalsize = fdt_totalsize(dtb_orig);
  if (totalsize <= 0 || static_cast<size_t>(totalsize) > dtb_size) {
    FXL_LOG(ERROR) << "Invalid device tree size: " << totalsize;
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  size_t working_size = totalsize * 2;
  std::vector<uint8_t> working_buf(working_size);

  memcpy(working_buf.data(), dtb_orig, totalsize);

  int ret = fdt_open_into(working_buf.data(), working_buf.data(), working_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "fdt_open_into failed: " << fdt_strerror(ret);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  const char name[] = "mem_controller0";
  const char compatible[] = "grt,mem-controller";

  int rproc_offset = fdt_add_subnode(working_buf.data(), 0, name);
  if (rproc_offset < 0) {
    FXL_LOG(ERROR) << "Failed to add subnode: " << fdt_strerror(rproc_offset);
    return ZX_ERR_INTERNAL;
  }

  ret = fdt_setprop_cells_u64(working_buf.data(), rproc_offset, "reg", 2,
                               mmio_base_, PAGE_SIZE * 2);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to set reg property: " << fdt_strerror(ret);
    return ZX_ERR_INTERNAL;
  }

  ret = fdt_setprop_string(working_buf.data(), rproc_offset, "compatible", compatible);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to set compatible property: " << fdt_strerror(ret);
    return ZX_ERR_INTERNAL;
  }
#if MEM_CONTROLLER_USE_NUMA
  ret = convert_cma_to_node1_memory(working_buf.data(), cma_compatible_, name);
#else
  ret = add_cma_and_assign_to_vhost_mem(
      working_buf.data(), cma_compatible_, name);
#endif
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to configure mem-controller memory: "
                   << fdt_strerror(ret);
    return ZX_ERR_INTERNAL;
  }

  ret = add_msi_map(working_buf.data(), name, kDeviceId);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to add MSI map: " << fdt_strerror(ret);
    return ZX_ERR_INTERNAL;
  }

  ret = fdt_pack(working_buf.data());
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to pack device tree: " << fdt_strerror(ret);
    return ZX_ERR_INTERNAL;
  }

  int new_totalsize = fdt_totalsize(working_buf.data());
  if (new_totalsize <= 0 || static_cast<size_t>(new_totalsize) > dtb_size) {
    FXL_LOG(ERROR) << "Modified device tree too large: " << new_totalsize
                   << " > " << dtb_size;
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(dtb_orig, working_buf.data(), new_totalsize);

  return ZX_OK;
}

void MemControllerImpl::RegisterMemControllerListener(
    fidl::InterfaceHandle<MemControllerListener> listener,
    RegisterMemControllerListenerCallback callback) {

  listener_.Bind(std::move(listener));

  NotifyUosGpaRegion();

  callback();
}

}  // namespace machina
