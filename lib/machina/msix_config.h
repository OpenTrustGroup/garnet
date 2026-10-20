// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include "garnet/lib/machina/io.h"

static constexpr uint32_t kEntryMask = 0x1;

namespace machina {

class VirtioDevice;

enum MsixStatus {
  Changed,
  EntryChanged,
  NothingToDo,
};

typedef struct msix_table_entry {
  uint32_t msg_addr_lo{0};
  uint32_t msg_addr_hi{0};
  uint32_t msg_data{0};
  uint32_t vector_ctl{0};

  bool masked() { return (vector_ctl & kEntryMask) == kEntryMask; }
} msix_table_entry_t;

class MsixConfig {
 public:
  MsixConfig(VirtioDevice* device);

  bool enabled() { return enabled_; }
  bool masked() { return masked_; }
  uint16_t num_vectors() { return msix_num_; }
  bool table_masked(uint32_t index);
  uint32_t ReadMsixCapability(uint32_t data);
  void WriteMsixCapability(uint32_t offset, uint16_t data);
  void WriteMsixTable(uint64_t offset, const IoValue& value);
  void ReadMsixTable(uint64_t offset, IoValue* value);
  void ReadPbaEntries(uint64_t offset, IoValue* value);

 private:
  void InjectMsixAndClearPba(uint16_t index);
  uint8_t get_pba_bit(uint16_t vector);
  void set_pba_bit(uint16_t vector);
  void clear_pba_bit(uint16_t vector);

  VirtioDevice* device_;
  bool enabled_;
  bool masked_;
  uint16_t msix_num_;
  std::vector<uint64_t> pba_entries_;
  std::vector<msix_table_entry_t> table_entries_;
};

}  // namespace machina