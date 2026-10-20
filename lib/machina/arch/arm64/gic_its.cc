// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/arm64/gic_its.h"
#include "garnet/lib/machina/arch/arm64/gic_its_private.h"
#include "garnet/lib/machina/guest.h"

#define COLLECTION_NOT_MAPPED ((uint32_t)~0)
#define SZ_64K 0x00010000

namespace machina {

static constexpr uint32_t kGicItsIteEsz = 8;
static constexpr uint32_t kGicItsDteEsz = 8;
static constexpr uint32_t kGicItsCteEsz = 8;

zx_status_t GicIts::Init(uint64_t paddr) {
  zx_status_t status;

  status =
      guest_->CreateMapping(TrapType::MMIO_SYNC, paddr, kGicItsSize, 0, this);
  if (status != ZX_OK) {
    return status;
  }

  baser_device_table_ = INITIAL_BASER_VALUE | ((uint64_t)GITS_BASER_TYPE_DEVICE
                                               << GITS_BASER_TYPE_SHIFT);
  baser_coll_table_ =
      INITIAL_BASER_VALUE |
      ((uint64_t)GITS_BASER_TYPE_COLLECTION << GITS_BASER_TYPE_SHIFT);

  return ZX_OK;
}

zx_status_t GicIts::ReadCtrl(IoValue* value) const {
  value->u32 = 0;

  std::lock_guard<std::mutex> lock(cmd_mutex_);
  if (creader_ == cwriter_) {
    value->u32 |= GITS_CTLR_QUIESCENT;
  }
  if (enabled_) {
    value->u32 |= GITS_CTLR_ENABLE;
  }

  return ZX_OK;
}

zx_status_t GicIts::ReadIidr(IoValue* value) const {
  value->u32 =
      (PRODUCT_ID_MACHINA << GITS_IIDR_PRODUCTID_SHIFT) | IMPLEMENTER_ARM;

  return ZX_OK;
}

zx_status_t GicIts::ReadTyper(IoValue* value) const {
  uint64_t reg = GITS_TYPER_PLPIS;

  reg |= GIC_ENCODE_SZ(kGicItsDevBits, 5) << GITS_TYPER_DEVBITS_SHIFT;
  reg |= GIC_ENCODE_SZ(kGicItsIdBits, 5) << GITS_TYPER_IDBITS_SHIFT;
  reg |= GIC_ENCODE_SZ(kGicItsIteEsz, 4) << GITS_TYPER_ITT_ENTRY_SIZE_SHIFT;

  value->u64 = reg;

  return ZX_OK;
}

#define BASER_INDEX(addr) (((addr) / sizeof(uint64_t)) & 0x7)
zx_status_t GicIts::ReadBaser(uint64_t addr, IoValue* value) const {
  uint64_t reg;

  switch (BASER_INDEX(addr)) {
    case 0:
      reg = baser_device_table_;
      break;
    case 1:
      reg = baser_coll_table_;
      break;
    default:
      reg = 0;
      break;
  }

  value->u64 = reg;
  return ZX_OK;
}

zx_status_t GicIts::ReadIdRegs(uint64_t addr, IoValue* value) const {
  switch (static_cast<GicItsRegister>(addr & 0xffff)) {
    case GicItsRegister::PIDR0:
      return 0x92; /* part number, bits[7:0] */
    case GicItsRegister::PIDR1:
      return 0xb4; /* part number, bits[11:8] */
    case GicItsRegister::PIDR2:
      return GIC_PIDR2_ARCH_GICv3 | 0x0b;
    case GicItsRegister::PIDR4:
      return 0x40; /* This is a 64K software visible page */
    /* The following are the ID registers for (any) GIC. */
    case GicItsRegister::CIDR0:
      return 0x0d;
    case GicItsRegister::CIDR1:
      return 0xf0;
    case GicItsRegister::CIDR2:
      return 0x05;
    case GicItsRegister::CIDR3:
      return 0xb1;
    default:
      return 0;
  }
}

zx_status_t GicIts::Read(uint64_t addr, IoValue* value) const {
  switch (static_cast<GicItsRegister>(addr)) {
    case GicItsRegister::CTRL:
      return ReadCtrl(value);
    case GicItsRegister::IIDR:
      return ReadIidr(value);
    case GicItsRegister::TYPER:
      return ReadTyper(value);
    case GicItsRegister::CBASER:
      value->u64 = cbaser_;
      return ZX_OK;
    case GicItsRegister::CWRITER: {
      std::lock_guard<std::mutex> lock(cmd_mutex_);
      value->u64 = cwriter_;
      return ZX_OK;
    }
    case GicItsRegister::CREADER: {
      std::lock_guard<std::mutex> lock(cmd_mutex_);
      value->u64 = creader_;
      return ZX_OK;
    }
    case GicItsRegister::BASER... GicItsRegister::BASER7:
      return ReadBaser(addr, value);
    case GicItsRegister::IDREGS_BASE... GicItsRegister::CIDR3:
      value->u32 = ReadIdRegs(addr, value);
      return ZX_OK;
    default:
      break;
  }

  return ZX_OK;
}

zx_status_t GicIts::WriteCtrl(const IoValue& value) {
  uint32_t val = value.u32;

  std::lock_guard<std::mutex> lock(cmd_mutex_);
  if (!enabled_ && (val & GITS_CTLR_ENABLE) &&
      (!(baser_device_table_ & GITS_BASER_VALID) ||
       !(baser_coll_table_ & GITS_BASER_VALID) ||
       !(cbaser_ & GITS_CBASER_VALID)))
    goto out;

  enabled_ = !!(val & GITS_CTLR_ENABLE);

  ProcessCmdsLocked();

out:
  return ZX_OK;
}

zx_status_t GicIts::WriteCbaser(const IoValue& value) {
  if (enabled_) {
    return ZX_OK;
  }

  std::lock_guard<std::mutex> lock(cmd_mutex_);
  cbaser_ = value.u64;
  creader_ = 0;
  cwriter_ = creader_;

  return ZX_OK;
}

zx_status_t GicIts::WriteCwriter(const IoValue& value) {
  uint64_t reg;

  std::lock_guard<std::mutex> lock(cmd_mutex_);
  reg = value.u64;
  reg = ITS_CMD_OFFSET(reg);
  if (reg >= ITS_CMD_BUFFER_SIZE(cbaser_)) {
    return ZX_OK;
  }
  cwriter_ = reg;

  ProcessCmdsLocked();

  return ZX_OK;
}

void GicIts::ClearDevicItsCollectionLocked(uint32_t col_id) {
  for (auto device_iter = its_devices_.begin();
       device_iter != its_devices_.end(); device_iter++) {
    GicItsDevice* device = device_iter->second.get();
    for (int i = 0; i < device->max_event(); i++) {
      if (device->ittes[i].collection->collection_id == col_id) {
        device->ittes[i].collection = nullptr;
      }
    }
  }
}

void GicIts::FreeCollectionLocked(uint32_t col_id) {
  GicItsCollection* col;

  for (auto col_iter = its_colls_.begin(); col_iter != its_colls_.end();
       col_iter++) {
    col = col_iter->get();
    if (col->collection_id == col_id) {
      ClearDevicItsCollectionLocked(col_id);
      its_colls_.erase(col_iter);
      break;
    }
  }
}

void GicIts::FreeCollectionListLocked() {
  GicItsCollection* col;

  for (auto col_iter = its_colls_.begin(); col_iter != its_colls_.end();
       col_iter++) {
    col = col_iter->get();
    ClearDevicItsCollectionLocked(col->collection_id);
  }

  its_colls_.clear();
}

GicItsCollection* GicIts::FindCollection(uint16_t col_id) {
  GicItsCollection* col;

  for (auto col_iter = its_colls_.begin(); col_iter != its_colls_.end();
       col_iter++) {
    col = col_iter->get();
    if (col->collection_id == col_id) {
      return col;
    }
  }

  return nullptr;
}

#define GITS_BASER_RO_MASK (GENMASK_ULL(52, 48) | GENMASK_ULL(58, 56))
zx_status_t GicIts::WriteBaser(uint64_t addr, const IoValue& value) {
  uint64_t reg;
  uint64_t* regptr;
  uint64_t clearbits = GITS_BASER_INDIRECT;
  uint64_t entry_size;
  uint64_t table_type;

  if (enabled_)
    return ZX_OK;

  switch (BASER_INDEX(addr)) {
    case 0:
      regptr = &baser_device_table_;
      entry_size = kGicItsDteEsz;
      table_type = GITS_BASER_TYPE_DEVICE;
      break;
    case 1:
      regptr = &baser_coll_table_;
      entry_size = kGicItsCteEsz;
      table_type = GITS_BASER_TYPE_COLLECTION;
      break;
    default:
      return ZX_OK;
  }

  reg = value.u64;
  reg &= ~GITS_BASER_RO_MASK;
  reg &= ~clearbits;

  reg |= (entry_size - 1) << GITS_BASER_ENTRY_SIZE_SHIFT;
  reg |= table_type << GITS_BASER_TYPE_SHIFT;
  reg &= (~(GENMASK_ULL(51, 48)));

  *regptr = reg;
  if (!(reg & GITS_BASER_VALID)) {
    std::lock_guard<std::mutex> lock(its_mutex_);
    switch (table_type) {
      case GITS_BASER_TYPE_DEVICE:
        its_devices_.clear();
        break;
      case GITS_BASER_TYPE_COLLECTION:
        FreeCollectionListLocked();
        break;
    }
  }

  return ZX_OK;
}

zx_status_t GicIts::Write(uint64_t addr, const IoValue& value) {
  switch (static_cast<GicItsRegister>(addr)) {
    case GicItsRegister::CTRL:
      return WriteCtrl(value);
    case GicItsRegister::CBASER:
      return WriteCbaser(value);
    case GicItsRegister::CWRITER:
      return WriteCwriter(value);
    case GicItsRegister::BASER... GicItsRegister::BASER7:
      return WriteBaser(addr, value);
    case GicItsRegister::IDREGS_BASE... GicItsRegister::CIDR3:
    case GicItsRegister::IIDR:
    case GicItsRegister::TYPER:
    case GicItsRegister::CREADER:
      break;
    default:
      break;
  }

  return ZX_OK;
}

static uint64_t its_cmd_mask_field(uint64_t* its_cmd,
                                   int word,
                                   int shift,
                                   int size) {
  return (its_cmd[word] >> shift) & (BIT_ULL(size) - 1);
}

#define its_cmd_get_command(cmd) its_cmd_mask_field(cmd, 0, 0, 8)
#define its_cmd_get_deviceid(cmd) its_cmd_mask_field(cmd, 0, 32, 32)
#define its_cmd_get_size(cmd) (its_cmd_mask_field(cmd, 1, 0, 5) + 1)
#define its_cmd_get_id(cmd) its_cmd_mask_field(cmd, 1, 0, 32)
#define its_cmd_get_physical_id(cmd) its_cmd_mask_field(cmd, 1, 32, 32)
#define its_cmd_get_collection(cmd) its_cmd_mask_field(cmd, 2, 0, 16)
#define its_cmd_get_ittaddr(cmd) (its_cmd_mask_field(cmd, 2, 8, 44) << 8)
#define its_cmd_get_target_addr(cmd) its_cmd_mask_field(cmd, 2, 16, 32)
#define its_cmd_get_validbit(cmd) its_cmd_mask_field(cmd, 2, 63, 1)

bool GicIts::ItsCheckId(uint64_t baser, uint32_t id, uint64_t* eaddr) {
  int l1_tbl_size = GITS_BASER_NR_PAGES(baser) * SZ_64K;
  uint64_t type = GITS_BASER_TYPE(baser);
  int esz = GITS_BASER_ENTRY_SIZE(baser);

  switch (type) {
    case GITS_BASER_TYPE_DEVICE:
      if (id >= BIT_ULL(kGicItsDevBits)) {
        FXL_LOG(ERROR) << "Invalid device id:" << id;
        return false;
      }
      break;
    case GITS_BASER_TYPE_COLLECTION:
      /* as GITS_TYPER.CIL == 0, ITS supports 16-bit collection ID */
      if (id >= BIT_ULL(16)) {
        FXL_LOG(ERROR) << "Invalidd collection id:" << id;
        return false;
      }
      break;
    default:
      return false;
  }

  if (baser & GITS_BASER_INDIRECT) {
    FXL_LOG(ERROR) << "Indirect table is not support, type:" << type;
    return false;
  }

  uint64_t addr;

  if (id >= (l1_tbl_size / esz))
    return false;

  addr = BASER_ADDRESS(baser) + id * esz;

  if (eaddr)
    *eaddr = addr;
  return guest_->GpaValid(addr, esz);
}

std::unique_ptr<GicItsDevice> GicIts::ItsAllocDevice(uint32_t device_id,
                                                     uint64_t its_addr,
                                                     uint8_t num_eventid_bits) {
  std::unique_ptr<GicItsDevice> device = std::make_unique<GicItsDevice>();
  device->device_id = device_id;
  device->itt_addr = its_addr;
  device->eventid_bits = num_eventid_bits;
  return device;
}

void GicIts::ProcessMapdCmd(uint64_t* its_cmd) {
  uint32_t device_id = its_cmd_get_deviceid(its_cmd);
  bool valid = its_cmd_get_validbit(its_cmd);
  uint8_t num_eventid_bits = its_cmd_get_size(its_cmd);
  uint64_t itt_addr = its_cmd_get_ittaddr(its_cmd);

  if (!ItsCheckId(baser_device_table_, device_id, nullptr)) {
    FXL_LOG(ERROR) << "Failed to check device id:" << device_id;
    return;
  }

  if (valid && (num_eventid_bits > kGicItsIdBits)) {
    FXL_LOG(ERROR) << "Invalid event id bits:" << num_eventid_bits;
    return;
  }

  auto device_iter = its_devices_.find(device_id);
  if (device_iter != its_devices_.end()) {
    its_devices_.erase(device_iter);
  }

  if (!valid) {
    return;
  }

  std::unique_ptr<GicItsDevice> device =
      ItsAllocDevice(device_id, itt_addr, num_eventid_bits);
  its_devices_.emplace(device_id, std::move(device));
}

static bool GicItsCollectionMapped(GicItsCollection* collection) {
  return collection->target_addr != COLLECTION_NOT_MAPPED;
}

static void UpdateAffinity(GicLpiIrq* lpi, GicItsCollection* collection) {
  lpi->target_vcpu = collection->target_addr;
}

int GicIts::ItsAllocCollection(GicItsCollection** collection, uint16_t col_id) {
  std::unique_ptr<GicItsCollection> col;

  col = std::make_unique<GicItsCollection>();
  if (!col) {
    return ZX_ERR_NO_MEMORY;
  }

  col->collection_id = col_id;
  col->target_addr = COLLECTION_NOT_MAPPED;
  *collection = col.get();
  its_colls_.push_back(std::move(col));

  return ZX_OK;
}

void GicIts::UpdateAffinityCollection(GicItsCollection* collection) {
  if (!collection) {
    return;
  }

  if (GicItsCollectionMapped(collection)) {
    return;
  }

  for (auto device_iter = its_devices_.begin();
       device_iter != its_devices_.end(); device_iter++) {
    GicItsDevice* device = device_iter->second.get();
    for (int i = 0; i < device->max_event(); i++) {
      if (device->ittes[i].collection != collection) {
        continue;
      }
      UpdateAffinity(device->ittes[i].lpi, collection);
    }
  }
}

void GicIts::ProcessMapcCmd(uint64_t* its_cmd) {
  uint16_t coll_id;
  uint32_t target_addr;
  GicItsCollection* collection;
  bool valid;
  int ret;

  valid = its_cmd_get_validbit(its_cmd);
  coll_id = its_cmd_get_collection(its_cmd);
  target_addr = its_cmd_get_target_addr(its_cmd);

  if (!(interrupt_controller_->GetSPIVcpuMask() & (1 << target_addr))) {
    std::vector<int> spi_vcpus = interrupt_controller_->GetSPIVcpus();
    if (spi_vcpus.size() > 0) {
      target_addr = spi_vcpus[0];
    }
  }

  if (target_addr >= guest_->vcpus().size()) {
    FXL_LOG(ERROR) << "Invalid target addr:" << target_addr;
    return;
  }

  if (!valid) {
    FreeCollectionLocked(coll_id);
  } else {
    collection = FindCollection(coll_id);

    if (!collection) {
      ret = ItsAllocCollection(&collection, coll_id);
      if (ret)
        return;
      collection->target_addr = target_addr;
    } else {
      collection->target_addr = target_addr;
      UpdateAffinityCollection(collection);
    }
  }
}

GicItsItte* GicIts::FindIte(uint32_t device_id, uint32_t event_id) {
  auto device_iter = its_devices_.find(device_id);
  if (device_iter == its_devices_.end()) {
    return nullptr;
  }

  GicItsDevice* device = device_iter->second.get();
  for (int i = 0; i < device->max_event(); i++) {
    if (device->ittes[i].event_id == event_id) {
      return &device->ittes[i];
    }
  }

  return nullptr;
}

GicItsItte* GicIts::ItsAllocIte(GicItsDevice* device,
                                GicItsCollection* collection,
                                uint32_t event_id) {
  if (event_id >= device->max_event()) {
    FXL_LOG(ERROR) << "Invalid event id:" << event_id;
    return nullptr;
  }

  GicItsItte* ite = &device->ittes[event_id];
  ite->event_id = event_id;
  ite->collection = collection;

  return ite;
}

void GicIts::ProcessMapiCmd(uint64_t* its_cmd) {
  uint32_t device_id = its_cmd_get_deviceid(its_cmd);
  uint32_t event_id = its_cmd_get_id(its_cmd);
  uint32_t coll_id = its_cmd_get_collection(its_cmd);
  GicItsItte* ite;
  GicItsDevice* device;
  GicItsCollection *collection, *new_coll = NULL;
  int lpi_nr;

  auto device_iter = its_devices_.find(device_id);
  if (device_iter == its_devices_.end()) {
    FXL_LOG(ERROR) << "Invalid device id:" << device_id;
    return;
  }

  device = device_iter->second.get();
  if (event_id >= device->max_event()) {
    FXL_LOG(ERROR) << "Invalid event id:" << event_id;
    return;
  }

  if (its_cmd_get_command(its_cmd) == GITS_CMD_MAPTI)
    lpi_nr = its_cmd_get_physical_id(its_cmd);
  else
    lpi_nr = event_id;
  if (lpi_nr < GIC_LPI_OFFSET) {
    FXL_LOG(ERROR) << "Invalid lpi:" << lpi_nr;
    return;
  }

  /* If there is an existing mapping, behavior is UNPREDICTABLE. */
  if (FindIte(device_id, event_id))
    return;

  collection = FindCollection(coll_id);
  if (!collection) {
    int ret = ItsAllocCollection(&collection, coll_id);
    if (ret)
      return;
    new_coll = collection;
  }

  ite = ItsAllocIte(device, collection, event_id);
  if (!ite) {
    if (new_coll)
      FreeCollectionLocked(coll_id);
    return;
  }

  std::unique_ptr<GicLpiIrq> lpi = std::make_unique<GicLpiIrq>();
  lpi->irq = lpi_nr;
  ite->lpi = lpi.get();
  UpdateAffinity(lpi.get(), collection);

  gic_lpis_.push_back(std::move(lpi));
}

void GicIts::ProcessMoviCmd(uint64_t* its_cmd) {
  uint32_t device_id = its_cmd_get_deviceid(its_cmd);
  uint32_t event_id = its_cmd_get_id(its_cmd);
  uint32_t coll_id = its_cmd_get_collection(its_cmd);
  GicItsItte* ite;
  GicItsCollection* collection;

  ite = FindIte(device_id, event_id);
  if (!ite) {
    FXL_LOG(ERROR) << "Failed to find ite, device_id:" << device_id
                   << ", event_id:" << event_id;
    return;
  }

  if (!GicItsCollectionMapped(ite->collection)) {
    FXL_LOG(ERROR) << "Ite's collection is not mapped, device_id:" << device_id
                   << ", event_id:" << event_id;
    return;
  }

  collection = FindCollection(coll_id);
  FXL_CHECK(collection != nullptr) << "invalid collection id:" << coll_id;

  if (!GicItsCollectionMapped(collection)) {
    FXL_LOG(ERROR) << "Collection is not mapped, collection_id:" << coll_id;
    return;
  }

  ite->collection = collection;
  UpdateAffinity(ite->lpi, collection);
}

void GicIts::ProcessDiscardCmd(uint64_t* its_cmd) {
  uint32_t device_id = its_cmd_get_deviceid(its_cmd);
  uint32_t event_id = its_cmd_get_id(its_cmd);
  GicItsItte* ite;

  ite = FindIte(device_id, event_id);
  if (ite && ite->collection) {
    ite->event_id = (uint32_t)-1;
    ite->collection = nullptr;
  }
}

void GicIts::ProcessClearCmd(uint64_t* its_cmd) {
  // Move peding state is not supported.
}

void GicIts::ProcessMoveallCmd(uint64_t* its_cmd) {
  uint32_t target1_addr = its_cmd_get_target_addr(its_cmd);
  uint32_t target2_addr = its_cmd_mask_field(its_cmd, 3, 16, 32);

  if (target1_addr >= guest_->vcpus().size() ||
      target2_addr >= guest_->vcpus().size()) {
    FXL_LOG(ERROR) << "Invalid target addr:" << target1_addr << ","
                   << target2_addr;
    return;
  }

  if (target1_addr == target2_addr)
    return;

  for (auto lpi_iter = gic_lpis_.begin(); lpi_iter != gic_lpis_.end();
       lpi_iter++) {
    if ((*lpi_iter)->target_vcpu == target1_addr) {
      (*lpi_iter)->target_vcpu = target2_addr;
    }
  }
}

void GicIts::ProcessIntCmd(uint64_t* its_cmd) {
  uint32_t msi_data = its_cmd_get_id(its_cmd);
  uint64_t msi_devid = its_cmd_get_deviceid(its_cmd);

  InjectMsiLocked(msi_devid, msi_data);
}

#define LPI_PROP_ENABLE_BIT(p) ((p)&LPI_PROP_ENABLED)

void GicIts::UpdateLpiConfig(GicLpiIrq* lpi, int filter_vcpu) {
  uint64_t propbase;
  uint64_t propaddr;
  uint8_t* prop;

  propbase = interrupt_controller_->PropBaser();
  propaddr = propbase & GENMASK_ULL(51, 12);
  prop = guest_->Gpa2va<uint8_t>(propaddr + lpi->irq - GIC_LPI_OFFSET);

  if ((filter_vcpu == -1) || filter_vcpu == lpi->target_vcpu) {
    lpi->enabled = LPI_PROP_ENABLE_BIT(*prop);
  }
}

void GicIts::ProcessInvCmd(uint64_t* its_cmd) {
  uint32_t device_id = its_cmd_get_deviceid(its_cmd);
  uint32_t event_id = its_cmd_get_id(its_cmd);
  GicItsItte* ite;

  ite = FindIte(device_id, event_id);
  if (!ite) {
    FXL_LOG(ERROR) << "failed to find valid ite, device_id:" << device_id
                   << ", event_id:" << event_id;
    return;
  }

  UpdateLpiConfig(ite->lpi, -1);
}

void GicIts::ProcessInvAllCmd(uint64_t* its_cmd) {
  uint32_t coll_id = its_cmd_get_collection(its_cmd);
  GicItsCollection* collection;
  GicLpiIrq* lpi;

  collection = FindCollection(coll_id);
  FXL_CHECK(collection != nullptr) << "invalid collection id:" << coll_id;

  if (!GicItsCollectionMapped(collection)) {
    FXL_LOG(ERROR) << "Collection is invalid:" << coll_id;
    return;
  }

  for (auto lpi_iter = gic_lpis_.begin(); lpi_iter != gic_lpis_.end();
       lpi_iter++) {
    lpi = (*lpi_iter).get();
    if (lpi->target_vcpu == collection->target_addr) {
      UpdateLpiConfig(lpi, collection->target_addr);
    }
  }
}

void GicIts::ProcessItsCmd(GicItsCmd* cmd) {
  std::lock_guard<std::mutex> lock(its_mutex_);

  switch (its_cmd_get_command(cmd->cmd_buf)) {
    case GITS_CMD_MAPD:
      ProcessMapdCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_MAPC:
      ProcessMapcCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_MAPI:
      ProcessMapiCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_MAPTI:
      ProcessMapiCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_MOVI:
      ProcessMoviCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_DISCARD:
      ProcessDiscardCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_CLEAR:
      ProcessClearCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_MOVALL:
      ProcessMoveallCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_INT:
      ProcessIntCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_INV:
      ProcessInvCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_INVALL:
      ProcessInvAllCmd(cmd->cmd_buf);
      break;
    case GITS_CMD_SYNC:
      break;
  }
}

void GicIts::ProcessCmdsLocked() {
  uint64_t cbaser_addr;
  GicItsCmd* cmd;

  if (!enabled_) {
    return;
  }

  cbaser_addr = CBASER_ADDRESS(cbaser_);

  while (cwriter_ != creader_) {
    cmd = guest_->Gpa2va<GicItsCmd>(cbaser_addr + creader_);
    ProcessItsCmd(cmd);

    creader_ += ITS_CMD_SIZE;
    if (creader_ == ITS_CMD_BUFFER_SIZE(cbaser_))
      creader_ = 0;
  }
}

zx_status_t GicIts::ResolveLpiLocked(uint64_t devid,
                                     uint16_t evt_inx,
                                     GicLpiIrq** lpi) {
  if (!enabled_) {
    FXL_LOG(ERROR) << "Its is disabled.";
    return ZX_ERR_BAD_STATE;
  }

  GicItsItte* ite = FindIte(devid, evt_inx);
  if (ite == nullptr) {
    FXL_LOG(ERROR) << "Invalid id, devid:" << std::hex << devid << ", evt_inx:" << evt_inx;
    return ZX_ERR_INVALID_ARGS;
  }

  if (!GicItsCollectionMapped(ite->collection)) {
    FXL_LOG(ERROR) << "Colloection unmapped, colid:"
                   << ite->collection->collection_id;
    return ZX_ERR_BAD_STATE;
  }

  if (!interrupt_controller_->LpisEnabled()) {
    FXL_LOG(ERROR) << "Lpi is disabled";
    return ZX_ERR_BAD_STATE;
  }

  *lpi = ite->lpi;

  return ZX_OK;
}

zx_status_t GicIts::InjectMsiLocked(uint64_t devid, uint16_t evt_inx) {
  GicLpiIrq* lpi;

  zx_status_t status = ResolveLpiLocked(devid, evt_inx, &lpi);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to resolve irq, status:" << status
                   << ", dev_id:" << devid << ", evt_inx:" << evt_inx;
    return status;
  }

