// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_ARM64_GIC_DISTRIBUTOR_H_
#define GARNET_LIB_MACHINA_ARCH_ARM64_GIC_DISTRIBUTOR_H_

#include <array>
#include <set>
#include <limits.h>
#include <map>
#include <mutex>
#include <vector>

#include <lib/zx/interrupt.h>
#include <fuchsia/cpp/machina.h>

#include "garnet/bin/guest/guest_config.h"
#include "garnet/lib/machina/io.h"

#include "garnet/lib/machina/arch/arm64/gic_v3.h"

#include "garnet/lib/machina/arch/arm64/gic_its.h"

namespace machina {

enum class GicVersion {
  V2 = 2,
  V3 = 3,
};

// NOTE: This must match the same constant in arch/hypervisor.h within Zircon.
static constexpr size_t kNumInterrupts = 1024;
static constexpr uint8_t kNumSgisAndPpis = 32;
static constexpr uint8_t kSpiBase = 32;
static constexpr uint8_t kMaxVcpus = 8;
static constexpr uint32_t kExtendedSpiBase = 4096;
static constexpr uint32_t kExtendedSpiMax = 5119;
static constexpr size_t kExtendedSpiInterrupts =
    (kExtendedSpiMax - kExtendedSpiBase) + 1;

class Guest;
class Vcpu;
class GicDistributor;

// Implements GIC distributor/redistributor passthru.
class GicIoWrapper : public IoHandler {
 public:
  GicIoWrapper(IoHandler* base_handler) : base_handler_(base_handler) {}
  zx_status_t Read(uint64_t addr, IoValue* value) const override {
    zx_status_t status = base_handler_->Read(addr, value);
    if (status != ZX_OK) {
      return status;
    }
    auto iter = passthru_map_.find(addr);
    if (iter != passthru_map_.end()) {
      status = iter->second->Read(addr, value);
    }
    return status;
  }
  zx_status_t Write(uint64_t addr, const IoValue& value) override {
    zx_status_t status = base_handler_->Write(addr, value);
    if (status != ZX_OK) {
      return status;
    }
    auto iter = passthru_map_.find(addr);
    if (iter != passthru_map_.end()) {
      status = iter->second->Write(addr, value);
    }
    return status;
  }
  zx_status_t AddPassthru(uint64_t addr, IoHandler* handler) {
    auto passthru = passthru_map_.find(addr);
    if (passthru != passthru_map_.end())
      return ZX_ERR_ALREADY_BOUND;
    passthru_map_.emplace(addr, handler);
    return ZX_OK;
  }

 private:
  std::unordered_map<uint64_t, IoHandler*> passthru_map_;
  IoHandler* base_handler_;
};

// Implements GIC redistributor.
class GicRedistributor : public IoHandler {
 public:
  GicRedistributor(Guest* guest,
                   uint16_t processor_number_,
                   uint64_t affinity_value,
                   bool last)
      : guest_(guest),
        processor_number_(processor_number_),
        affinity_value_(affinity_value),
        last_(last),
        enabled_(0) {}

  zx_status_t Init(const std::vector<uint16_t>& interrupts);

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  void BindVcpu(Vcpu* vcpu);

  zx_status_t MaskInterrupt(uint32_t vector);
  zx_status_t UnmaskInterrupt(uint32_t vector);

  void EnableIts(GicIts* its) {
    its_enabled_ = true;
    its_ = its;
  }

  bool LpisEnabled() { return lpis_enabled_; }

  uint64_t PropBaser() { return propbaser_; }

 private:
  Guest *guest_;
  uint16_t processor_number_;
  uint64_t affinity_value_;
  bool last_;

  uint32_t enabled_;
  std::map<uint32_t, zx::interrupt> percpu_interrupts_;

  GicIts* its_{nullptr};
  bool its_enabled_{false};
  bool lpis_enabled_;
  uint32_t ctrl_{0};

  uint64_t propbaser_{INITIAL_PROPBASER_VALUE};
  uint64_t pendbaser_{INITIAL_PENDBASER_VALUE};
};

// Implements GIC distributor.
class GicDistributor : public IoHandler {
 public:
  GicDistributor(Guest* guest, machina::InterruptServiceSyncPtr *interrupt_svc = nullptr);
  zx_status_t Init(uint8_t num_cpus,
                   Gic gic_version,
                   const std::vector<uint16_t>& interrupts,
                   const std::vector<uint16_t>& percpu_interrupts,
                   uint64_t gicd_paddr = 0,
                   uint64_t gicr_paddr = 0) __TA_NO_THREAD_SAFETY_ANALYSIS;

  zx_status_t Init(uint8_t num_cpus,
                   Gic gic_version,
                   const VgicSpec& vgic) __TA_NO_THREAD_SAFETY_ANALYSIS;

  zx_status_t PassThroughInterrupts(const std::vector<uint16_t>& interrupts);
  zx_status_t MovePassthroughToUos(const std::vector<uint16_t>& interrupts);
  void Shutdown();

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  zx_status_t RegisterVcpu(uint8_t vcpu_num,
                           Vcpu* vcpu) __TA_NO_THREAD_SAFETY_ANALYSIS;

  zx_status_t Interrupt(uint32_t global_irq);
  static constexpr size_t kTimeSlicedPtIrqCount = 5;
  zx_status_t SetTimeSlicedPtIrq(
      const std::array<uint32_t, kTimeSlicedPtIrqCount>& interrupts);
  zx_status_t ConfigureTimeSlicedPtIrqForwarding(
      const std::array<uint32_t, kTimeSlicedPtIrqCount>& interrupts);
  zx_status_t RaiseTimeSlicedPtIrq(uint32_t global_irq);

  void EnableIts(GicIts* its);

