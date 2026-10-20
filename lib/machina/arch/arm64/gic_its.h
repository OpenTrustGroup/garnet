// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <stdint.h>
#include <zircon/types.h>

#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/phys_mem.h"

namespace machina {

// With 13 bits we can support 8192 devices and linux
// will not use indirect table which can simplify our code.
static constexpr uint32_t kGicItsDevBits = 13;
// With 6 bits we can have 63 virtio queue per device.
static constexpr uint32_t kGicItsIdBits = 6;

class GicDistributor;
class Guest;
struct GicItsCmd;

class GicItsCollection {
 public:
  uint32_t collection_id;
  uint32_t target_addr;
};

class GicLpiIrq {
 public:
  uint32_t irq;
  bool enabled;
  uint32_t target_vcpu{0xffffffff};
};

class GicItsItte {
 public:
  GicLpiIrq* lpi;
  GicItsCollection* collection;
  uint32_t event_id{0xffffffff};
};

class GicItsDevice {
 public:
  uint32_t eventid_bits;
  uint64_t itt_addr;
  uint16_t device_id;
  GicItsItte ittes[1 << kGicItsIdBits];

  uint32_t max_event() { return 1 << eventid_bits; }
};

class GicIts : public IoHandler {
 public:
  GicIts(Guest* guest, GicDistributor* controller)
      : guest_(guest), interrupt_controller_(controller) {}
  zx_status_t Init(uint64_t paddr);
  zx_status_t Read(uint64_t, IoValue* value) const override;
  zx_status_t Write(uint64_t, const IoValue& value) override;
  zx_status_t InjectMsi(uint64_t devid, uint16_t evt_inx);
  void InvalidLpiConfig(uint32_t lpi);

 private:
  zx_status_t ReadCtrl(IoValue* value) const;
  zx_status_t WriteCtrl(const IoValue& value);
  zx_status_t ReadIidr(IoValue* value) const;
  zx_status_t ReadTyper(IoValue* value) const;
  zx_status_t WriteCbaser(const IoValue& value);
  zx_status_t WriteCwriter(const IoValue& value);
  zx_status_t ReadBaser(uint64_t addr, IoValue* value) const;
  zx_status_t WriteBaser(uint64_t addr, const IoValue& value);
  zx_status_t ReadIdRegs(uint64_t addr, IoValue* value) const;
  void FreeCollectionListLocked() __TA_REQUIRES(its_mutex_);
  void ClearDevicItsCollectionLocked(uint32_t col_id) __TA_REQUIRES(its_mutex_);
  void FreeCollectionLocked(uint32_t col_id) __TA_REQUIRES(its_mutex_);
  void ProcessCmdsLocked() __TA_REQUIRES(cmd_mutex_);
  void ProcessItsCmd(GicItsCmd* cmd);
  void ProcessMapdCmd(uint64_t* its_cmd) __TA_REQUIRES(its_mutex_);
  bool ItsCheckId(uint64_t baser, uint32_t id, uint64_t* eaddr);
  std::unique_ptr<GicItsDevice> ItsAllocDevice(uint32_t device_id,
                                               uint64_t its_addr,
                                               uint8_t num_eventid_bits);
  void ProcessMapcCmd(uint64_t* its_cmd) __TA_REQUIRES(its_mutex_);
  GicItsCollection* FindCollection(uint16_t col_id) __TA_REQUIRES(its_mutex_);
  int ItsAllocCollection(GicItsCollection** collection, uint16_t col_id)
      __TA_REQUIRES(its_mutex_);
  void UpdateAffinityCollection(GicItsCollection* collection)
      __TA_REQUIRES(its_mutex_);
  void ProcessMapiCmd(uint64_t* its_cmd) __TA_REQUIRES(its_mutex_);
  GicItsItte* FindIte(uint32_t device_id, uint32_t event_id)
      __TA_REQUIRES(its_mutex_);
  GicItsItte* ItsAllocIte(GicItsDevice* device,
                          GicItsCollection* collection,
                          uint32_t event_id);
  void ProcessMoviCmd(uint64_t* its_cmd) __TA_REQUIRES(its_mutex_);
  void ProcessDiscardCmd(uint64_t* its_cmd) __TA_REQUIRES(its_mutex_);
  void ProcessClearCmd(uint64_t* its_cmd);
  void ProcessMoveallCmd(uint64_t* its_cmd);
  void ProcessIntCmd(uint64_t* its_cmd) __TA_REQUIRES(its_mutex_);
  zx_status_t ResolveLpiLocked(uint64_t devid,
                               uint16_t evt_inx,
                               GicLpiIrq** lpi) __TA_REQUIRES(its_mutex_);
  zx_status_t InjectMsiLocked(uint64_t devid, uint16_t evt_inx)
      __TA_REQUIRES(its_mutex_);
  void ProcessInvCmd(uint64_t* its_cmd) __TA_REQUIRES(its_mutex_);
  void ProcessInvAllCmd(uint64_t* its_cmd) __TA_REQUIRES(its_mutex_);
  void UpdateLpiConfig(GicLpiIrq* irq, int filter_vcpu);

  Guest* guest_;

  GicDistributor* interrupt_controller_;

  mutable std::mutex cmd_mutex_;
  uint64_t cbaser_;
  uint32_t creader_ __TA_GUARDED(cmd_mutex_);
  uint32_t cwriter_ __TA_GUARDED(cmd_mutex_);
  bool enabled_{false};

  /* These registers correspond to GITS_BASER{0,1} */
  uint64_t baser_device_table_;
  uint64_t baser_coll_table_;

  mutable std::mutex its_mutex_;
  std::unordered_map<uint32_t, std::unique_ptr<GicItsDevice>> its_devices_
      __TA_GUARDED(its_mutex_);
  std::list<std::unique_ptr<GicItsCollection>> its_colls_
      __TA_GUARDED(its_mutex_);
  std::list<std::unique_ptr<GicLpiIrq>> gic_lpis_;
};

}  // namespace machina