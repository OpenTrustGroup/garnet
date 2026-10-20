// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smmu_v3.h"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <zircon/device/sysinfo.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/iommu.h>

#include "garnet/lib/machina/guest.h"
#include "lib/fxl/logging.h"

namespace machina {

#define le32_to_cpus(data)
#define le64_to_cpus(data)
#define cpu_to_le32s(data)
#define cpu_to_le64s(data)

static constexpr size_t kSmmuv3Size = 0x80000;

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";

static zx_status_t get_root_resource(zx::resource* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  resource->reset(rsc_handle);
  close(fd);
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

void Smmuv3::trigger_irq(SMMUIrq irq, uint32_t gerror_mask) {
  auto s = &state_;
  bool pulse = false;

  switch (irq) {
    case SMMU_IRQ_EVTQ:
      pulse = smmuv3_eventq_irq_enabled(s);
      break;
    case SMMU_IRQ_PRIQ:
      FXL_LOG(ERROR) << "PRI not yet supported";
      break;
    case SMMU_IRQ_CMD_SYNC:
      pulse = true;
      break;
    case SMMU_IRQ_GERROR: {
      uint32_t pending = s->gerror ^ s->gerrorn;
      uint32_t new_gerrors = ~pending & gerror_mask;

      if (!new_gerrors) {
        /* only toggle non pending errors */
        return;
      }
      s->gerror ^= new_gerrors;

      pulse = smmuv3_gerror_irq_enabled(s);
      break;
    }
  }
  if (pulse) {
    guest_->SignalInterrupt(/*cpumask=*/0x1, combined_irq_);
  }
}

void Smmuv3::write_gerrorn(uint32_t new_gerrorn) {
  auto s = &state_;
  uint32_t pending = s->gerror ^ s->gerrorn;
  uint32_t toggled = s->gerrorn ^ new_gerrorn;

  if (toggled & ~pending) {
    FXL_LOG(ERROR) << "guest toggles non pending errors = 0x" << std::hex
                   << (toggled & ~pending);
  }

  /*
   * We do not raise any error in case guest toggles bits corresponding
   * to not active IRQs (CONSTRAINED UNPREDICTABLE)
   */
  s->gerrorn = new_gerrorn;
}

zx_status_t Smmuv3::queue_read(SMMUQueue* q, Cmd* out_cmd) {
  dma_addr_t addr = Q_CONS_ENTRY(q);
  int i;

  auto& guest_mem = guest_->phys_mem();
  auto offset = addr - guest_mem.phys_base();
  auto cmd = guest_mem.as<Cmd>(offset);

  for (i = 0; i < ARRAY_SIZE(cmd->word); i++) {
    out_cmd->word[i] = cmd->word[i];
    le32_to_cpus(&out_cmd->word[i]);
  }

  return ZX_OK;
}

zx_status_t Smmuv3::queue_write(SMMUQueue* q, Evt* evt_in) {
  dma_addr_t addr = Q_PROD_ENTRY(q);
  int i;

  auto& guest_mem = guest_->phys_mem();
  auto offset = addr - guest_mem.phys_base();
  auto evt = guest_mem.as<Cmd>(offset);
  for (i = 0; i < ARRAY_SIZE(evt_in->word); i++) {
    evt->word[i] = evt_in->word[i];
    cpu_to_le32s(&evt->word[i]);
  }

  queue_prod_incr(q);
  return ZX_OK;
}

zx_status_t Smmuv3::write_eventq(Evt* evt) {
  SMMUv3State* s = &state_;
  SMMUQueue* q = &s->eventq;
  zx_status_t r;

  if (!smmuv3_eventq_enabled(s)) {
    FXL_LOG(ERROR) << "smmuv3_eventq_enabled fail";
    return ZX_ERR_IO;
  }

  if (smmuv3_q_full(q)) {
    FXL_LOG(ERROR) << "smmuv3_q_full fail";

    return ZX_ERR_IO;
  }

  r = queue_write(q, evt);
  if (r != ZX_OK) {
    FXL_LOG(ERROR) << "queue_write fail";
    return r;
  }

  if (!smmuv3_q_empty(q)) {
    // FXL_LOG(ERROR) << "trigger_irq " ;
    trigger_irq(SMMU_IRQ_EVTQ, 0);
  }
  return ZX_OK;
}

void Smmuv3::get_ste(dma_addr_t addr, STE* ste, SMMUEventInfo* event) {
  int i;
  auto& guest_mem = guest_->phys_mem();
  auto offset = addr - guest_mem.phys_base();
  auto buf = guest_mem.as<STE>(offset);

  for (i = 0; i < ARRAY_SIZE(buf->word); i++) {
    le32_to_cpus(&buf->word[i]);
  }

  *ste = *buf;
}

zx_status_t Smmuv3::find_ste(uint32_t sid, STE* ste, SMMUEventInfo* event) {
  auto s = &state_;
  dma_addr_t addr, strtab_base;
  uint32_t log2size;
  int strtab_size_shift;

  log2size = FIELD_EX32(s->strtab_base_cfg, STRTAB_BASE_CFG, LOG2SIZE);
  /*
   * Check SID range against both guest-configured and implementation limits
   */
  if (sid >= (1 << MIN(log2size, SMMU_IDR1_SIDSIZE))) {
    event->type = SMMU_EVT_C_BAD_STREAMID;
    return ZX_ERR_INVALID_ARGS;
  }
  if (s->features & SMMU_FEATURE_2LVL_STE) {
    int l1_ste_offset, l2_ste_offset, max_l2_ste, span, i;
    dma_addr_t l1ptr, l2ptr;
    STEDesc* l1std;

    /*
     * Align strtab base address to table size. For this purpose, assume it
     * is not bounded by SMMU_IDR1_SIDSIZE.
     */
    strtab_size_shift = MAX(5, (int)log2size - s->sid_split - 1 + 3);
    strtab_base = s->strtab_base & SMMU_BASE_ADDR_MASK &
                  ~MAKE_64BIT_MASK(0, strtab_size_shift);
    l1_ste_offset = sid >> s->sid_split;
    l2_ste_offset = sid & ((1 << s->sid_split) - 1);
    l1ptr = (dma_addr_t)(strtab_base + l1_ste_offset * sizeof(*l1std));

    auto& guest_mem = guest_->phys_mem();
    auto offset = l1ptr - guest_mem.phys_base();
    l1std = guest_mem.as<STEDesc>(offset);

    for (i = 0; i < ARRAY_SIZE(l1std->word); i++) {
      le32_to_cpus(l1std->word[i]);
    }

    span = L1STD_SPAN(l1std);

    if (!span) {
      /* l2ptr is not valid */
      if (!event->inval_ste_allowed) {
        FXL_LOG(ERROR) << "invalid sid=" << sid << "(L1STD span=0)\n", sid;
      }
      event->type = SMMU_EVT_C_BAD_STREAMID;
      return ZX_ERR_INVALID_ARGS;
    }
    max_l2_ste = (1 << span) - 1;
    l2ptr = l1std_l2ptr(l1std);
    if (l2_ste_offset > max_l2_ste) {
      FXL_LOG(ERROR) << "l2_ste_offset=" << l2_ste_offset << ", max_l2_ste=",
          max_l2_ste;
      event->type = SMMU_EVT_C_BAD_STE;
      return ZX_ERR_INVALID_ARGS;
    }
    addr = l2ptr + l2_ste_offset * sizeof(*ste);
  } else {
    strtab_size_shift = log2size + 5;
    strtab_base = s->strtab_base & SMMU_BASE_ADDR_MASK &
                  ~MAKE_64BIT_MASK(0, strtab_size_shift);
    addr = strtab_base + sid * sizeof(*ste);
  }

  get_ste(addr, ste, event);
  return 0;
}

void Smmuv3::uninstall_nested_ste(SMMUDevice* sdev) {
  SMMUHwpt* hwpt = sdev->hwpt;

  if (!sdev || !hwpt) {
    return;
  }

  sdev->bti.release_hwpt(hwpt->hwpt_id);
  free(hwpt);
  sdev->hwpt = NULL;
}

zx_status_t Smmuv3::install_nested_ste(SMMUDevice* sdev,
                                       uint32_t data_len,
                                       void* data) {
  auto bs = &state_.smmu_state;
  SMMUHwpt* hwpt = sdev->hwpt;
  zx_status_t status;

  if (!bs || !sdev) {
    return ZX_ERR_NOT_FOUND;
  }

  if (hwpt) {
    uninstall_nested_ste(sdev);
  }

  hwpt = (SMMUHwpt*)malloc(sizeof(*hwpt));
  if (!hwpt) {
    return ZX_ERR_NO_MEMORY;
  }

  hwpt->smmu = sdev->smmu;
  status = sdev->bti.alloc_hwpt(data, data_len, &hwpt->hwpt_id);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to allocate stage-1 HW pagetable: " << status;
    goto free;
  }

  status = sdev->bti.attach_hwpt(hwpt->hwpt_id);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to attach dev to stage-1 HW pagetable: "
                   << status;
    goto free_hwpt;
  }

