// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/module_symbol_index.h"
#include "garnet/bin/zxdb/symbols/module_symbols.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"

namespace llvm {

class DWARFCompileUnit;
class DWARFContext;
class MemoryBuffer;

namespace object {
class Binary;
}  // namespace object

}  // namespace llvm

namespace zxdb {

class DwarfSymbolFactory;

// Represents the symbols for a module (executable or shared library).
//
// All addresses in and out of the API of this class are module-relative. This
// way, the symbol information can be shared between multiple processes that
// have mapped the same .so file (often at different addresses). This means
// that callers have to offset addresses when calling into this class, and
// offset them in the opposite way when they get the results.
class ModuleSymbolsImpl : public ModuleSymbols {
 public:
  // You must call Load before using this class.
  explicit ModuleSymbolsImpl(const std::string& name,
                             const std::string& build_id);
  ~ModuleSymbolsImpl();

  llvm::DWARFContext* context() { return context_.get(); }
  llvm::DWARFUnitVector& compile_units() { return compile_units_; }
  DwarfSymbolFactory* symbol_factory() { return symbol_factory_.get(); }

  Err Load();

  fxl::WeakPtr<ModuleSymbolsImpl> GetWeakPtr();

  // ModuleSymbols implementation.
  ModuleSymbolStatus GetStatus() const override;
  Location LocationForAddress(const SymbolContext& symbol_context,
                              uint64_t absolute_address) const override;
  LineDetails LineDetailsForAddress(const SymbolContext& symbol_context,
                                    uint64_t absolute_address) const override;
  std::vector<uint64_t> AddressesForFunction(
      const SymbolContext& symbol_context,
      const std::string& name) const override;
  std::vector<std::string> FindFileMatches(
      const std::string& name) const override;
  std::vector<uint64_t> AddressesForLine(const SymbolContext& symbol_context,
                                         const FileLine& line) const override;

 private:
  llvm::DWARFUnit* CompileUnitForRelativeAddress(
      uint64_t relative_address) const;

  const std::string name_;
  const std::string build_id_;

  std::unique_ptr<llvm::MemoryBuffer> binary_buffer_;  // Backing for binary_.
  std::unique_ptr<llvm::object::Binary> binary_;
  std::unique_ptr<llvm::DWARFContext> context_;

  llvm::DWARFUnitVector compile_units_;

  ModuleSymbolIndex index_;

  fxl::RefPtr<DwarfSymbolFactory> symbol_factory_;

  fxl::WeakPtrFactory<ModuleSymbolsImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleSymbolsImpl);
};

}  // namespace zxdb
