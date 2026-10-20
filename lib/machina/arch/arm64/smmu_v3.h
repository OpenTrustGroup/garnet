// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smmu_v3_internal.h"

#include <sys/types.h>

#include "lib/async-loop/cpp/loop.h"
#include "lib/async/cpp/iommu_receiver.h"

#include "garnet/bin/guest/guest_config.h"
#include "garnet/lib/machina/io.h"

typedef struct SMMUIOTLBPageInvInfo {
  int asid;
  int vmid;
  uint64_t iova;
  uint64_t mask;
} SMMUIOTLBPageInvInfo;

typedef struct SMMUSIDRange {
  SMMUState* state;
  uint32_t start;
  uint32_t end;
} SMMUSIDRange;

/**
 * struct iommu_hwpt_arm_smmuv3 - ARM SMMUv3 Context Descriptor Table info
 *                                (IOMMU_HWPT_DATA_ARM_SMMUV3)
 *
 * @ste: The first two double words of the user space Stream Table Entry for
 *       a user stage-1 Context Descriptor Table. Must be little-endian.
 *       Allowed fields: (Refer to "5.2 Stream Table Entry" in SMMUv3 HW Spec)
 *       - word-0: V, S1Fmt, S1ContextPtr, S1CDMax
 *       - word-1: S1DSS, S1CIR, S1COR, S1CSH, S1STALLD
 * @sid: The user space Stream ID to index the user Stream Table Entry @ste
 *
 * -EIO will be returned if @ste is not legal or contains any non-allowed field.
 */
struct iommu_hwpt_arm_smmuv3 {
  uint64_t ste[2];
  uint32_t sid;
};

/**
 * struct iommu_hwpt_arm_smmuv3_invalidate - ARM SMMUv3 cahce invalidation
 *                                           (IOMMU_HWPT_DATA_ARM_SMMUV3)
 * @cmd: 128-bit cache invalidation command that runs in SMMU CMDQ.
 *       Must be little-endian.
 *
 * Supported command list:
 *     CMDQ_OP_TLBI_NSNH_ALL
 *     CMDQ_OP_TLBI_NH_VA
 *     CMDQ_OP_TLBI_NH_VAA
 *     CMDQ_OP_TLBI_NH_ALL
 *     CMDQ_OP_TLBI_NH_ASID
 *     CMDQ_OP_ATC_INV
 *     CMDQ_OP_CFGI_CD
 *     CMDQ_OP_CFGI_CD_ALL
 */
struct iommu_hwpt_arm_smmuv3_invalidate {
  uint64_t cmd[2];
};

#define MM_SMMU_IO_BASE (0x30800000)
#define APU_SMMU_IO_BASE (0x4C000000)
#define SOC_SMMU_IO_BASE (0x14900000)
#define GPU_SMMU_IO_BASE (0x48600000)

enum mtk_smmu_type {
  MM_SMMU = 0,
  APU_SMMU = 1,
  SOC_SMMU = 2,
  GPU_SMMU = 3,
  SMMU_TYPE_NUM
};

namespace machina {

class Smmuv3 : public IoHandler {
 public:
  Smmuv3(Guest* guest, uint8_t type);

  ~Smmuv3();

  zx_status_t Init(const VsmmuSpec& spec);

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  zx_status_t Interrupt(uint32_t global_irq);

  static mtk_smmu_type GetSmmuType(uint64_t paddr);

 private:
  void trigger_irq(SMMUIrq irq, uint32_t gerror_mask);
  void write_gerrorn(uint32_t new_gerrorn);

  void get_ste(dma_addr_t addr, STE* ste, SMMUEventInfo* event);
  zx_status_t find_ste(uint32_t sid, STE* ste, SMMUEventInfo* event);

  void config_ste(SMMUDevice* sdev, uint32_t sid);
  SMMUDevice* find_sdev(uint32_t sid);
  void flush_config(SMMUDevice* sdev);

  zx_status_t queue_read(SMMUQueue* q, Cmd* cmd);
  zx_status_t queue_write(SMMUQueue* q, Evt* evt_in);
  zx_status_t write_eventq(Evt* evt);
  zx_status_t cmdq_consume();

  zx_status_t writell(uint64_t offset, uint64_t data);
  zx_status_t writel(uint64_t offset, uint64_t data);
  zx_status_t write_mmio(uint64_t offset, uint64_t data, unsigned size);
  zx_status_t readll(uint64_t offset, uint64_t* data) const;
  zx_status_t readl(uint64_t offset, uint64_t* data) const;
  zx_status_t read_mmio(uint64_t offset, uint64_t* data, unsigned size) const;

  zx_status_t install_nested_ste(SMMUDevice* sdev,
                                 uint32_t data_len,
                                 void* data);
  void uninstall_nested_ste(SMMUDevice* sdev);

  zx_status_t invalidate_cache(Cmd* cmds, uint32_t* ncmds, uint32_t* cmd_error);

  Guest* guest_;

  SMMUv3State state_;
  zx_handle_t iommu_;

 private:
  mutable bool initialized_ = false;
  std::vector<uint16_t> sids_;
  mutable uint8_t smmu_type;

  void OnNewEvent(async_t* async, const zx_packet_iommu_event_t* event);
  async::IommuReceiverMethod<Smmuv3, &Smmuv3::OnNewEvent> async_receiver_;
  async::Loop loop_;
  uint32_t combined_irq_;
};
}  // namespace machina