  sdev->hwpt = hwpt;

  return ZX_OK;

free_hwpt:
  sdev->bti.release_hwpt(hwpt->hwpt_id);
free:
  free(hwpt);
  sdev->hwpt = NULL;

  return status;
}

void Smmuv3::config_ste(SMMUDevice* sdev, uint32_t sid) {
  SMMUEventInfo event = {
      .type = SMMU_EVT_NONE, .sid = sid, .inval_ste_allowed = true};
  struct iommu_hwpt_arm_smmuv3 iommu_config = {};
  uint32_t config;
  STE ste;
  zx_status_t status;

  status = find_ste(sid, &ste, &event);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to find Stream Table Entry: " << status;
    return;
  }

  config = STE_CONFIG(&ste);
  if (!STE_VALID(&ste) || !STE_CFG_S1_ENABLED(config)) {
    uninstall_nested_ste(sdev);
    return;
  }

  iommu_config.sid = sid;
  iommu_config.ste[0] = (uint64_t)ste.word[0] | (uint64_t)ste.word[1] << 32;
  iommu_config.ste[1] = (uint64_t)ste.word[2] | (uint64_t)ste.word[3] << 32;
  /* V | CONFIG | S1FMT | S1CTXPTR | S1CDMAX */
  iommu_config.ste[0] &= 0xf80ffffffffffff1ULL;
  /* S1DSS | S1CIR | S1COR | S1CSH | S1STALLD | STRW */
  iommu_config.ste[1] &= 0xc80000ffULL;

  status = install_nested_ste(sdev, sizeof(iommu_config), &iommu_config);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to install nested STE: " << status;
  }
}