  if (lpi->target_vcpu >= guest_->vcpus().size()) {
    FXL_LOG(ERROR) << "Invalid lpi target, devid:" << devid << ", evt_inx"
                   << evt_inx;
    return ZX_ERR_INVALID_ARGS;
  }

  if (!lpi->enabled) {
    FXL_LOG(WARNING) << "lpi is not enabled, devid:" << devid
                     << ", lpi:" << lpi->irq;
    return ZX_ERR_BAD_STATE;
  }

  return guest_->SignalLpiInterrupt(1 << lpi->target_vcpu, lpi->irq);
}

zx_status_t GicIts::InjectMsi(uint64_t devid, uint16_t evt_inx) {
  GicLpiIrq* lpi;
  {
    std::lock_guard<std::mutex> lock(its_mutex_);
    zx_status_t status = ResolveLpiLocked(devid, evt_inx, &lpi);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to resolve irq, status:" << status
                     << ", dev_id:" << devid << ", evt_inx:" << evt_inx;
      return status;
    }

    if (!lpi->enabled) {
      FXL_LOG(WARNING) << "lpi is not enabled, devid:" << devid
                       << ", lpi:" << lpi->irq;
      return ZX_ERR_BAD_STATE;
    }
  }

  if (lpi->target_vcpu >= guest_->vcpus().size()) {
    FXL_LOG(ERROR) << "Invalid lpi target, devid:" << devid << ", evt_inx"
                   << evt_inx;
    return ZX_ERR_INVALID_ARGS;
  }

  return guest_->SignalLpiInterrupt(1 << lpi->target_vcpu, lpi->irq);
}

void GicIts::InvalidLpiConfig(uint32_t irq) {
  std::lock_guard<std::mutex> lock(its_mutex_);
  GicLpiIrq* lpi;

  for (auto lpi_iter = gic_lpis_.begin(); lpi_iter != gic_lpis_.end();
       lpi_iter++) {
    lpi = (*lpi_iter).get();
    if (lpi->irq == irq) {
      UpdateLpiConfig(lpi, -1);
    }
  }
}

}  // namespace machina
