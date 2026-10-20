// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/features/features.h"

#include <fcntl.h>
#include <libfdt.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <vector>

#include <fbl/unique_fd.h>
#include <zircon/device/sysinfo.h>
#include <zircon/boot/wdt-profile.h>
#ifdef __Nebula__
#include <pal/platform_defs.h>
#include <trusty_std.h>
#endif
#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"

namespace {

constexpr char kDisabledStatus[] = "disabled";
constexpr char kFailedStatus[] = "fail";
constexpr char kSysinfoPath[] = "/dev/misc/sysinfo";

zx_status_t FdtErrorToStatus(int error) {
  switch (error) {
    case -FDT_ERR_NOTFOUND:
      return ZX_ERR_NOT_FOUND;
    case -FDT_ERR_NOSPACE:
      return ZX_ERR_NO_SPACE;
    case -FDT_ERR_BADPATH:
    case -FDT_ERR_BADOFFSET:
      return ZX_ERR_INVALID_ARGS;
    default:
      return ZX_ERR_IO_DATA_INTEGRITY;
  }
}

bool NodeIsDisabled(const void* dtb, int node) {
  int len = 0;
  const char* status =
      static_cast<const char*>(fdt_getprop(dtb, node, "status", &len));
  if (status == nullptr || len == 0 || status[len - 1] != '\0') {
    return false;
  }
  return !strcmp(status, kDisabledStatus) ||
         !strncmp(status, kFailedStatus, sizeof(kFailedStatus) - 1);
}

zx_status_t FeaturePathIsEnabled(const void* dtb,
                                 int features_node,
                                 const char* const* path,
                                 size_t path_len,
                                 bool default_value,
                                 bool* enabled) {
  if (enabled == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  int node = features_node;
  if (NodeIsDisabled(dtb, node)) {
    *enabled = false;
    return ZX_OK;
  }
  for (size_t i = 0; i < path_len; ++i) {
    node = fdt_subnode_offset(dtb, node, path[i]);
    if (node == -FDT_ERR_NOTFOUND) {
      *enabled = default_value;
      return ZX_OK;
    }
    if (node < 0) {
      return FdtErrorToStatus(node);
    }
    if (NodeIsDisabled(dtb, node)) {
      *enabled = false;
      return ZX_OK;
    }
  }
  *enabled = true;
  return ZX_OK;
}

zx_status_t ReadDtbVmo(zx_handle_t vmo, std::vector<uint8_t>* out) {
  size_t size = 0;
  zx_status_t status = zx_vmo_get_size(vmo, &size);
  if (status != ZX_OK) {
    return status;
  }

  out->resize(size);
  status = zx_vmo_read(vmo, out->data(), 0, out->size());
  if (status != ZX_OK) {
    out->clear();
  }
  return status;
}

}  // namespace

namespace features {

const FeatureSet::FeatureSpec* FeatureSet::FeatureSpecs(size_t* count) {
  static constexpr FeatureSpec kFeatureSpecs[] = {
      {{"disable_uartlog"}, 1, &FeatureSet::Values::hyp_disable_uart_log},
      {{"nebulalog"}, 1, &FeatureSet::Values::nebulalog_enabled},
      {{"vmlog"}, 1, &FeatureSet::Values::vmlog_enabled},
      {{"mmio_bus"}, 1, &FeatureSet::Values::mmio_bus_enabled},
      {{"mmio_bus", "mmio-virtio-block"},
       2,
       &FeatureSet::Values::mmio_virtio_block_enabled},
      {{"pci_bus"}, 1, &FeatureSet::Values::pci_bus_enabled},
      {{"pci_bus", "pci-virtio-block"},
       2,
       &FeatureSet::Values::pci_virtio_block_enabled},
      {{"pci_bus", "virtio-cluster"},
       2,
       &FeatureSet::Values::virtio_cluster_enabled},
      {{"pci_bus", "virtio-spi"}, 2, &FeatureSet::Values::virtio_spi_enabled},
      {{"pci_bus", "virtio-i2c"}, 2, &FeatureSet::Values::virtio_i2c_enabled},
      {{"pci_bus", "virtio-eint"},
       2,
       &FeatureSet::Values::virtio_eint_enabled},
      {{"pci_bus", "virtio-rtc"},
       2,
       &FeatureSet::Values::virtio_rtc_enabled},
      {{"pci_bus", "virtio-console"},
       2,
       &FeatureSet::Values::virtio_console_enabled},
      {{"pci_bus", "virtio-vsock"},
       2,
       &FeatureSet::Values::virtio_vsock_enabled},
      {{"virtio-vsock", "gzfs"},
       2,
       &FeatureSet::Values::gzfs_enabled},
      {{"ufs"}, 1, &FeatureSet::Values::ufs_enabled},
      {{"smc"}, 1, &FeatureSet::Values::smc_enabled},
      {{"pvblk"}, 1, &FeatureSet::Values::pvblk_enabled},
      {{"cvm_lock"}, 1, &FeatureSet::Values::cvm_lock_enabled},
  };

  if (count != nullptr) {
    *count = sizeof(kFeatureSpecs) / sizeof(kFeatureSpecs[0]);
  }
  return kFeatureSpecs;
}

FeatureSet FeatureSet::FromValues(Values values) {
  FeatureSet features;
  features.values_ = values;
  return features;
}

zx_status_t FeatureSet::LoadFromSysinfo(FeatureSet* out) {
  if (out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::unique_fd fd(open(kSysinfoPath, O_RDONLY));
  if (!fd) {
    return ZX_ERR_IO;
  }

  zx_handle_t dtb_vmo = ZX_HANDLE_INVALID;
  ssize_t actual = ioctl_sysinfo_get_devicetree(fd.get(), &dtb_vmo);
  if (actual == ZX_ERR_NOT_FOUND) {
    Values values;
#ifdef __Nebula__
    // Venus QEMU has no product DTB or MediaTek WDT. Keep its managed
    // software watchdog. Real boards continue to default to no-sink.
    uint32_t product_id = 0;
    if (_trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_PRODUCT_ID,
                      &product_id) == 0 && product_id == BOARD_VID_QEMU) {
      values.wdt_sink_enabled = true;
    }
#endif
    FXL_LOG(WARNING) << "event=wdt_profile_without_dtb sink="
                     << values.wdt_sink_enabled;
    *out = FromValues(values);
    return ZX_OK;
  }
  if (actual < 0) {
    return static_cast<zx_status_t>(actual);
  }
  if (actual != sizeof(dtb_vmo) || dtb_vmo == ZX_HANDLE_INVALID) {
    if (dtb_vmo != ZX_HANDLE_INVALID) {
      zx_handle_close(dtb_vmo);
    }
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  std::vector<uint8_t> dtb;
  zx_status_t status = ReadDtbVmo(dtb_vmo, &dtb);
  zx_handle_close(dtb_vmo);
  if (status != ZX_OK) {
    return status;
  }
  return ParseDtb(dtb.data(), dtb.size(), out);
}

zx_status_t FeatureSet::ParseDtb(const void* dtb,
                                 size_t size,
                                 FeatureSet* out) {
  if (dtb == nullptr || out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (size < sizeof(fdt_header)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int status = fdt_check_header(dtb);
  if (status != 0) {
    return FdtErrorToStatus(status);
  }
  if (static_cast<size_t>(fdt_totalsize(dtb)) > size) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  FeatureSet features;
  int features_node = fdt_path_offset(dtb, "/features");
  if (features_node == -FDT_ERR_NOTFOUND) {
    *out = features;
    return ZX_OK;
  }
  if (features_node < 0) {
    FXL_LOG(ERROR) << "Invalid devicetree feature tree";
    return FdtErrorToStatus(features_node);
  }

  size_t spec_count = 0;
  const FeatureSpec* specs = FeatureSpecs(&spec_count);
  for (size_t i = 0; i < spec_count; ++i) {
    const FeatureSpec& spec = specs[i];
    bool* enabled = &(features.values_.*spec.enabled);
    zx_status_t feature_status = FeaturePathIsEnabled(
        dtb, features_node, spec.path, spec.path_len, *enabled, enabled);
    if (feature_status != ZX_OK) {
      return feature_status;
    }
  }

  features.values_.wdt_sink_enabled = zx_wdt_sink_enabled(dtb);
  *out = features;
  return ZX_OK;
}

}  // namespace features