zx_status_t Smmuv3::invalidate_cache(Cmd* cmds,
                                     uint32_t* ncmds,
                                     uint32_t* cmd_error) {
  uint32_t ntlbi = *ncmds;
  auto cmd_len = sizeof(Cmd) * ntlbi;

  auto ret = zx_iommu_hwpt_invalidate(iommu_, cmds, cmd_len, ncmds);

  if (ret != ZX_OK || ntlbi != *ncmds || *cmd_error) {
    FXL_LOG(ERROR) << "failed: ret=" << ret << ", ncmds=" << *ncmds
                   << ", done=" << ntlbi << ", error=0x" << std::hex
                   << *cmd_error;
  }
  return ret;
}

SMMUDevice* Smmuv3::find_sdev(uint32_t sid) {
  auto bs = &state_.smmu_state;
  auto it = bs->devices.find(sid);
  if (it == bs->devices.end())
    return NULL;

  return it->second.get();
}

zx_status_t Smmuv3::cmdq_consume() {
  auto s = &state_;
  SMMUState* bs = &s->smmu_state;
  uint32_t cmd_error = SMMU_CERROR_NONE;
  SMMUQueue* q = &s->cmdq;
  uint32_t type = SMMU_CMD_NONE;
  uint32_t ncmds, ntlbi = 0;
  uint32_t* cons_list;
  Cmd* cmds;

  if (!smmuv3_cmdq_enabled(s)) {
    return ZX_OK;
  }

  ncmds = smmuv3_q_ncmds(q);
  cmds = (Cmd*)malloc(sizeof(*cmds) * ncmds);
  cons_list = (uint32_t*)malloc(sizeof(*cons_list) * ncmds);

  /*
   * some commands depend on register values, typically CR0. In case those
   * register values change while handling the command, spec says it
   * is UNPREDICTABLE whether the command is interpreted under the new
   * or old value.
   */
  while (!smmuv3_q_empty(q)) {
    uint32_t pending = s->gerror ^ s->gerrorn;
    Cmd cmd;

    if (FIELD_EX32(pending, GERROR, CMDQ_ERR)) {
      break;
    }

    if (queue_read(q, &cmd) != ZX_OK) {
      cmd_error = SMMU_CERROR_ABT;
      break;
    }

    type = CMD_TYPE(&cmd);

    s->mutex.lock();
    switch (type) {
      case SMMU_CMD_SYNC:
        if (CMD_SYNC_CS(&cmd) & CMD_SYNC_SIG_IRQ) {
          trigger_irq(SMMU_IRQ_CMD_SYNC, 0);
        }
        break;
      case SMMU_CMD_PREFETCH_CONFIG:
      case SMMU_CMD_PREFETCH_ADDR:
        break;
      case SMMU_CMD_CFGI_STE: {
        uint32_t sid = CMD_SID(&cmd);
        SMMUDevice* sdev = find_sdev(sid);

        if (CMD_SSEC(&cmd)) {
          cmd_error = SMMU_CERROR_ILL;
          break;
        }

        if (!sdev) {
          break;
        }

        config_ste(sdev, sid);

        break;
      }
      case SMMU_CMD_CFGI_STE_RANGE: /* same as SMMU_CMD_CFGI_ALL */
      {
        uint32_t sid = CMD_SID(&cmd), mask;
        uint8_t range = CMD_STE_RANGE(&cmd);
        SMMUSIDRange sid_range;

        if (CMD_SSEC(&cmd)) {
          cmd_error = SMMU_CERROR_ILL;
          break;
        }

        mask = (1ULL << (range + 1)) - 1;
        sid_range.state = bs;
        sid_range.start = sid & ~mask;
        sid_range.end = sid_range.start + mask;

        for (auto& it : bs->devices) {
          auto sdev = it.second.get();
          auto sid = sdev->sid;
          if (sid < sid_range.start || sid > sid_range.end)
            continue;

          config_ste(sdev, sid);
        }
        break;
      }
      case SMMU_CMD_CFGI_CD:
      case SMMU_CMD_CFGI_CD_ALL: {
        uint32_t sid = CMD_SID(&cmd);
        SMMUDevice* sdev = find_sdev(sid);

        if (CMD_SSEC(&cmd)) {
          cmd_error = SMMU_CERROR_ILL;
          break;
        }

        if (!sdev) {
          break;
        }

        if (sdev->hwpt) {
          cons_list[ntlbi] = q->cons;
          cmds[ntlbi++] = cmd;
        }
        break;
      }
      case SMMU_CMD_TLBI_NH_ASID: {
        if (!STAGE1_SUPPORTED(s)) {
          cmd_error = SMMU_CERROR_ILL;
          break;
        }

        cons_list[ntlbi] = q->cons;
        cmds[ntlbi++] = cmd;
        break;
      }
      case SMMU_CMD_TLBI_NH_ALL:
        if (!STAGE1_SUPPORTED(s)) {
          cmd_error = SMMU_CERROR_ILL;
          break;
        }
      // fallthrough
      case SMMU_CMD_TLBI_NSNH_ALL:
        cons_list[ntlbi] = q->cons;
        cmds[ntlbi++] = cmd;
        break;
      case SMMU_CMD_TLBI_NH_VAA:
      case SMMU_CMD_TLBI_NH_VA:
        if (!STAGE1_SUPPORTED(s)) {
          cmd_error = SMMU_CERROR_ILL;
          break;
        }
        cons_list[ntlbi] = q->cons;
        cmds[ntlbi++] = cmd;
        break;
      case SMMU_CMD_ATC_INV: {
        uint32_t sid = CMD_SID(&cmd);
        SMMUDevice* sdev = find_sdev(sid);
        if (sdev->hwpt) {
          cons_list[ntlbi] = q->cons;
          cmds[ntlbi++] = cmd;
        }
        break;
      }
      case SMMU_CMD_TLBI_S12_VMALL:
      case SMMU_CMD_TLBI_S2_IPA:
      case SMMU_CMD_TLBI_EL3_ALL:
      case SMMU_CMD_TLBI_EL3_VA:
      case SMMU_CMD_TLBI_EL2_ALL:
      case SMMU_CMD_TLBI_EL2_ASID:
      case SMMU_CMD_TLBI_EL2_VA:
      case SMMU_CMD_TLBI_EL2_VAA:
      case SMMU_CMD_PRI_RESP:
      case SMMU_CMD_RESUME:
      case SMMU_CMD_STALL_TERM:
        break;
      default:
        cmd_error = SMMU_CERROR_ILL;
        break;
    }
    s->mutex.unlock();
    if (cmd_error) {
      if (cmd_error == SMMU_CERROR_ILL) {
        FXL_LOG(ERROR) << "Illegal command type: 0x" << std::hex
                       << CMD_TYPE(&cmd);
      }
      break;
    }
    /*
     * We only increment the cons index after the completion of
     * the command. We do that because the SYNC returns immediately
     * and does not check the completion of previous commands
     */
    queue_cons_incr(q);
  }
  if (!cmd_error && ntlbi &&
      invalidate_cache(cmds, &ntlbi, &cmd_error) != ZX_OK) {
    q->cons = cons_list[ntlbi];
  }

  if (cmd_error) {
    smmu_write_cmdq_err(s, cmd_error);
    trigger_irq(SMMU_IRQ_GERROR, R_GERROR_CMDQ_ERR_MASK);
  }
  free(cons_list);
  free(cmds);

  return ZX_OK;
}

