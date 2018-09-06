// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/resolve_member.h"
#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/data_member.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/client/symbols/struct_class.h"
#include "garnet/bin/zxdb/client/symbols/type_test_support.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

// Defines a class with two member types "a" and "b". It puts the definitions
// of "a" and "b' members into the two out params.
fxl::RefPtr<StructClass> GetTestClassType(const DataMember** member_a,
                                          const DataMember** member_b) {
  auto int32_type = MakeInt32Type();
  auto sc = MakeStruct2Members("Foo", int32_type, "a", int32_type, "b");

  *member_a = sc->data_members()[0].Get()->AsDataMember();
  *member_b = sc->data_members()[1].Get()->AsDataMember();
  return sc;
}

}  // namespace

TEST(ResolveMember, GoodAccess) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  // Make this const volatile to add extra layers.
  auto vol_sc = fxl::MakeRefCounted<ModifiedType>(Symbol::kTagVolatileType,
                                                  LazySymbol(sc));
  auto const_vol_sc = fxl::MakeRefCounted<ModifiedType>(Symbol::kTagConstType,
                                                        LazySymbol(vol_sc));

  // This struct has the values 1 and 2 in it.
  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(const_vol_sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                 ExprValueSource(kBaseAddr));

  // Resolve A.
  ExprValue out;
  Err err = ResolveMember(base, a_data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ("int32_t", out.type()->GetAssignedName());
  EXPECT_EQ(4u, out.data().size());
  EXPECT_EQ(1, out.GetAs<int32_t>());
  EXPECT_EQ(kBaseAddr, out.source().address());

  // Resolve A by name.
  ExprValue out_by_name;
  err = ResolveMember(base, "a", &out_by_name);
  EXPECT_EQ(out, out_by_name);

  // Resolve B.
  out = ExprValue();
  err = ResolveMember(base, b_data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ("int32_t", out.type()->GetAssignedName());
  EXPECT_EQ(4u, out.data().size());
  EXPECT_EQ(2, out.GetAs<int32_t>());
  EXPECT_EQ(kBaseAddr + 4, out.source().address());

  // Resolve B by name.
  out_by_name = ExprValue();
  err = ResolveMember(base, "b", &out_by_name);
  EXPECT_EQ(out, out_by_name);
}

TEST(ResolveMember, BadArgs) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  // Test null base class pointer.
  ExprValue out;
  Err err = ResolveMember(ExprValue(), a_data, &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Can't resolve data member on non-struct/class value.", err.msg());

  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                 ExprValueSource(kBaseAddr));

  // Null datat member pointer.
  out = ExprValue();
  err = ResolveMember(base, nullptr, &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid data member for struct 'Foo'.", err.msg());
}

TEST(ResolveMember, BadAccess) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                 ExprValueSource(kBaseAddr));

  // Lookup by name that doesn't exist.
  ExprValue out;
  Err err = ResolveMember(base, "c", &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("No member 'c' in struct 'Foo'.", err.msg());

  // Lookup by a DataMember that references outside of the struct (in this
  // case, by one byte).
  auto bad_member = fxl::MakeRefCounted<DataMember>();
  bad_member->set_assigned_name("c");
  bad_member->set_type(LazySymbol(MakeInt32Type()));
  bad_member->set_member_location(5);

  out = ExprValue();
  err = ResolveMember(base, bad_member.get(), &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(
      "Member value 'c' is outside of the data of base 'Foo'. Please file a "
      "bug with a repro.",
      err.msg());
}

}  // namespace zxdb
