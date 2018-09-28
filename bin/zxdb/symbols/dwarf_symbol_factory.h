// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/symbol_factory.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace llvm {
class DWARFDie;
}  // namespace llvm

namespace zxdb {

class BaseType;
class LazySymbol;
class ModuleSymbolsImpl;

// Implementation of SymbolFactory that reads from the DWARF symbols in the
// given module.
class DwarfSymbolFactory : public SymbolFactory {
 public:
  explicit DwarfSymbolFactory(fxl::WeakPtr<ModuleSymbolsImpl> symbols);
  ~DwarfSymbolFactory() override;

  // SymbolFactory implementation.
  fxl::RefPtr<Symbol> CreateSymbol(void* data_ptr, uint32_t offset) override;

  // Returns a LazySymbol referencing the given DIE.
  LazySymbol MakeLazy(const llvm::DWARFDie& die);

 private:
  // Internal version that creates a symbol from a Die.
  fxl::RefPtr<Symbol> DecodeSymbol(const llvm::DWARFDie& die);

  // is_specification will be set when this function recursively calls itself
  // to parse the specification of a function implementation.
  fxl::RefPtr<Symbol> DecodeFunction(const llvm::DWARFDie& die,
                                     bool is_specification = false);
  fxl::RefPtr<Symbol> DecodeArrayType(const llvm::DWARFDie& die);
  fxl::RefPtr<Symbol> DecodeBaseType(const llvm::DWARFDie& die);
  fxl::RefPtr<Symbol> DecodeDataMember(const llvm::DWARFDie& die);
  fxl::RefPtr<Symbol> DecodeLexicalBlock(const llvm::DWARFDie& die);
  fxl::RefPtr<Symbol> DecodeModifiedType(const llvm::DWARFDie& die);
  fxl::RefPtr<Symbol> DecodeNamespace(const llvm::DWARFDie& die);
  fxl::RefPtr<Symbol> DecodeStructClass(const llvm::DWARFDie& die);
  fxl::RefPtr<Symbol> DecodeVariable(const llvm::DWARFDie& die);

  // This can be null if the module is unloaded but there are still some
  // dangling type references to it.
  fxl::WeakPtr<ModuleSymbolsImpl> symbols_;
};

}  // namespace zxdb