zx_status_t Smmuv3::writell(uint64_t offset, uint64_t data) {
  auto s = &state_;

  switch (offset) {
    case A_GERROR_IRQ_CFG0:
      s->gerror_irq_cfg0 = data;
      return ZX_OK;
    case A_STRTAB_BASE:
      s->strtab_base = data;
      return ZX_OK;
    case A_CMDQ_BASE:
      s->cmdq.base = data;
      s->cmdq.log2size = extract64(s->cmdq.base, 0, 5);
      if (s->cmdq.log2size > SMMU_CMDQS) {
        s->cmdq.log2size = SMMU_CMDQS;
      }
      return ZX_OK;
    case A_EVENTQ_BASE:
      s->eventq.base = data;
      s->eventq.log2size = extract64(s->eventq.base, 0, 5);
      if (s->eventq.log2size > SMMU_EVENTQS) {
        s->eventq.log2size = SMMU_EVENTQS;
      }
      return ZX_OK;
    case A_EVENTQ_IRQ_CFG0:
      s->eventq_irq_cfg0 = data;
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unexpected 64-bit access to 0x" << std::hex << offset;
      return ZX_OK;
  }
}

zx_status_t Smmuv3::writel(uint64_t offset, uint64_t data) {
  auto s = &state_;
  switch (offset) {
    case A_CR0:
      s->cr[0] = data;
      s->cr0ack = data & ~SMMU_CR0_RESERVED;
      /* in case the command queue has been enabled */
      cmdq_consume();
      return ZX_OK;
    case A_CR1:
      s->cr[1] = data;
      return ZX_OK;
    case A_CR2:
      s->cr[2] = data;
      return ZX_OK;
    case A_IRQ_CTRL:
      s->irq_ctrl = data;
      return ZX_OK;
    case A_GERRORN:
      write_gerrorn(data);
      /*
       * By acknowledging the CMDQ_ERR, SW may notify cmds can
       * be processed again
       */
      cmdq_consume();
      return ZX_OK;
    case A_GERROR_IRQ_CFG0: /* 64b */
      s->gerror_irq_cfg0 = deposit64(s->gerror_irq_cfg0, 0, 32, data);
      return ZX_OK;
    case A_GERROR_IRQ_CFG0 + 4:
      s->gerror_irq_cfg0 = deposit64(s->gerror_irq_cfg0, 32, 32, data);
      return ZX_OK;
    case A_GERROR_IRQ_CFG1:
      s->gerror_irq_cfg1 = data;
      return ZX_OK;
    case A_GERROR_IRQ_CFG2:
      s->gerror_irq_cfg2 = data;
      return ZX_OK;
    case A_GBPA:
      /*
       * If UPDATE is not set, the write is ignored. This is the only
       * permitted behavior in SMMUv3.2 and later.
       */
      if (data & R_GBPA_UPDATE_MASK) {
        /* Ignore update bit as write is synchronous. */
        s->gbpa = data & ~R_GBPA_UPDATE_MASK;
      }
      return ZX_OK;
    case A_STRTAB_BASE: /* 64b */
      s->strtab_base = deposit64(s->strtab_base, 0, 32, data);
      return ZX_OK;
    case A_STRTAB_BASE + 4:
      s->strtab_base = deposit64(s->strtab_base, 32, 32, data);
      return ZX_OK;
    case A_STRTAB_BASE_CFG:
      s->strtab_base_cfg = data;
      if (FIELD_EX32(data, STRTAB_BASE_CFG, FMT) == 1) {
        s->sid_split = FIELD_EX32(data, STRTAB_BASE_CFG, SPLIT);
        s->features |= SMMU_FEATURE_2LVL_STE;
      }
      return ZX_OK;
    case A_CMDQ_BASE: /* 64b */
      s->cmdq.base = deposit64(s->cmdq.base, 0, 32, data);
      s->cmdq.log2size = extract64(s->cmdq.base, 0, 5);
      if (s->cmdq.log2size > SMMU_CMDQS) {
        s->cmdq.log2size = SMMU_CMDQS;
      }
      return ZX_OK;
    case A_CMDQ_BASE + 4: /* 64b */
      s->cmdq.base = deposit64(s->cmdq.base, 32, 32, data);
      return ZX_OK;
    case A_CMDQ_PROD:
      s->cmdq.prod = data;
      cmdq_consume();
      return ZX_OK;
    case A_CMDQ_CONS:
      s->cmdq.cons = data;
      return ZX_OK;
    case A_EVENTQ_BASE: /* 64b */
      s->eventq.base = deposit64(s->eventq.base, 0, 32, data);
      s->eventq.log2size = extract64(s->eventq.base, 0, 5);
      if (s->eventq.log2size > SMMU_EVENTQS) {
        s->eventq.log2size = SMMU_EVENTQS;
      }
      return ZX_OK;
    case A_EVENTQ_BASE + 4:
      s->eventq.base = deposit64(s->eventq.base, 32, 32, data);
      return ZX_OK;
    case A_EVENTQ_PROD:
      s->eventq.prod = data;
      return ZX_OK;
    case A_EVENTQ_CONS:
      s->eventq.cons = data;
      return ZX_OK;
    case A_EVENTQ_IRQ_CFG0: /* 64b */
      s->eventq_irq_cfg0 = deposit64(s->eventq_irq_cfg0, 0, 32, data);
      return ZX_OK;
    case A_EVENTQ_IRQ_CFG0 + 4:
      s->eventq_irq_cfg0 = deposit64(s->eventq_irq_cfg0, 32, 32, data);
      return ZX_OK;
    case A_EVENTQ_IRQ_CFG1:
      s->eventq_irq_cfg1 = data;
      return ZX_OK;
    case A_EVENTQ_IRQ_CFG2:
      s->eventq_irq_cfg2 = data;
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unexpected 32-bit access to 0x" << std::hex << offset;
      return ZX_OK;
  }
}

