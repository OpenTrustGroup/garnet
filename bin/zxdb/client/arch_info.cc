// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/arch_info.h"

#include "garnet/public/lib/fxl/logging.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"

namespace zxdb {

ArchInfo::ArchInfo() {
  int argc = 0;
  const char* arg = nullptr;
  const char** argv = &arg;
  init_ = std::make_unique<llvm::InitLLVM>(argc, argv);

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllDisassemblers();
}

ArchInfo::~ArchInfo() = default;

Err ArchInfo::Init(debug_ipc::Arch arch) {
  switch (arch) {
    case debug_ipc::Arch::kX64:
      is_fixed_instr_ = false;
      max_instr_len_ = 15;
      instr_align_ = 1;
      triple_name_ = "x86_64";
      processor_name_ = "x86-64";
      break;
    case debug_ipc::Arch::kArm64:
      is_fixed_instr_ = true;
      max_instr_len_ = 4;
      instr_align_ = 4;
      triple_name_ = "aarch64";
      processor_name_ = "generic";
      break;
    default:
      FXL_NOTREACHED();
      break;
  }

  triple_ = std::make_unique<llvm::Triple>(triple_name_);

  std::string err_msg;
  target_ = llvm::TargetRegistry::lookupTarget(triple_name_, err_msg);
  if (!target_)
    return Err("Error initializing LLVM: " + err_msg);

  instr_info_.reset(target_->createMCInstrInfo());
  register_info_.reset(target_->createMCRegInfo(triple_name_));
  subtarget_info_.reset(
      target_->createMCSubtargetInfo(triple_name_, processor_name_, ""));
  asm_info_.reset(target_->createMCAsmInfo(*register_info_, triple_name_));

  if (!instr_info_ || !register_info_ || !subtarget_info_ || !asm_info_)
    return Err("Error initializing LLVM.");

  return Err();
}

}  // namespace zxdb
