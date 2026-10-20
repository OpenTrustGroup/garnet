// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_device.h"

#include "lib/fxl/logging.h"

#include "garnet/lib/machina/msix_config.h"

static constexpr size_t kMsixFunctionMask = 0x4000;
static constexpr size_t kMsixEnable = 0x8000;
static constexpr uint64_t kBitsPerPbaEntry = 64;
static constexpr uint16_t kMaxMsixVectorPerDevice = 2048;
static constexpr uint64_t kMsixEntriesModulo = 16;
static constexpr uint64_t kMsixPbaEntriesModulo = 8;

namespace machina {

MsixConfig::MsixConfig(VirtioDevice* device) : device_(device), enabled_(false), masked_(false) {
  // per queue vector plus one config vector.
  msix_num_ = device->num_queues() + 1;
  table_entries_.resize(msix_num_);
  pba_entries_.resize((msix_num_ + kBitsPerPbaEntry - 1) / kBitsPerPbaEntry);
}

bool MsixConfig::table_masked(uint32_t index) {
  if (index >= table_entries_.size()) {
    return true;
  } else {
    return table_entries_[index].masked();
  }
}

uint32_t MsixConfig::ReadMsixCapability(uint32_t data) {
  uint16_t msg_ctl = data >> 16;
  msg_ctl &= ~(kMsixEnable | kMsixFunctionMask);

  if (enabled_) {
    msg_ctl |= kMsixEnable;
  }
  if (masked_) {
    msg_ctl |= kMsixFunctionMask;
  }
  return ((uint32_t)msg_ctl) << 16 | (data & 0xffff);
}

uint8_t MsixConfig::get_pba_bit(uint16_t vector) {
  FXL_CHECK(vector < kMaxMsixVectorPerDevice);

  uint32_t index = vector / kBitsPerPbaEntry;
  uint32_t shift = vector % kBitsPerPbaEntry;

  return ((pba_entries_[index] >> shift) & 0x1);
}

void MsixConfig::set_pba_bit(uint16_t index) {
  FXL_CHECK(index < kMaxMsixVectorPerDevice);

  uint32_t entry_index = index / kBitsPerPbaEntry;
  uint32_t shift = index % kBitsPerPbaEntry;
  uint64_t mask = 1 << shift;

  pba_entries_[entry_index] |= mask;
}

void MsixConfig::clear_pba_bit(uint16_t index) {
  FXL_CHECK(index < kMaxMsixVectorPerDevice);

  uint32_t pba_index = index / kBitsPerPbaEntry;
  uint32_t shift = index % kBitsPerPbaEntry;
  uint64_t mask = 1 << shift;

  pba_entries_[pba_index] &= ~mask;
}

void MsixConfig::InjectMsixAndClearPba(uint16_t index) {
  device_->TriggerMsixInterrupt(index);
  clear_pba_bit(index);
}

void MsixConfig::WriteMsixCapability(uint32_t offset, uint16_t data) {
  FXL_CHECK(offset == 2);

  bool old_masked = masked_;

  masked_ = (data & kMsixFunctionMask) == kMsixFunctionMask;
  enabled_ = (data & kMsixEnable) == kMsixEnable;

  // If the Function Mask bit was set, and has just been cleared, it's
  // important to go through the entire PBA to check if there was any
  // pending MSI-X message to inject, given that the vector is not
  // masked.
  if (old_masked && !masked_) {
    for (uint16_t i = 0; i < table_entries_.size(); i++) {
      if (!(table_masked(i)) && (get_pba_bit(i) == 1)) {
        InjectMsixAndClearPba(i);
      }
    }
    device_->ControlNotify(MsixStatus::Changed);
  } else {
    device_->ControlNotify(MsixStatus::Changed);
  }
}

// Write to MSI-X table
//
// Message Address: the contents of this field specifies the address
//     for the memory write transaction; different MSI-X vectors have
//     different Message Address values
// Message Data: the contents of this field specifies the data driven
//     on AD[31::00] during the memory write transaction's data phase.
// Vector Control: only bit 0 (Mask Bit) is not reserved: when this bit
//     is set, the function is prohibited from sending a message using
//     this MSI-X Table entry.
void MsixConfig::WriteMsixTable(uint64_t offset, const IoValue& value) {
  uint32_t index = offset / kMsixEntriesModulo;
  uint32_t modulo_offset = offset % kMsixEntriesModulo;

  msix_table_entry_t old_entry = table_entries_[index];
  msix_table_entry_t& entry = table_entries_[index];
  switch (value.access_size) {
    case 4:
      if (modulo_offset == 0x0) {
        entry.msg_addr_lo = value.u32;
      } else if (modulo_offset == 0x4) {
        entry.msg_addr_hi = value.u32;
      } else if (modulo_offset == 0x8) {
        entry.msg_data = value.u32;
        device_->AllocMsixInterrupt(index, entry.msg_data);
      } else if (modulo_offset == 0xc) {
        entry.vector_ctl = value.u32;
      } else {
        FXL_LOG(FATAL) << "invalid offset.";
      }
      break;
    case 8:
      if (modulo_offset == 0x0) {
        entry.msg_addr_lo = value.u64 & 0xffffffff;
        entry.msg_addr_hi = value.u64 >> 32;
      } else if (modulo_offset == 0x8) {
        entry.msg_data = value.u64 & 0xffffffff;
        entry.vector_ctl = value.u64 >> 32;
      } else {
        FXL_LOG(FATAL) << "invalid offset.";
      }
      break;
    default:
      FXL_LOG(FATAL) << "invalid data length.";
  }

  // After the MSI-X table entry has been updated, it is necessary to
  // check if the vector control masking bit has changed. In case the
  // bit has been flipped from 1 to 0, we need to inject a MSI message
  // if the corresponding pending bit from the PBA is set. Once the MSI
  // has been injected, the pending bit in the PBA needs to be cleared.
  // All of this is valid only if MSI-X has not been masked for the whole
  // device.

  // Check if bit has been flipped
  if (!masked_) {
    if (old_entry.masked() && !entry.masked()) {
      if (get_pba_bit(index) == 1) {
        InjectMsixAndClearPba(index);
      }
      device_->ControlNotify(MsixStatus::EntryChanged, index);
    } else if (!old_entry.masked() && entry.masked()) {
      device_->ControlNotify(MsixStatus::EntryChanged, index);
    }
  }
}

void MsixConfig::ReadMsixTable(uint64_t offset, IoValue* value) {
  uint32_t index = offset / kMsixEntriesModulo;
  uint32_t modulo_offset = offset % kMsixEntriesModulo;

  msix_table_entry_t& entry = table_entries_[index];
  switch (value->access_size) {
    case 4:
      if (modulo_offset == 0x0) {
        value->u32 = entry.msg_addr_lo;
      } else if (modulo_offset == 0x4) {
        value->u32 = entry.msg_addr_hi;
      } else if (modulo_offset == 0x8) {
        value->u32 = entry.msg_data;
      } else if (modulo_offset == 0xc) {
        value->u32 = entry.vector_ctl;
      } else {
        FXL_LOG(FATAL) << "invalid offset.";
      }
      break;
    case 8:
      if (modulo_offset == 0x0) {
        value->u64 = ((uint64_t)entry.msg_addr_hi << 32) | entry.msg_addr_lo;
      } else if (modulo_offset == 0x8) {
        value->u64 = ((uint64_t)entry.vector_ctl << 32) | entry.msg_data;
      } else {
        FXL_LOG(FATAL) << "invalid offset.";
      }
      break;
    default:
      FXL_LOG(FATAL) << "invalid data length.";
  }
}

// Read PBA Entries
//
// Pending Bits[63::00]: For each Pending Bit that is set, the function
// has a pending message for the associated MSI-X Table entry.
void MsixConfig::ReadPbaEntries(uint64_t offset, IoValue* value) {
  uint32_t index = offset / kMsixPbaEntriesModulo;
  uint32_t modulo_offset = offset % kMsixPbaEntriesModulo;

  switch (value->access_size) {
    case 4:
      if (modulo_offset == 0) {
        value->u32 = pba_entries_[index] & 0xffffffff;
      } else if (modulo_offset == 4) {
        value->u32 = pba_entries_[index] >> 32;
      } else {
        FXL_LOG(FATAL) << "invalid offset.";
      }
      break;
    case 8:
      if (modulo_offset == 0) {
        value->u64 = pba_entries_[index];
      } else {
        FXL_LOG(FATAL) << "invalid offset.";
      }
      break;
    default:
      FXL_LOG(FATAL) << "invalid data length.";
  }
}

}  // namespace machina
