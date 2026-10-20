// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GUEST_CONFIG_H_
#define GARNET_LIB_MACHINA_GUEST_CONFIG_H_

#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "garnet/lib/machina/block_dispatcher.h"

static constexpr uintptr_t kNormalPriority = 16;
static constexpr uintptr_t kSosVcpuPriority = kNormalPriority;
static constexpr uintptr_t kUosVcpuPriority = kNormalPriority;
static constexpr uintptr_t kLooperPriority = kNormalPriority + 2;
static constexpr uintptr_t kIRQPriority = kLooperPriority + 2;

static constexpr uint64_t kSosVcpuTimeSlice = 2000000ULL;
static constexpr uint64_t kUosVcpuTimeSlice = 2000000ULL;

static constexpr uint32_t kNormalCpuAffinity = 0xcf;

namespace machina {

enum class Gic {
  V2 = 2,
  V3 = 3,
};

struct BlockSpec {
  std::string path;
  machina::BlockDispatcher::Guid guid;

  machina::BlockDispatcher::Mode mode = machina::BlockDispatcher::Mode::RW;
  machina::BlockDispatcher::DataPlane data_plane =
      machina::BlockDispatcher::DataPlane::FDIO;
  bool volatile_writes = false;
};

constexpr int kNumVcpuRegs = 31;
constexpr int kNumVgicIrqs = 1024;
constexpr int kMaxNameChars = 32;

// TODO(TC): Consider if Per-Vcpu configuration neede.
struct VcpuSpec {
  uint64_t entry;
  uint64_t hcr;
  uint64_t x[kNumVcpuRegs];
};

struct AudioIrqRegSpec {
  uint32_t irq_status_offset;
  uint32_t irq_clear_offset;
  uint64_t irq_regs_bit;
  uint64_t interested_bitmask;
  uint32_t vmid;
};

struct SharedIrqSpec {
  uint32_t vector;
  uint64_t ctrl_reg_base;
  uint32_t irq_status_offset;
  uint32_t irq_clear_offset;
  uint64_t interested_bitmask;
};

struct AudioAfeClearRegSpec {
  uint32_t clear_offset;
  uint32_t clear_bitmask;
  uint32_t clear_reg_mask;
};

struct AudioVMStatusBitmaskSpec {
  uint32_t interested_bitmask;
  uint32_t status;
  std::vector<AudioAfeClearRegSpec> clear_regs;
};

struct AudioAfeRegSpec {
  uint32_t stats_offset;
  // vmid
  std::unordered_map<uint32_t, AudioVMStatusBitmaskSpec> audio_vm_status;
};

struct AudioIrqSpecEx {
  uint32_t vector;
  uint64_t ctrl_reg_base;
  std::vector<AudioIrqRegSpec> regs;
  std::vector<AudioAfeRegSpec> audio_regs;
};

struct VgicSpec {
  uint64_t gicd_paddr = 0;
  uint64_t gicr_paddr = 0;
  uint64_t its_paddr = 0;
  bool has_its;
  std::vector<uint16_t> irqs;
  std::vector<uint16_t> irqs_movable;
  std::vector<uint16_t> percpu_irqs;
  std::vector<SharedIrqSpec> audio_irqs;
  std::vector<SharedIrqSpec> shared_irqs;
};

struct RprocSpec {
  bool has_ipi_affinity;
  uint16_t ctrl_irq;
  std::vector<uint16_t> local_irqs;
  std::vector<uint16_t> remote_irqs;
};

struct DeviceTreeSpec {
  uint64_t base = 0;
  uint64_t size = 0;
};

struct ProductSpec {
  std::string name;
  std::string file;
};
struct VsmmuSpec {
  uint64_t paddr = 0;
  uint16_t irq;
  std::vector<uint16_t> sids;
};

struct RegAccessSpec {
  uint64_t start_addr;
  uint64_t end_addr;
};

struct IrqMonitorSpec {
  uint8_t enable = 0;
  uint32_t threshold = 20000;
  std::vector<uint16_t> irqs;
};

enum class MemoryPolicy : uint8_t {
  /// Map a VMO as cached memory into the guest physical address space.
  kDemand = 0,
  /// Map a VMO with 1:1 correspondence with host memory as cached memory into
  /// the guest physical address space.
  kCached = 1,
  /// Map a VMO with 1:1 correspondence with host memory as device memory into
  /// the guest physical address space.
  kDevice = 2,
  /// Secure memory that is protected by MPU
  kMpu = 3,
  ///
  kMax,
};

struct VmemSpec {
  std::string name;
  std::string node_name;
  uint64_t gpa_base;
  uint64_t hpa_base;
  uint64_t size;
  MemoryPolicy policy;
  uint8_t is_physmem;
  uint8_t is_reservedmem;
  std::string permission;
};

struct DefaultRegSpec {
  uint64_t regbase;
  uint64_t vmstatus;
  uint64_t value_num;
  uint64_t value_reg;
};

struct VmConfig {
  uint16_t vmid;
  uint8_t num_cpus;
  uint64_t cpu_affinity;
  uint64_t entry;
  uint32_t sched_irq;
  DeviceTreeSpec dtb;
  VmemSpec mem;
  uint8_t sched_id;
  uint32_t uos_exp_irq;
  bool virtio_mem_enable;
  int32_t bindlk_pcpu;
};

struct IpcMboxSpec {
  uint16_t rx_ready_irq = 0;
  uint16_t tx_done_irq = 0;
};

struct GpuSchedTime {
  uint64_t timeout;
  uint64_t flag;
};

struct ArbiterGpuSpec {
  bool enable = false;
  std::vector<uint64_t> regions = {};
  bool is_reserved_mem = false;
  std::unordered_map<std::string, uint32_t> irqs;
  std::vector<uint32_t> vcpus = {};
  uint8_t sched_mode = 0;
  std::unordered_map<std::string, GpuSchedTime> sos_timer = {
      {"schedule", {16ULL * 1000 * 1000, 0x0}}
  };
  std::unordered_map<std::string, GpuSchedTime> uos_timer = {
      {"schedule", {16ULL * 1000 * 1000, 0x0}},
      {"stopping", {5ULL * 1000 * 1000 * 1000, 0x0}}
  };
};

enum class Kernel {
  ZIRCON,
  LINUX,
};

enum class GuestDisplay {
  FRAMEBUFFER,
  SCENIC,
  NONE,
};

static inline std::string get_policy_string(MemoryPolicy policy) {
  switch (policy) {
    case MemoryPolicy::kDemand:
      return "Demand";
    case MemoryPolicy::kCached:
      return "Cached";
    case MemoryPolicy::kDevice:
      return "Device";
    case MemoryPolicy::kMpu:
      return "MPU";
    default:
      return "Unknow";
  }
}

static inline std::ostream& operator<<(std::ostream& os, const VmConfig& cfg) {
  os << "\nVmConfig {\n";
  os << "  vmid: " << cfg.vmid << "\n";
  os << "  num_cpus: " << (uint16_t)cfg.num_cpus << "\n";
  os << "  cpu_affinity: " << std::hex << (uint64_t)cfg.cpu_affinity << "\n";
  os << "  entry: " << std::hex << cfg.entry << "\n";
  os << "  sched_irq: " << std::hex << cfg.sched_irq << "\n";
  os << "  dtb: {\n";
  os << "    base: " << std::hex << cfg.dtb.base << "\n";
  os << "    size: " << std::hex << cfg.dtb.size << "\n";
  os << "  }\n";
  os << "  mem: {\n";
  os << "    gpa_base: " << std::hex << cfg.mem.gpa_base << "\n";
  os << "    hpa_base: " << std::hex << cfg.mem.hpa_base << "\n";
  os << "    size: " << std::hex << cfg.mem.size << "\n";
  os << "    policy: " << get_policy_string(cfg.mem.policy) << "\n";
  os << "    is_physmem: " << (uint16_t)cfg.mem.is_physmem << "\n";
  os << "  }\n";
  os << "}\n";
  return os;
}

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GUEST_CONFIG_H_
