// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest/bootloader/lk2.h"

#include <string.h>
#include "lib/fxl/logging.h"

zx_status_t BootloaderLk2::Setup(GuestConfig& cfg,
                                 uintptr_t* guest_ip,
                                 std::vector<uint64_t>& extra_params) {
  auto lk2_image = find_loaded_image("lk2");
  FXL_CHECK(lk2_image != nullptr);

  uint64_t lk2_start = lk2_image->phys_base;
  uint64_t lk2_end = lk2_image->phys_base + lk2_image->size;
  FXL_LOG(INFO) << "lk2: " << std::hex << lk2_start << "-" << std::hex
                << lk2_end;
  *guest_ip = lk2_image->phys_base;
  zx_cache_flush(InspectMemory<uint8_t>(lk2_start), lk2_image->size,
                 ZX_CACHE_FLUSH_DATA);

  auto dtb_image = find_loaded_image("dtb");
  if (dtb_image) {
    uint64_t dtb_start = dtb_image->phys_base;
    uint64_t dtb_end = dtb_image->phys_base + dtb_image->size;
    FXL_LOG(INFO) << "dtb: " << std::hex << dtb_start << "-" << std::hex
                  << dtb_end;
    cfg.set_dtb(dtb_start, dtb_image->size);
    FXL_LOG(INFO) << "Device tree setup completed";
  }
  return ZX_OK;
}

static constexpr uintptr_t kMrdumpOffset = 0x0D002000;
static constexpr uintptr_t kRamConsoleOffset = 0x0D001000;

void BootloaderLk2::DumpStateHandler(const nbl_vmm::GuestDumpState& state,
                                     uint32_t reason) {
  auto mrdump_cblock = phys_mem_.as<mrdump_control_block>(kMrdumpOffset);
  FXL_LOG(ERROR) << "phys mem base " << std::hex << phys_mem_.phys_base();
  if (strncmp(mrdump_cblock->sig, MRDUMP_GO_DUMP, strlen(MRDUMP_GO_DUMP))) {
    FXL_LOG(ERROR) << "MRDUMP signature mismatched";
    return;
  }
  int cpuid = 0;
  for (auto& vcpu_state : state.vcpus()) {
    FXL_LOG(INFO) << "Dump vcpu" << cpuid;
    auto vcpu_regs = &mrdump_cblock->crash_record.cpu_regs[cpuid++];
    memset(vcpu_regs, 0, sizeof(*vcpu_regs));
    auto gp_regs = vcpu_regs->arm64_reg.arm64_regs;
    auto ctrl_regs = &vcpu_regs->arm64_reg.arm64_creg;
    auto count = vcpu_state.regs().size();
    if (count != 31) {
      FXL_LOG(ERROR) << "We got " << count << " vcpu registers, should be 31!";
      /* Invalid count just skip dump */
      if (count < 0 || count > 31)
        count = 0;
    }
    /* x0 ~ x30 */
    for (int i = 0; i < count; i++) {
    gp_regs[i] = vcpu_state.regs(i);
    }
    /* x31: sp */
    gp_regs[31] = vcpu_state.sp_el1();
    /* x32: pc */
    gp_regs[32] = vcpu_state.pc();
    /* x33: pstate */
    gp_regs[33] = vcpu_state.cpsr();
    /* sctlr_el1, tcr_el1, ttbr0_el1, ttbr1_el1, sp_el0, sp */
    ctrl_regs->sctlr_el1 = vcpu_state.sctlr_el1();
    ctrl_regs->tcr_el1 = vcpu_state.tcr_el1();
    ctrl_regs->ttbr0_el1 = vcpu_state.ttbr0_el1();
    ctrl_regs->ttbr1_el1 = vcpu_state.ttbr1_el1();
    ctrl_regs->sp_el[0] = vcpu_state.sp_el0();
    ctrl_regs->sp_el[1] = vcpu_state.sp_el1();
  }
  if (reason == 1) {
    FXL_LOG(INFO) << "For kernel panic, no set exp_type";
    //return;
  }
  /* record boot reason to ram_console */
  //auto mboot_params = sram_mem.as<mboot_params_buffer>(kRamConsoleOffset);
  auto mboot_params = phys_mem_.as<mboot_params_buffer>(kRamConsoleOffset);
  unsigned int exp_type = AEE_EXP_TYPE_HWT;
  if (mboot_params && mboot_params->off_linux &&
  (mboot_params->off_linux ==
  (mboot_params->off_llk + mboot_params->sz_lk) &&
  (mboot_params->off_pl == sizeof(mboot_params_buffer)))) {
  auto rrk = (struct reboot_reason_kernel*)((uint8_t*)mboot_params +
            mboot_params->off_linux);
  if ((rrk->exp_type ^ MBOOT_PARAMS_EXP_TYPE_MAGIC) > 0 &&
  (rrk->exp_type ^ MBOOT_PARAMS_EXP_TYPE_MAGIC) < 16) {
    FXL_LOG(ERROR) << "set exp type failed: it had been set as "
    << (rrk->exp_type ^ MBOOT_PARAMS_EXP_TYPE_MAGIC) << " before";
    return;
  }
  exp_type = exp_type ^ MBOOT_PARAMS_EXP_TYPE_MAGIC;
  rrk->exp_type = exp_type;
  FXL_LOG(INFO) << "set exp type: " << std::hex << rrk->exp_type;
  auto rr_pl = (struct reboot_reason_pl*)((uint8_t*)mboot_params +
          mboot_params->off_pl);
  rr_pl->wdt_status = 0x6;
  FXL_LOG(INFO) << "set wdt_status: " << rr_pl->wdt_status;
  } else {
    FXL_LOG(ERROR) << "mboot params not ready, set exp type failed: exp type: "
    << exp_type;
  }
}