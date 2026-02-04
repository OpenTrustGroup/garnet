// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_GUEST_CONFIG_H_
#define GARNET_BIN_GUEST_GUEST_CONFIG_H_

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "garnet/lib/machina/guest_config.h"

class GuestConfig {
 public:
  machina::Kernel kernel() const { return kernel_; }
  const std::string& kernel_path() const { return kernel_path_; }
  const std::string& ramdisk_path() const { return ramdisk_path_; }
  const std::vector<machina::BlockSpec>& block_devices() const { return block_specs_; }
  const std::string& cmdline() const { return cmdline_; }
  uint8_t num_cpus() const { return num_cpus_; }
  size_t memory() const { return memory_; }
  zx_duration_t balloon_interval() const {
    return ZX_SEC(balloon_interval_seconds_);
  }
  uint32_t balloon_pages_threshold() const { return balloon_pages_threshold_; }
  bool balloon_demand_page() const { return balloon_demand_page_; }
  machina::GuestDisplay display() const { return display_; }
  bool block_wait() const { return block_wait_; }
  machina::Gic gic_version() const { return gic_version_; }
  const machina::VcpuSpec& vcpu() const { return vcpu_spec_; }
  machina::VgicSpec& vgic() { return vgic_spec_; }
  const std::vector<machina::VsmmuSpec>& vsmmus() const { return vsmmu_specs_; }
  std::vector<machina::VsmmuSpec>& vsmmus() { return vsmmu_specs_; }
  const std::vector<machina::VmemSpec>& vmem() { return vmem_spec_; }
  const machina::DeviceTreeSpec dtb() const { return dtb_spec_; }
  const machina::RprocSpec& rproc_spec() const { return rproc_spec_; }
  bool has_rproc_spec() const { return rproc_spec_.ctrl_irq != 0; }
  uint32_t sched_irq() const { return sched_irq_; }
  uint32_t dump_irq() const { return dump_irq_; }
  const std::vector<machina::IpcMboxSpec>& ipc_mbox() const { return mbox_spec_; }
  machina::IrqMonitorSpec& irq_monitor() { return irq_monitor_spec_; }

  uint64_t phys_base() const;
  uint64_t phys_size() const;
  uint32_t tipc_vq_notifier_irq() const { return tipc_vq_notifier_irq_; }
  uint32_t vhm_irq() const { return vhm_irq_; }
  uint32_t gpu_irq() const { return gpu_irq_; }
  const std::vector<uint16_t>& vsock_irqs() const { return vsock_irqs_; }
  const std::vector<uint16_t>& apu_irqs() const { return apu_irqs_; }
  const std::vector<uint16_t>& cmdq_irqs() const { return cmdq_irqs_; }
  uint32_t guest_reserved_memory() const { return guest_reserved_memory_; }
  std::vector<uint32_t>& bind_pcpus() { return bind_pcpus_; }
  std::vector<uint64_t>& budgets() { return budgets_; }
  std::vector<uint64_t>& periods() { return periods_; }
  std::vector<uint64_t>& sched_priority() { return sched_priority_; }
  std::vector<uint64_t>& sched_timeslice() { return sched_timeslice_; }
  const std::vector<machina::ProductSpec>& product_info() const { return product_spec_; }
  uint32_t vtee_notifier_irq() const { return vtee_notifier_irq_; }
  uint8_t sched_id() const { return sched_id_; }

 private:
  friend class GuestConfigParser;
  machina::Kernel kernel_ = machina::Kernel::ZIRCON;
  std::string kernel_path_;
  std::string ramdisk_path_;
  std::vector<machina::BlockSpec> block_specs_;
  std::string cmdline_;
  uint8_t num_cpus_ = zx_system_get_num_cpus();
  size_t memory_ = 1 << 28;
  uint32_t balloon_interval_seconds_ = 0;
  uint32_t balloon_pages_threshold_ = 0;
  bool balloon_demand_page_ = false;
  machina::GuestDisplay display_ = machina::GuestDisplay::SCENIC;
  bool block_wait_ = false;
  machina::Gic gic_version_ = machina::Gic::V3;
  machina::VcpuSpec vcpu_spec_;
  machina::VgicSpec vgic_spec_;
  machina::RprocSpec rproc_spec_ = {};
  std::vector<machina::VsmmuSpec> vsmmu_specs_;
  machina::IrqMonitorSpec irq_monitor_spec_;
  machina::DeviceTreeSpec dtb_spec_;
  uint32_t tipc_vq_notifier_irq_;
  uint32_t vhm_irq_;
  uint32_t gpu_irq_{0};
  std::vector<uint16_t> vsock_irqs_;
  std::vector<uint16_t> apu_irqs_;
  std::vector<uint16_t> cmdq_irqs_;
  std::vector<machina::VmemSpec> vmem_spec_;
  uint32_t guest_reserved_memory_ = 0;
  uint32_t sched_irq_;
  uint32_t dump_irq_;
  uint8_t sched_id_;
  std::vector<machina::IpcMboxSpec> mbox_spec_;
  std::string cpufreq_;
  std::vector<uint32_t> bind_pcpus_;
  std::vector<machina::ProductSpec> product_spec_;
  std::vector<uint64_t> periods_;
  std::vector<uint64_t> budgets_;
  std::vector<uint64_t> sched_priority_;
  std::vector<uint64_t> sched_timeslice_;
  uint32_t vtee_notifier_irq_;
};

class GuestConfigParser {
 public:
  using OptionHandler = std::function<zx_status_t(const std::string& name,
                                                  const std::string& value)>;
  using OptionMap = std::unordered_map<std::string, OptionHandler>;

  GuestConfigParser(GuestConfig* config);
  ~GuestConfigParser();

  zx_status_t ParseArgcArgv(int argc, char** argv);
  zx_status_t ParseConfig(const std::string& data);
  zx_status_t ParseProductConfig(const std::string& data);

 private:
  OptionMap GetOptionHandlers(const std::string& name);
  OptionMap GetRootOptionHandlers();
  OptionMap GetVcpuOptionHandlers();
  OptionMap GetVgicOptionHandlers();
  OptionMap GetVsmmuOptionHandlers(machina::VsmmuSpec& spec);
  OptionMap GetVmemOptionHandlers();
  OptionMap GetRprocOptionHandlers();
  OptionMap GetIrqMonitorOptionHandlers();

  GuestConfig* cfg_;
  int current_vsmmu_spec_index_;
  std::string current_vsmmu_name_;

  const std::string kRootOptionName = "/";
  const std::string kVcpuOptionName = "vcpu";
  const std::string kVgicOptionName = "vgic";
  const std::string kRprocOptionName = "rproc";
  const std::string kVsmmuptionName = "vsmmu_";
  const std::string kIrqMonitorOptionName = "irq_monitor";
};

#endif  // GARNET_BIN_GUEST_GUEST_CONFIG_H_
