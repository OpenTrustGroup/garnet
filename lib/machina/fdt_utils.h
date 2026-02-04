#pragma once

#include <libfdt.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#define GIC_FDT_IRQ_TYPE_SPI 0
#define GIC_FDT_IRQ_TYPE_PPI 1

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