// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/arm64/pl011.h"

#include <endian.h>
#include <stdio.h>

#include <libzbi/zbi.h>
#include <zircon/boot/driver-config.h>

#include "garnet/lib/machina/guest.h"
#include "lib/fxl/logging.h"

__BEGIN_CDECLS;
#include <libfdt.h>
__END_CDECLS;

namespace machina {

// clang-format off

// PL011 registers.
enum class Pl011Register : uint64_t {
    DR      = 0x00,
    FR      = 0x18,
    IBRD    = 0x24,
    FBRD    = 0x28,
    LCR     = 0x2c,
    CR      = 0x30,
    IFLS    = 0x34,
    IMSC    = 0x38,
    ICR     = 0x44,
};

static constexpr uint64_t kPl011PhysBase = 0x808300000;
static constexpr uint64_t kPl011Size     = 0x1000;

// clang-format on

zx_status_t Pl011::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::MMIO_SYNC, kPl011PhysBase, kPl011Size, 0, this);
}

zx_status_t Pl011::Read(uint64_t addr, IoValue* value) const {
  switch (static_cast<Pl011Register>(addr)) {
    case Pl011Register::CR: {
      std::lock_guard<std::mutex> lock(mutex_);
      value->u16 = control_;
    }
      return ZX_OK;
    case Pl011Register::FR:
    case Pl011Register::IMSC:
      value->u16 = 0;
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled PL011 address read 0x" << std::hex << addr;
      return ZX_ERR_IO;
  }
}

zx_status_t Pl011::Write(uint64_t addr, const IoValue& value) {
  switch (static_cast<Pl011Register>(addr)) {
    case Pl011Register::CR: {
      std::lock_guard<std::mutex> lock(mutex_);
      control_ = value.u16;
    }
      return ZX_OK;
    case Pl011Register::DR:
      Print(value.u8);
      return ZX_OK;
    case Pl011Register::IBRD:
    case Pl011Register::FBRD:
    case Pl011Register::ICR:
    case Pl011Register::IFLS:
    case Pl011Register::IMSC:
    case Pl011Register::LCR:
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled PL011 address write 0x" << std::hex << addr;
      return ZX_ERR_IO;
  }
}

void Pl011::Print(uint8_t ch) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tx_buffer_[tx_offset_++] = ch;
    if (tx_offset_ < kBufferSize && ch != '\r') {
      return;
    }
    fprintf(stdout, "%.*s", tx_offset_, tx_buffer_);
    tx_offset_ = 0;
  }
  fflush(stdout);
}

zx_status_t Pl011::ConfigureZbi(void* zbi_base, size_t zbi_max) const {
  dcfg_simple_t zbi_uart = {
      .mmio_phys = kPl011PhysBase, .irq = 111,
  };
  zbi_result_t res =
      zbi_append_section(zbi_base, zbi_max, sizeof(zbi_uart),
                         ZBI_TYPE_KERNEL_DRIVER, KDRV_PL011_UART, 0, &zbi_uart);
  return res == ZBI_RESULT_OK ? ZX_OK : ZX_ERR_INTERNAL;
}

zx_status_t Pl011::ConfigureDtb(void* dtb) const {
  uint64_t reg_val[2] = {htobe64(kPl011PhysBase), htobe64(kPl011Size)};
  int node_off = fdt_node_offset_by_prop_value(dtb, -1, "reg", reg_val, sizeof(reg_val));
  if (node_off < 0) {
    FXL_LOG(ERROR) << "Failed to find Pl011 in DTB";
    return ZX_ERR_INTERNAL;
  }
  int ret = fdt_node_check_compatible(dtb, node_off, "arm,pl011");
  if (ret != 0) {
    FXL_LOG(ERROR) << "Device with Pl011 registers is not Pl011 compatible";
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

}  // namespace machina