  std::vector<std::unique_ptr<GicRedistributor>>& redistributors() {
    std::lock_guard<std::mutex> lock(mutex_);
    return redistributors_;
  }

  bool LpisEnabled() {
    std::lock_guard<std::mutex> lock(mutex_);
    return redistributors_[0]->LpisEnabled();
  }

  uint64_t PropBaser() {
    std::lock_guard<std::mutex> lock(mutex_);
    return redistributors_[0]->PropBaser();
  }

  void SetSPIVcpuMask(uint64_t mask) {
    vcpu_mask_ = mask;
    spi_vcpus_.clear();
    for (int i = 0; i < kMaxVcpus; i++) {
      if (vcpu_mask_ & (1 << i)) {
        spi_vcpus_.push_back(i);
      }
    }
  }

  uint64_t GetSPIVcpuMask() { return vcpu_mask_; }

  std::vector<int> GetSPIVcpus() { return spi_vcpus_; }

  void AddWhiteListSPIs(int vcpu_id, std::vector<uint16_t> spi_array) {
    if (vcpu_id < kMaxVcpus) {
      spi_white_list_[vcpu_id].insert(spi_white_list_[vcpu_id].end(),
                                      spi_array.begin(), spi_array.end());
    }
  }

  bool InSPIWhileList(int vcpu_id, uint16_t spi) {
    if (spi_white_list_[vcpu_id].empty()) {
      return true;
    } else {
      auto it = std::find(spi_white_list_[vcpu_id].begin(),
                          spi_white_list_[vcpu_id].end(), spi);
      if (it != spi_white_list_[vcpu_id].end()) {
        return true;
      } else {
        return false;
      }
    }
  }

  bool IsPassthrough(uint16_t vector) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = interrupts_.find(vector);
    return it != interrupts_.end();
  }

 private:

  Guest* guest_;
  machina::InterruptServiceSyncPtr *interrupt_svc_;
  std::map<uint32_t, zx::interrupt> interrupts_ __TA_GUARDED(mutex_);
  std::vector<uint16_t> irqs_movable_;
  Gic gic_version_ = Gic::V2;
  Vcpu* vcpus_[kMaxVcpus] = {};
  uint8_t num_vcpus_ = 0;
  mutable std::mutex mutex_;
  bool affinity_routing_ __TA_GUARDED(mutex_) = false;
  std::vector<std::unique_ptr<GicRedistributor>> __TA_GUARDED(mutex_)
      redistributors_;
  uint8_t enabled_[kNumInterrupts / CHAR_BIT] __TA_GUARDED(mutex_) = {};

  // SPI routing without affinity routing uses these cpu masks.
  uint8_t cpu_masks_[kNumInterrupts] __TA_GUARDED(mutex_) = {};

  uint8_t configs_[kNumInterrupts] __TA_GUARDED(mutex_) = {};

  // SPI routing with affinity routing either sends the interrupt to all VCPUs
  // or routes to the VCPU specified vcpu_id in cpu_routes_.
  bool broadcast_[kNumInterrupts - kNumSgisAndPpis] __TA_GUARDED(mutex_) = {};
  uint8_t cpu_routes_[kNumInterrupts - kNumSgisAndPpis] __TA_GUARDED(
      mutex_) = {};

  // Extended SPI (4096 - 5119).
  bool broadcast_espi_[kExtendedSpiInterrupts] __TA_GUARDED(mutex_) = {};
  uint8_t cpu_routes_espi_[kExtendedSpiInterrupts] __TA_GUARDED(mutex_) = {};
  uint8_t enabled_espi_[kExtendedSpiInterrupts / CHAR_BIT] __TA_GUARDED(
      mutex_) = {};
  uint32_t gicd_icfg_[kNumInterrupts / 16] __TA_GUARDED(mutex_) = {};

  bool its_enabled_{false};

  std::unique_ptr<GicIoWrapper> distributor_iowrapper_;
  std::vector<std::unique_ptr<GicIoWrapper>> redistributor_iowrappers_;
  std::vector<std::unique_ptr<IoHandler>> io_handler_;
  uint64_t vcpu_mask_;
  std::vector<uint16_t> spi_white_list_[kMaxVcpus];
  std::vector<int> spi_vcpus_;

  zx_status_t TargetInterrupt(uint32_t global_irq, uint8_t cpu_mask);
  zx_status_t RouteInterrupt(uint32_t global_irq);
  zx_status_t BindVcpus(uint32_t vector, uint8_t cpu_mask) __TA_REQUIRES(mutex_);
  zx_status_t BindVcpuExclusively(uint32_t vector, uint8_t cpu_id) __TA_REQUIRES(mutex_);

  zx_status_t MaskInterrupt(uint32_t vector) __TA_REQUIRES(mutex_);
  zx_status_t UnmaskInterrupt(uint32_t vector) __TA_REQUIRES(mutex_);
  zx_status_t ConfigureRead(uint32_t reg, uint64_t* val);
  zx_status_t ConfigureWrite(uint32_t reg, uint64_t base, uint64_t val);
  zx_status_t Configure(uint32_t reg, uint64_t val);

  bool IsValidSpiInterrupt(uint32_t vector);
  bool IsValidInterrupt(uint32_t vector);
  bool IsExtendedSpi(uint32_t vector);

  std::map<uint32_t, zx::interrupt> time_sliced_pt_irqs_ __TA_GUARDED(mutex_);
  std::set<uint32_t> time_sliced_pt_irq_numbers_ __TA_GUARDED(mutex_);
  zx_status_t CreateTimeSlicedPtIrqHandle(uint32_t global_irq,
                                          zx::interrupt* out);
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_ARM64_GIC_DISTRIBUTOR_H_