zx_status_t Smmuv3::write_mmio(uint64_t offset, uint64_t data, unsigned size) {
  zx_status_t r;

  /* CONSTRAINED UNPREDICTABLE choice to have page0/1 be exact aliases */
  offset &= ~0x10000;

  switch (size) {
    case 8:
      r = writell(offset, data);
      break;
    case 4:
      r = writel(offset, data);
      break;
    default:
      r = ZX_ERR_IO;
      break;
  }

  return r;
}

zx_status_t Smmuv3::readll(uint64_t offset, uint64_t* data) const {
  auto s = &state_;

  switch (offset) {
    case A_GERROR_IRQ_CFG0:
      *data = s->gerror_irq_cfg0;
      return ZX_OK;
    case A_STRTAB_BASE:
      *data = s->strtab_base;
      return ZX_OK;
    case A_CMDQ_BASE:
      *data = s->cmdq.base;
      return ZX_OK;
    case A_EVENTQ_BASE:
      *data = s->eventq.base;
      return ZX_OK;
    default:
      *data = 0;
      FXL_LOG(ERROR) << "Unexpected 64-bit access to 0x%" << std::hex << offset;
      return ZX_OK;
  }
}

zx_status_t Smmuv3::readl(uint64_t offset, uint64_t* data) const {
  auto s = &state_;

  switch (offset) {
    case A_IDREGS ... A_IDREGS + 0x2f:
      *data = smmuv3_idreg(offset - A_IDREGS);
      return ZX_OK;
    case A_IDR0 ... A_IDR5:
      *data = s->idr[(offset - A_IDR0) / 4];
      return ZX_OK;
    case A_IIDR:
      *data = s->iidr;
      return ZX_OK;
    case A_AIDR:
      *data = s->aidr;
      return ZX_OK;
    case A_CR0:
      *data = s->cr[0];
      return ZX_OK;
    case A_CR0ACK:
      *data = s->cr0ack;
      return ZX_OK;
    case A_CR1:
      *data = s->cr[1];
      return ZX_OK;
    case A_CR2:
      *data = s->cr[2];
      return ZX_OK;
    case A_STATUSR:
      *data = s->statusr;
      return ZX_OK;
    case A_GBPA:
      *data = s->gbpa;
      return ZX_OK;
    case A_IRQ_CTRL:
    case A_IRQ_CTRL_ACK:
      *data = s->irq_ctrl;
      return ZX_OK;
    case A_GERROR:
      *data = s->gerror;
      return ZX_OK;
    case A_GERRORN:
      *data = s->gerrorn;
      return ZX_OK;
    case A_GERROR_IRQ_CFG0: /* 64b */
      *data = extract64(s->gerror_irq_cfg0, 0, 32);
      return ZX_OK;
    case A_GERROR_IRQ_CFG0 + 4:
      *data = extract64(s->gerror_irq_cfg0, 32, 32);
      return ZX_OK;
    case A_GERROR_IRQ_CFG1:
      *data = s->gerror_irq_cfg1;
      return ZX_OK;
    case A_GERROR_IRQ_CFG2:
      *data = s->gerror_irq_cfg2;
      return ZX_OK;
    case A_STRTAB_BASE: /* 64b */
      *data = extract64(s->strtab_base, 0, 32);
      return ZX_OK;
    case A_STRTAB_BASE + 4: /* 64b */
      *data = extract64(s->strtab_base, 32, 32);
      return ZX_OK;
    case A_STRTAB_BASE_CFG:
      *data = s->strtab_base_cfg;
      return ZX_OK;
    case A_CMDQ_BASE: /* 64b */
      *data = extract64(s->cmdq.base, 0, 32);
      return ZX_OK;
    case A_CMDQ_BASE + 4:
      *data = extract64(s->cmdq.base, 32, 32);
      return ZX_OK;
    case A_CMDQ_PROD:
      *data = s->cmdq.prod;
      return ZX_OK;
    case A_CMDQ_CONS:
      *data = s->cmdq.cons;
      return ZX_OK;
    case A_EVENTQ_BASE: /* 64b */
      *data = extract64(s->eventq.base, 0, 32);
      return ZX_OK;
    case A_EVENTQ_BASE + 4: /* 64b */
      *data = extract64(s->eventq.base, 32, 32);
      return ZX_OK;
    case A_EVENTQ_PROD:
      *data = s->eventq.prod;
      return ZX_OK;
    case A_EVENTQ_CONS:
      *data = s->eventq.cons;
      return ZX_OK;
    default:
      *data = 0;
      FXL_LOG(ERROR) << "Unhandled 32-bit access at 0x" << std::hex << offset;
      return ZX_OK;
  }
}

