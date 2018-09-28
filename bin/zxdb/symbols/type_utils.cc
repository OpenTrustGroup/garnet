// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/type_utils.h"

#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/type.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

Err GetPointedToType(const Type* input, const Type** pointed_to) {
  if (!input)
    return Err("No type information.");

  // Convert to a pointer.
  const ModifiedType* mod_type = input->GetConcreteType()->AsModifiedType();
  if (!mod_type || mod_type->tag() != Symbol::kTagPointerType) {
    return Err(fxl::StringPrintf(
        "Attempting to dereference '%s' which is not a pointer.",
        input->GetFullName().c_str()));
  }

  *pointed_to = mod_type->modified().Get()->AsType();
  if (!*pointed_to)
    return Err("Missing pointer type info, please file a bug with a repro.");
  return Err();
}

}  // namespace zxdb
