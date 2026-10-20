// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/bootloader/bootloader.h"

#define MRDUMP_GO_DUMP "MRDUMP11"
#define MRDUMP_CB_ADDR 0x11e000
#define RAM_CONSOLE_ADDR 0x11d000
#define AEE_EXP_TYPE_HWT 1
#define AEE_EXP_TYPE_NEBULA_HYP_PANIC 15
#define MBOOT_PARAMS_EXP_TYPE_MAGIC 0xaeedead0

typedef uint32_t arm32_gregset_t[18];
typedef uint64_t arm64_gregset_t[34];
typedef char char_t;
struct arm32_ctrl_regs {
  uint32_t sctlr;
  uint64_t ttbcr;
  uint64_t ttbr0;
  uint64_t ttbr1;
};

struct arm64_ctrl_regs {
  uint64_t sctlr_el1;
  uint64_t sctlr_el2;
  uint64_t sctlr_el3;

  uint64_t tcr_el1;
  uint64_t tcr_el2;
  uint64_t tcr_el3;

  uint64_t ttbr0_el1;
  uint64_t ttbr0_el2;
  uint64_t ttbr0_el3;

  uint64_t ttbr1_el1;

  uint64_t sp_el[4];
};

struct mrdump_arm32_reg {
  arm32_gregset_t arm32_regs;
  struct arm32_ctrl_regs arm32_creg;
};

struct mrdump_arm64_reg {
  arm64_gregset_t arm64_regs;
  struct arm64_ctrl_regs arm64_creg;
};

struct mrdump_crash_record {
  int32_t reboot_mode;

  char_t msg[128];

  uint32_t fault_cpu;

  union {
    struct mrdump_arm32_reg arm32_reg;
    struct mrdump_arm64_reg arm64_reg;
  } cpu_regs[0];
};

struct __PACKED mrdump_ksyms_param {
  char_t tag[4];
  uint32_t flag;
  uint32_t crc;
  uint64_t start_addr;
  uint32_t size;
  uint32_t addresses_off;
  uint32_t num_syms_off;
  uint32_t names_off;
  uint32_t markers_off;
  uint32_t token_table_off;
  uint32_t token_index_off;
};

struct mrdump_machdesc {
  uint32_t nr_cpus;

  uint64_t page_offset;
  uint64_t tcr_el1_t1sz;

  uint64_t kimage_vaddr;
  uint64_t dram_start;
  uint64_t dram_end;
  uint64_t kimage_stext;
  uint64_t kimage_etext;
  uint64_t kimage_stext_real;
  uint64_t kimage_voffset;
  uint64_t kernel_pac_mask;
  uint64_t unused1;

  uint64_t vmalloc_start;
  uint64_t vmalloc_end;

  uint64_t modules_start;
  uint64_t modules_end;

  uint64_t phys_offset;
  uint64_t master_page_table;

  uint64_t memmap;
  uint64_t pageflags;
  uint32_t struct_page_size;

  uint64_t dfdmem_pa;

  struct mrdump_ksyms_param kallsyms;
};

struct mrdump_control_block {
  char_t sig[8];

  struct mrdump_machdesc machdesc;
  uint32_t machdesc_crc;

  uint32_t unused0;
  uint32_t output_fs_lbaooo;

  struct mrdump_crash_record crash_record;
};

struct mboot_params_buffer {
  uint32_t sig;
  /* for size comptible */
  uint32_t off_pl;
  uint32_t off_lpl; /* last preloader */
  uint32_t sz_pl;
  uint32_t off_lk;
  uint32_t off_llk; /* last lk */
  uint32_t sz_lk;
  uint32_t padding[2]; /* size = 2 * 16 = 32 byte */
  uint32_t dump_step;
  uint32_t sz_buffer;
  uint32_t off_linux;
  uint32_t off_console;
  uint32_t padding2[3];
};

struct reboot_reason_kernel {
  uint32_t fiq_step;
  /* 0xaeedeadX: X=1 (HWT), X=2 (KE), X=3 (nested panic) */
  uint32_t exp_type;
  uint64_t kaslr_offset;
  uint64_t mboot_params_buffer_addr;
};

#define MBOOT_PARAMS_PL_SIZE 3
struct reboot_reason_pl {
    uint32_t wdt_status;
    uint32_t last_func[MBOOT_PARAMS_PL_SIZE];
};

class BootloaderLk2 : public Bootloader {
 public:
  BootloaderLk2(nbl_vmm::BootloaderRecord& rec, machina::Guest& guest)
      : Bootloader(rec, guest) {}

 protected:
  virtual zx_status_t Setup(GuestConfig& cfg,
                            uintptr_t* guest_ip,
                            std::vector<uint64_t>& extra_params) override;
  virtual void DumpStateHandler(const nbl_vmm::GuestDumpState& state,
                                uint32_t reason) override;
};