zx_status_t Smmuv3::read_mmio(uint64_t offset,
                              uint64_t* data,
                              unsigned size) const {
  zx_status_t r;

  /* CONSTRAINED UNPREDICTABLE choice to have page0/1 be exact aliases */
  offset &= ~0x10000;

  switch (size) {
    case 8:
      r = readll(offset, data);
      break;
    case 4:
      r = readl(offset, data);
      break;
    default:
      r = ZX_ERR_IO;
      break;
  }

  return r;
}

static void smmuv3_init_regs(SMMUv3State* s) {
  s->idr[0] = FIELD_DP32(s->idr[0], IDR0, S1P, 1);
  s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TTF, 3);      /* AArch64 PTW only */
  s->idr[0] = FIELD_DP32(s->idr[0], IDR0, COHACC, 1);   /* IO coherent */
  s->idr[0] = FIELD_DP32(s->idr[0], IDR0, ASID16, 1);   /* 16-bit ASID */
  s->idr[0] = FIELD_DP32(s->idr[0], IDR0, VMID16, 1);   /* 16-bit VMID */
  s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TTENDIAN, 2); /* little endian */
  s->idr[0] = FIELD_DP32(s->idr[0], IDR0, STALL_MODEL, 1); /* No stall */
  /* terminated transaction will always be aborted/error returned */
  s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TERM_MODEL, 1);
  /* 2-level stream table supported */
  s->idr[0] = FIELD_DP32(s->idr[0], IDR0, STLEVEL, 1);

  s->idr[1] = FIELD_DP32(s->idr[1], IDR1, SIDSIZE, SMMU_IDR1_SIDSIZE);
  s->idr[1] = FIELD_DP32(s->idr[1], IDR1, EVENTQS, SMMU_EVENTQS);
  s->idr[1] = FIELD_DP32(s->idr[1], IDR1, CMDQS, SMMU_CMDQS);

  s->idr[3] = FIELD_DP32(s->idr[3], IDR3, HAD, 1);
  if (FIELD_EX32(s->idr[0], IDR0, S2P)) {
    /* XNX is a stage-2-specific feature */
    s->idr[3] = FIELD_DP32(s->idr[3], IDR3, XNX, 1);
  }
  s->idr[3] = FIELD_DP32(s->idr[3], IDR3, RIL, 1);
  s->idr[3] = FIELD_DP32(s->idr[3], IDR3, BBML, 2);

  s->idr[5] = FIELD_DP32(s->idr[5], IDR5, OAS, SMMU_IDR5_OAS); /* 36 bits */
  /* 4K, 16K and 64K granule support */
  s->idr[5] = FIELD_DP32(s->idr[5], IDR5, GRAN4K, 1);
  s->idr[5] = FIELD_DP32(s->idr[5], IDR5, GRAN16K, 1);
  s->idr[5] = FIELD_DP32(s->idr[5], IDR5, GRAN64K, 1);

  s->cmdq.base = deposit64(s->cmdq.base, 0, 5, SMMU_CMDQS);
  s->cmdq.prod = 0;
  s->cmdq.cons = 0;
  s->cmdq.entry_size = sizeof(struct Cmd);
  s->eventq.base = deposit64(s->eventq.base, 0, 5, SMMU_EVENTQS);
  s->eventq.prod = 0;
  s->eventq.cons = 0;
  s->eventq.entry_size = sizeof(struct Evt);

  s->features = 0;
  s->sid_split = 0;
  s->aidr = 0x1;
  s->cr[0] = 0;
  s->cr0ack = 0;
  s->irq_ctrl = 0;
  s->gerror = 0;
  s->gerrorn = 0;
  s->statusr = 0;
  s->gbpa = SMMU_GBPA_RESET_VAL;
}

