// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <libfdt.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#define GIC_FDT_IRQ_TYPE_SPI 0
#define GIC_FDT_IRQ_TYPE_PPI 1
#define GIC_FDT_IRQ_TYPE_ESPI 2

#define GIC_FDT_IRQ_FLAGS_EDGE_LO_HI 1
#define GIC_FDT_IRQ_FLAGS_EDGE_HI_LO 2
#define GIC_FDT_IRQ_FLAGS_LEVEL_HI 4
#define GIC_FDT_IRQ_FLAGS_LEVEL_LO 8

#define check_status(ret, property_name)                     \
  {                                                          \
    if ((ret) < 0) {                                         \
      FXL_LOG(ERROR) << "Device tree operation failed at \"" \
                     << (property_name) << "\", "            \
                     << "errot code is " << (ret);           \
      return ZX_ERR_BAD_STATE;                               \
    }                                                        \
  }

[[maybe_unused]] static int fdt_setprop_cells_u64(void* dtb,
                                                  int offset,
                                                  const char* prop_name,
                                                  int num_cells,
                                                  ...) {
  std::vector<uint32_t> cells;
  va_list args;
  va_start(args, num_cells);
  for (int i = 0; i < num_cells; ++i) {
    uint64_t value = va_arg(args, uint64_t);
    uint32_t hi = value >> 32;
    uint32_t low = value & 0xffffffff;
    cells.push_back(cpu_to_fdt32(hi));
    cells.push_back(cpu_to_fdt32(low));
  }
  va_end(args);
  return fdt_setprop(dtb, offset, prop_name, cells.data(),
                     sizeof(uint32_t) * cells.size());
}
[[maybe_unused]] static int fdt_getprop_cells_u64(void* dtb,
                                                  int offset,
                                                  const char* prop_name,
                                                  int num_cells,
                                                  ...) {
  int len;
  auto prop = reinterpret_cast<const uint32_t*>(
      fdt_getprop(dtb, offset, prop_name, &len));
  if ((size_t)len != num_cells * sizeof(uint64_t))
    return -FDT_ERR_BADSTATE;

  va_list args;
  va_start(args, num_cells);
  for (int i = 0; i < num_cells; i++) {
    uint64_t hi = fdt32_to_cpu(prop[i * 2]);
    uint64_t low = fdt32_to_cpu(prop[i * 2 + 1]);
    *va_arg(args, uint64_t*) = low | hi << 32;
  }
  va_end(args);
  return 0;
}

[[maybe_unused]] static int fdt_setprop_cells_u32(void* dtb,
                                                  int offset,
                                                  const char* prop_name,
                                                  int num_cells,
                                                  ...) {
  std::vector<uint32_t> cells;
  va_list args;
  va_start(args, num_cells);
  for (int i = 0; i < num_cells; ++i) {
    uint32_t value = va_arg(args, uint32_t);
    cells.push_back(cpu_to_fdt32(value));
  }
  va_end(args);
  return fdt_setprop(dtb, offset, prop_name, cells.data(),
                     sizeof(uint32_t) * cells.size());
}

[[maybe_unused]] static int fdt_getprop_cells_u32(void* dtb,
                                                  int offset,
                                                  const char* prop_name,
                                                  int num_cells,
                                                  ...) {
  int len;
  auto prop = reinterpret_cast<const uint32_t*>(
      fdt_getprop(dtb, offset, prop_name, &len));
  if ((size_t)len != num_cells * sizeof(uint32_t))
    return -FDT_ERR_BADSTATE;

  va_list args;
  va_start(args, num_cells);
  for (int i = 0; i < num_cells; i++) {
    uint32_t value = fdt32_to_cpu(prop[i]);
    *va_arg(args, uint32_t*) = value;
  }
  va_end(args);
  return 0;
}

// Generate a phandle that is not used by any node in the current FDT.
[[maybe_unused]] static int fdt_generate_unused_phandle(const void* fdt,
                                                 uint32_t* phandle) {
  uint32_t max_phandle = 0;
  int depth = 0;
  int offset = -1;

  if (fdt == nullptr || phandle == nullptr) {
    return -FDT_ERR_BADPHANDLE;
  }

  for (;;) {
    offset = fdt_next_node(fdt, offset, &depth);
    if (offset == -FDT_ERR_NOTFOUND) {
      break;
    }
    if (offset < 0) {
      return offset;
    }

    uint32_t current_phandle = fdt_get_phandle(fdt, offset);
    if (current_phandle != 0 && current_phandle != UINT32_MAX &&
        current_phandle > max_phandle) {
      max_phandle = current_phandle;
    }
  }

  if (max_phandle >= UINT32_MAX - 1) {
    return -FDT_ERR_BADPHANDLE;
  }

  *phandle = max_phandle + 1;
  return 0;
}

[[maybe_unused]] static int find_first_compatible_under_path(const void *fdt,
                                 const char *path,
                                 const char *compatible)
{
  int parent, off;

  parent = fdt_path_offset(fdt, path);
  if (parent < 0)
    return parent;

  for (off = fdt_first_subnode(fdt, parent); off >= 0; off = fdt_next_subnode(fdt, off)) {
    if (fdt_node_check_compatible(fdt, off, compatible) == 0)
      return off;
  }

  return -FDT_ERR_NOTFOUND;
}
