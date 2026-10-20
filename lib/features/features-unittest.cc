// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/features/features.h"

#include <libfdt.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include "gtest/gtest.h"

using features::FeatureSet;

namespace {

struct FeatureStatus {
  const char* features = nullptr;
  const char* nebulalog = nullptr;
  const char* vmlog = nullptr;
  const char* virtio_console = nullptr;
  const char* mmio_bus = nullptr;
  const char* mmio_virtio_block = nullptr;
  const char* pci_bus = nullptr;
  const char* pci_virtio_block = nullptr;
  const char* virtio_cluster = nullptr;
  const char* virtio_spi = nullptr;
  const char* virtio_i2c = nullptr;
  const char* virtio_eint = nullptr;
  const char* virtio_rtc = nullptr;
  const char* virtio_vsock = nullptr;
  const char* gzfs = nullptr;
  const char* ufs = nullptr;
  const char* smc = nullptr;
  const char* pvblk = nullptr;
  const char* cvm_lock = nullptr;
  const char* wdt_sink = nullptr;
};

void SetPropString(void* dtb, int node, const char* name, const char* value) {
  ASSERT_EQ(0, fdt_setprop_string(dtb, node, name, value));
}

int AddSubnode(void* dtb, int parent, const char* name) {
  int node = fdt_add_subnode(dtb, parent, name);
  EXPECT_GE(node, 0);
  return node;
}

int AddFeatureNode(void* dtb,
                   int parent,
                   const char* name,
                   const char* status) {
  int node = AddSubnode(dtb, parent, name);
  if (status != nullptr) {
    SetPropString(dtb, node, "status", status);
  }
  return node;
}

FeatureStatus AllOkayStatus() {
  FeatureStatus status;
  status.nebulalog = "okay";
  status.vmlog = "okay";
  status.virtio_console = "okay";
  status.mmio_bus = "okay";
  status.mmio_virtio_block = "okay";
  status.pci_bus = "okay";
  status.pci_virtio_block = "okay";
  status.virtio_cluster = "okay";
  status.virtio_spi = "okay";
  status.virtio_i2c = "okay";
  status.virtio_eint = "okay";
  status.virtio_rtc = "okay";
  status.virtio_vsock = "okay";
  status.gzfs = "okay";
  status.ufs = "okay";
  status.smc = "okay";
  status.pvblk = "okay";
  status.cvm_lock = "okay";
  status.wdt_sink = "okay";
  return status;
}

std::vector<uint8_t> MakeDtb(const FeatureStatus& status) {
  std::vector<uint8_t> dtb(4096);
  EXPECT_EQ(0, fdt_create_empty_tree(dtb.data(), dtb.size()));

  int features = AddFeatureNode(dtb.data(), 0, "features", status.features);

  AddFeatureNode(dtb.data(), features, "nebulalog", status.nebulalog);
  AddFeatureNode(dtb.data(), features, "vmlog", status.vmlog);

  int mmio_bus =
      AddFeatureNode(dtb.data(), features, "mmio_bus", status.mmio_bus);
  AddFeatureNode(dtb.data(), mmio_bus, "mmio-virtio-block",
                 status.mmio_virtio_block);

  int pci_bus = AddFeatureNode(dtb.data(), features, "pci_bus", status.pci_bus);
  AddFeatureNode(dtb.data(), pci_bus, "pci-virtio-block",
                 status.pci_virtio_block);
  AddFeatureNode(dtb.data(), pci_bus, "virtio-cluster", status.virtio_cluster);
  AddFeatureNode(dtb.data(), pci_bus, "virtio-spi", status.virtio_spi);
  AddFeatureNode(dtb.data(), pci_bus, "virtio-i2c", status.virtio_i2c);
  AddFeatureNode(dtb.data(), pci_bus, "virtio-eint", status.virtio_eint);
  AddFeatureNode(dtb.data(), pci_bus, "virtio-rtc", status.virtio_rtc);
  AddFeatureNode(dtb.data(), pci_bus, "virtio-console",
                 status.virtio_console);
  AddFeatureNode(dtb.data(), pci_bus, "virtio-vsock", status.virtio_vsock);

  int virtio_vsock =
      AddFeatureNode(dtb.data(), features, "virtio-vsock",
                     status.virtio_vsock);
  AddFeatureNode(dtb.data(), virtio_vsock, "gzfs", status.gzfs);
  AddFeatureNode(dtb.data(), features, "ufs", status.ufs);
  AddFeatureNode(dtb.data(), features, "smc", status.smc);
  AddFeatureNode(dtb.data(), features, "pvblk", status.pvblk);
  AddFeatureNode(dtb.data(), features, "cvm_lock", status.cvm_lock);
  AddFeatureNode(dtb.data(), features, "wdt-sink", status.wdt_sink);

  EXPECT_EQ(0, fdt_pack(dtb.data()));
  dtb.resize(fdt_totalsize(dtb.data()));
  return dtb;
}

std::vector<uint8_t> MakeDtbWithoutFeatureSchema() {
  std::vector<uint8_t> dtb(4096);
  EXPECT_EQ(0, fdt_create_empty_tree(dtb.data(), dtb.size()));
  EXPECT_EQ(0, fdt_pack(dtb.data()));
  dtb.resize(fdt_totalsize(dtb.data()));
  return dtb;
}

void ExpectAllEnabled(const FeatureSet& features) {
  EXPECT_TRUE(features.nebulalog_enabled());
  EXPECT_TRUE(features.vmlog_enabled());
  EXPECT_TRUE(features.virtio_console_enabled());
  EXPECT_TRUE(features.mmio_bus_enabled());
  EXPECT_TRUE(features.pci_bus_enabled());
  EXPECT_TRUE(features.virtio_cluster_enabled());
  EXPECT_TRUE(features.virtio_spi_enabled());
  EXPECT_TRUE(features.virtio_i2c_enabled());
  EXPECT_TRUE(features.virtio_eint_enabled());
  EXPECT_TRUE(features.virtio_rtc_enabled());
  EXPECT_TRUE(features.mmio_virtio_block_enabled());
  EXPECT_TRUE(features.pci_virtio_block_enabled());
  EXPECT_TRUE(features.virtio_vsock_enabled());
  EXPECT_TRUE(features.gzfs_enabled());
  EXPECT_TRUE(features.ufs_enabled());
  EXPECT_TRUE(features.smc_enabled());
  EXPECT_TRUE(features.pvblk_enabled());
  EXPECT_TRUE(features.cvm_lock_enabled());
  EXPECT_TRUE(features.wdt_sink_enabled());
}

void ExpectAllDisabled(const FeatureSet& features) {
  EXPECT_FALSE(features.nebulalog_enabled());
  EXPECT_FALSE(features.vmlog_enabled());
  EXPECT_FALSE(features.virtio_console_enabled());
  EXPECT_FALSE(features.mmio_bus_enabled());
  EXPECT_FALSE(features.pci_bus_enabled());
  EXPECT_FALSE(features.virtio_cluster_enabled());
  EXPECT_FALSE(features.virtio_spi_enabled());
  EXPECT_FALSE(features.virtio_i2c_enabled());
  EXPECT_FALSE(features.virtio_eint_enabled());
  EXPECT_FALSE(features.virtio_rtc_enabled());
  EXPECT_FALSE(features.mmio_virtio_block_enabled());
  EXPECT_FALSE(features.pci_virtio_block_enabled());
  EXPECT_FALSE(features.virtio_vsock_enabled());
  EXPECT_FALSE(features.gzfs_enabled());
  EXPECT_FALSE(features.ufs_enabled());
  EXPECT_FALSE(features.smc_enabled());
  EXPECT_FALSE(features.pvblk_enabled());
  EXPECT_FALSE(features.cvm_lock_enabled());
  EXPECT_FALSE(features.wdt_sink_enabled());
}

TEST(FeatureSetTest, DisabledWdtSinkFeature) {
  FeatureStatus status = AllOkayStatus();
  status.wdt_sink = "disabled";
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  EXPECT_FALSE(features.wdt_sink_enabled());
}

TEST(FeatureSetTest, MissingFeatureSchemaDisablesByDefault) {
  std::vector<uint8_t> dtb = MakeDtbWithoutFeatureSchema();

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  ExpectAllDisabled(features);
}

TEST(FeatureSetTest, OkayFeatureStatusIsEnabled) {
  FeatureStatus status = AllOkayStatus();
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  ExpectAllEnabled(features);
}

TEST(FeatureSetTest, MissingFeatureStatusIsEnabled) {
  FeatureStatus status;
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  ExpectAllEnabled(features);
}

TEST(FeatureSetTest, DisabledFeatureTreeDisablesChildren) {
  FeatureStatus status = AllOkayStatus();
  status.features = "disabled";
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  ExpectAllDisabled(features);
}

TEST(FeatureSetTest, DisabledMmioVirtioBlockFeature) {
  FeatureStatus status = AllOkayStatus();
  status.mmio_virtio_block = "disabled";
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  EXPECT_FALSE(features.mmio_virtio_block_enabled());
  EXPECT_TRUE(features.pci_virtio_block_enabled());
  EXPECT_TRUE(features.virtio_console_enabled());
  EXPECT_TRUE(features.mmio_bus_enabled());
  EXPECT_TRUE(features.pci_bus_enabled());
  EXPECT_TRUE(features.nebulalog_enabled());
}

TEST(FeatureSetTest, DisabledPciVirtioBlockFeature) {
  FeatureStatus status = AllOkayStatus();
  status.pci_virtio_block = "disabled";
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  EXPECT_TRUE(features.mmio_virtio_block_enabled());
  EXPECT_FALSE(features.pci_virtio_block_enabled());
  EXPECT_TRUE(features.mmio_bus_enabled());
  EXPECT_TRUE(features.pci_bus_enabled());
}

TEST(FeatureSetTest, DisabledBothVirtioBlockFeatures) {
  FeatureStatus status = AllOkayStatus();
  status.mmio_virtio_block = "disabled";
  status.pci_virtio_block = "disabled";
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  EXPECT_FALSE(features.mmio_virtio_block_enabled());
  EXPECT_FALSE(features.pci_virtio_block_enabled());
  EXPECT_TRUE(features.virtio_console_enabled());
}

TEST(FeatureSetTest, DisabledVmlogDoesNotDisableVirtioConsoleFeature) {
  FeatureStatus status = AllOkayStatus();
  status.vmlog = "disabled";
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  EXPECT_FALSE(features.vmlog_enabled());
  EXPECT_TRUE(features.virtio_console_enabled());
  EXPECT_TRUE(features.mmio_virtio_block_enabled());
  EXPECT_TRUE(features.pci_virtio_block_enabled());
}

TEST(FeatureSetTest, DisabledVirtioConsoleFeature) {
  FeatureStatus status = AllOkayStatus();
  status.virtio_console = "disabled";
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  EXPECT_TRUE(features.vmlog_enabled());
  EXPECT_FALSE(features.virtio_console_enabled());
  EXPECT_TRUE(features.mmio_virtio_block_enabled());
  EXPECT_TRUE(features.pci_virtio_block_enabled());
}

TEST(FeatureSetTest, DisabledPciBusDisablesChildFeatures) {
  FeatureStatus status = AllOkayStatus();
  status.pci_bus = "disabled";
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  EXPECT_FALSE(features.pci_bus_enabled());
  EXPECT_FALSE(features.virtio_cluster_enabled());
  EXPECT_FALSE(features.virtio_spi_enabled());
  EXPECT_FALSE(features.virtio_i2c_enabled());
  EXPECT_FALSE(features.virtio_eint_enabled());
  EXPECT_FALSE(features.virtio_rtc_enabled());
  EXPECT_FALSE(features.virtio_console_enabled());
  EXPECT_FALSE(features.virtio_vsock_enabled());
  EXPECT_FALSE(features.pci_virtio_block_enabled());
  EXPECT_TRUE(features.mmio_virtio_block_enabled());
  EXPECT_TRUE(features.vmlog_enabled());
}

TEST(FeatureSetTest, DisabledPciChildFeature) {
  FeatureStatus status = AllOkayStatus();
  status.virtio_i2c = "disabled";
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  EXPECT_TRUE(features.pci_bus_enabled());
  EXPECT_TRUE(features.virtio_cluster_enabled());
  EXPECT_TRUE(features.virtio_spi_enabled());
  EXPECT_FALSE(features.virtio_i2c_enabled());
  EXPECT_TRUE(features.virtio_eint_enabled());
  EXPECT_TRUE(features.virtio_rtc_enabled());
  EXPECT_TRUE(features.virtio_vsock_enabled());
}

TEST(FeatureSetTest, DisabledIndependentTopLevelFeatures) {
  FeatureStatus status = AllOkayStatus();
  status.mmio_bus = "disabled";
  status.gzfs = "disabled";
  status.ufs = "disabled";
  status.smc = "disabled";
  status.pvblk = "disabled";
  status.cvm_lock = "disabled";
  std::vector<uint8_t> dtb = MakeDtb(status);

  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  EXPECT_FALSE(features.mmio_bus_enabled());
  EXPECT_FALSE(features.mmio_virtio_block_enabled());
  EXPECT_FALSE(features.gzfs_enabled());
  EXPECT_FALSE(features.ufs_enabled());
  EXPECT_FALSE(features.smc_enabled());
  EXPECT_FALSE(features.pvblk_enabled());
  EXPECT_FALSE(features.cvm_lock_enabled());
  EXPECT_TRUE(features.vmlog_enabled());
  EXPECT_TRUE(features.pci_bus_enabled());
  EXPECT_TRUE(features.pci_virtio_block_enabled());
  EXPECT_TRUE(features.virtio_vsock_enabled());
}

TEST(FeatureSetTest, MissingWdtNodeDoesNotDisableHardwareFeatures) {
  auto dtb = MakeDtb(AllOkayStatus());
  ASSERT_EQ(0, fdt_del_node(dtb.data(), fdt_path_offset(dtb.data(), "/features/wdt-sink")));
  FeatureSet features;
  ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
  EXPECT_FALSE(features.wdt_sink_enabled());
  EXPECT_TRUE(features.pci_bus_enabled());
}

TEST(FeatureSetTest, WdtProfileRejectsMalformedOrUnknownStatus) {
  for (const char* invalid : {"fail", "disabled", "enabled", "okayy"}) {
    auto status = AllOkayStatus();
    status.wdt_sink = invalid;
    auto dtb = MakeDtb(status);
    FeatureSet features;
    ASSERT_EQ(ZX_OK, FeatureSet::ParseDtb(dtb.data(), dtb.size(), &features));
    EXPECT_FALSE(features.wdt_sink_enabled());
  }
}

TEST(FeatureSetTest, InvalidDtbFails) {
  uint8_t dtb[sizeof(fdt_header)] = {};

  FeatureSet features;
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY,
            FeatureSet::ParseDtb(dtb, sizeof(dtb), &features));
}

}  // namespace