zx_status_t Smmuv3::Read(uint64_t addr, IoValue* value) const {
  if (!initialized_) {
    auto status = zx_iommu_init_dev(iommu_);
    FXL_CHECK(status == ZX_OK);
    initialized_ = true;
    for (auto& sid : sids_) {
      auto it = state_.smmu_state.devices.find(sid);
      if (it != state_.smmu_state.devices.end()) {
        status = it->second->bti.attach();
        FXL_CHECK(status == ZX_OK);
      }
    }
  }

  return read_mmio(addr, &value->u64, value->access_size);
}

zx_status_t Smmuv3::Write(uint64_t addr, const IoValue& value) {
  return write_mmio(addr, value.u64, value.access_size);
}

mtk_smmu_type Smmuv3::GetSmmuType(uint64_t paddr) {
  switch (paddr) {
    case MM_SMMU_IO_BASE:
      return MM_SMMU;
    case APU_SMMU_IO_BASE:
      return APU_SMMU;
    case SOC_SMMU_IO_BASE:
      return SOC_SMMU;
    case GPU_SMMU_IO_BASE:
      return GPU_SMMU;
    default:
      return SMMU_TYPE_NUM;
  }
}

Smmuv3::Smmuv3(Guest* guest, uint8_t type)
    : guest_(guest), async_receiver_(this) {
  zx::resource resource;
  auto status = get_root_resource(&resource);

  FXL_CHECK(status == ZX_OK);

  zx_iommu_desc_smmuv3_t desc = {.id = type};

  status = zx_iommu_create(resource.get(), ZX_IOMMU_TYPE_ARM_SMMUV3, &desc,
                           sizeof(desc), &iommu_);
  FXL_CHECK(status == ZX_OK);

  status = zx_iommu_set_guest(iommu_, guest_->handle());
  FXL_CHECK(status == ZX_OK);

  async_receiver_.set_iommu(iommu_);
  status = async_receiver_.Begin(loop_.async());
  FXL_CHECK(status == ZX_OK);

  status = loop_.StartThread();
  FXL_CHECK(status == ZX_OK);
};

