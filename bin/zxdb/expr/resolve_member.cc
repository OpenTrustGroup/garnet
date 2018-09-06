// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/resolve_member.h"

#include "garnet/bin/zxdb/client/symbols/data_member.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/client/symbols/struct_class.h"
#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/type_utils.h"
#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/resolve_pointer.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Tries to interpret the type as a pointed to a StructClass. On success,
// places the output into |*sc|.
Err GetPointedToStructClass(const Type* type, const StructClass** sc) {
  const Type* pointed_to = nullptr;
  Err err = GetPointedToType(type, &pointed_to);
  if (err.has_error())
    return err;

  *sc = pointed_to->GetConcreteType()->AsStructClass();
  if (!sc) {
    return Err(
        fxl::StringPrintf("Attempting to dereference a pointer to '%s' which "
                          "is not a class or a struct.",
                          pointed_to->GetFullName().c_str()));
  }
  return Err();
}

// This can accept a null base pointer so the caller doesn't need to check.
// On success, fills *out.
Err FindMemberNamed(const StructClass* base, const std::string& member_name,
                    const DataMember** out) {
  if (!base) {
    return Err(fxl::StringPrintf(
        "Can't resolve '%s' on non-struct/class value.", member_name.c_str()));
  }

  for (const auto& lazy : base->data_members()) {
    const DataMember* data = lazy.Get()->AsDataMember();
    if (data && data->GetAssignedName() == member_name) {
      *out = data;
      return Err();
    }
  }
  return Err(fxl::StringPrintf(
      "No member '%s' in %s '%s'.", member_name.c_str(),
      base->GetStructOrClassString(), base->GetFullName().c_str()));
}

// Validates the input member (it will null check) and extracts the type
// for the member.
Err GetMemberType(const StructClass* sc, const DataMember* member,
                  fxl::RefPtr<Type>* member_type) {
  if (!member) {
    return Err(fxl::StringPrintf("Invalid data member for %s '%s'.",
                                 sc->GetStructOrClassString(),
                                 sc->GetFullName().c_str()));
  }

  *member_type =
      fxl::RefPtr<Type>(const_cast<Type*>(member->type().Get()->AsType()));
  if (!*member_type) {
    return Err(fxl::StringPrintf("Bad type information for '%s.%s'.",
                                 sc->GetFullName().c_str(),
                                 member->GetAssignedName().c_str()));
  }
  return Err();
}

void DoResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                              const ExprValue& base_ptr,
                              const StructClass* pointed_to_type,
                              const DataMember* member,
                              std::function<void(const Err&, ExprValue)> cb) {
  Err err = base_ptr.EnsureSizeIs(sizeof(uint64_t));
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  fxl::RefPtr<Type> member_type;
  err = GetMemberType(pointed_to_type, member, &member_type);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  uint64_t base_address = base_ptr.GetAs<uint64_t>();
  uint32_t offset = member->member_location();
  ResolvePointer(context->GetDataProvider(), base_address + offset,
                 std::move(member_type), std::move(cb));
}

}  // namespace

Err ResolveMember(const ExprValue& base, const DataMember* member,
                  ExprValue* out) {
  const StructClass* sc = nullptr;
  if (!base.type() || !(sc = base.type()->GetConcreteType()->AsStructClass()))
    return Err("Can't resolve data member on non-struct/class value.");

  fxl::RefPtr<Type> member_type;
  Err err = GetMemberType(sc, member, &member_type);
  if (err.has_error())
    return err;

  // Extract the data.
  uint32_t offset = member->member_location();
  uint32_t size = member_type->byte_size();
  if (offset + size > base.data().size()) {
    return Err(fxl::StringPrintf(
        "Member value '%s' is outside of the data of base '%s'. Please file a "
        "bug with a repro.",
        member->GetAssignedName().c_str(), sc->GetFullName().c_str()));
  }
  std::vector<uint8_t> member_data(base.data().begin() + offset,
                                   base.data().begin() + (offset + size));

  *out = ExprValue(std::move(member_type), std::move(member_data),
                   base.source().GetOffsetInto(offset));
  return Err();
}

Err ResolveMember(const ExprValue& base, const std::string& member_name,
                  ExprValue* out) {
  if (!base.type())
    return Err("No type information.");

  const DataMember* member = nullptr;
  Err err = FindMemberNamed(base.type()->GetConcreteType()->AsStructClass(),
                            member_name, &member);
  if (err.has_error())
    return err;
  return ResolveMember(base, member, out);
}

void ResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                            const ExprValue& base_ptr, const DataMember* member,
                            std::function<void(const Err&, ExprValue)> cb) {
  const StructClass* sc = nullptr;
  Err err = GetPointedToStructClass(base_ptr.type(), &sc);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  DoResolveMemberByPointer(context, base_ptr, sc, member, std::move(cb));
}

void ResolveMemberByPointer(fxl::RefPtr<ExprEvalContext> context,
                            const ExprValue& base_ptr,
                            const std::string& member_name,
                            std::function<void(const Err&, ExprValue)> cb) {
  const StructClass* sc = nullptr;
  Err err = GetPointedToStructClass(base_ptr.type(), &sc);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  const DataMember* member = nullptr;
  err = FindMemberNamed(sc, member_name, &member);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  DoResolveMemberByPointer(context, base_ptr, sc, member, std::move(cb));
}

}  // namespace zxdb
