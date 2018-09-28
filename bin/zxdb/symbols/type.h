// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/bin/zxdb/symbols/symbol.h"

namespace zxdb {

class Type : public Symbol {
 public:
  // Symbol overrides.
  const Type* AsType() const final;
  const std::string& GetAssignedName() const final { return assigned_name_; }

  // Returns the type with no "const" or "volatile" modifiers. If this is
  // neither of those types, or the underlying modified typen can not be
  // resolved, it will return |this|.
  //
  // Most operations don't care about "const" and "volatile". This function
  // will follow modifiers until it finds a concrete type.
  //
  // It is on the Type class rather than the ModifiedType class so that calling
  // code can unconditionally call type->GetConcreteType()->byte_size() or
  // other functions to work with the type.
  virtual const Type* GetConcreteType() const;

  // The name assigned in the DWARF file. This will be empty for modified
  // types (Which usually have no assigned name). See
  // Symbol::GetAssignedName).
  void set_assigned_name(std::string n) { assigned_name_ = std::move(n); }

  // For forward-defines where the size of the structure is not known, the
  // byte size will be 0.
  uint32_t byte_size() const { return byte_size_; }
  void set_byte_size(uint32_t bs) { byte_size_ = bs; }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Type);
  FRIEND_MAKE_REF_COUNTED(Type);

  explicit Type(int kind);
  virtual ~Type();

 private:
  std::string assigned_name_;
  uint32_t byte_size_ = 0;
};

}  // namespace zxdb