Smmuv3::~Smmuv3() {
  loop_.Quit();
  loop_.JoinThreads();

  auto bs = &state_.smmu_state;
  for (auto& it : bs->devices) {
    uninstall_nested_ste(it.second.get());
  }

  zx_handle_close(iommu_);
}

void Smmuv3::OnNewEvent(async_t* async, const zx_packet_iommu_event_t* event) {
  Evt evt;
  memcpy(&evt, event->evt, sizeof(evt));
  auto status = write_eventq(&evt);
  FXL_CHECK(status == ZX_OK);
};

zx_status_t Smmuv3::Init(const VsmmuSpec& spec) {
  smmuv3_init_regs(&state_);
  auto status = guest_->CreateMapping(TrapType::MMIO_SYNC, spec.paddr,
                                      kSmmuv3Size, 0, this);
  if (status != ZX_OK) {
    return status;
  }

  auto bs = &state_.smmu_state;
  for (auto& sid : spec.sids) {
    auto sdev = std::make_unique<SMMUDevice>();
    FXL_CHECK(sdev != nullptr);

    zx_handle_t bti_handle;
    auto status = zx_bti_create(iommu_, 0, sid, &bti_handle);
    FXL_CHECK(status == ZX_OK);

    memset(sdev.get(), 0, sizeof(SMMUDevice));
    sdev->sid = sid;
    sdev->smmu = this;
    sdev->bti.reset(bti_handle);
    sdev->hwpt = nullptr;
    bs->devices[sid] = std::move(sdev);
  }

  combined_irq_ = spec.irq;
  sids_ = spec.sids;

  return ZX_OK;
}

}  // namespace machina