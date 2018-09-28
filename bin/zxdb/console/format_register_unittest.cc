// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "garnet/bin/zxdb/console/format_register.cc"
#include "lib/fxl/logging.h"

namespace zxdb {

using namespace debug_ipc;

namespace {

// Creates fake data for a register.
// |length| is how long the register data (and thus the register) is.
// |val_loop| is to determine a loop that will fill the register with a
// particular pattern (010203....).
std::vector<uint8_t> CreateData(size_t length, size_t val_loop) {
  FXL_DCHECK(length >= val_loop);

  std::vector<uint8_t> data(length);
  // So that we get the number backwards (0x0102...).
  uint8_t base = val_loop;
  for (size_t i = 0; i < val_loop; i++) {
    data[i] = base - i;
  }
  return data;
}

debug_ipc::Register CreateRegister(RegisterID id, size_t length,
                                   size_t val_loop) {
  debug_ipc::Register reg;
  reg.id = id;
  reg.data = CreateData(length, val_loop);
  return reg;
}

void SetRegisterValue(Register* reg, uint64_t value) {
  std::vector<uint8_t> data;
  data.reserve(8);
  uint8_t* ptr = reinterpret_cast<uint8_t*>(&value);
  for (size_t i = 0; i < 8; i++, ptr++)
    data.emplace_back(*ptr);
  reg->data() = data;
}

Register CreateRegisterWithValue(RegisterID id, uint64_t value) {
  Register reg(CreateRegister(id, 8, 8));
  SetRegisterValue(&reg, value);
  return reg;
}

void GetCategories(RegisterSet* registers) {
  std::vector<debug_ipc::RegisterCategory> categories;

  RegisterCategory cat1;
  cat1.type = RegisterCategory::Type::kGeneral;
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rax, 8, 1));
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rbx, 8, 2));
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rcx, 8, 4));
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rdx, 8, 8));
  categories.push_back(cat1);

  // Sanity check
  ASSERT_EQ(*(uint8_t*)&(cat1.registers[0].data[0]), 0x01u);
  ASSERT_EQ(*(uint16_t*)&(cat1.registers[1].data[0]), 0x0102u);
  ASSERT_EQ(*(uint32_t*)&(cat1.registers[2].data[0]), 0x01020304u);
  ASSERT_EQ(*(uint64_t*)&(cat1.registers[3].data[0]), 0x0102030405060708u);

  RegisterCategory cat2;
  cat2.type = RegisterCategory::Type::kVector;
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm0, 16, 1));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm1, 16, 2));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm2, 16, 4));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm3, 16, 8));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm4, 16, 16));
  categories.push_back(cat2);

  RegisterCategory cat3;
  cat3.type = RegisterCategory::Type::kFloatingPoint;
  cat3.registers.push_back(CreateRegister(RegisterID::kX64_st0, 16, 4));
  cat3.registers.push_back(CreateRegister(RegisterID::kX64_st1, 16, 4));
  // Invalid
  cat3.registers.push_back(CreateRegister(RegisterID::kX64_st2, 16, 16));
  // Push a valid 16-byte long double value.
  auto& reg = cat3.registers.back();
  // Clear all (create a 0 value).
  for (size_t i = 0; i < reg.data.size(); i++)
    reg.data[i] = 0;

  categories.push_back(cat3);

  RegisterSet regs(debug_ipc::Arch::kArm64, std::move(categories));
  *registers = std::move(regs);
}

}  // namespace

TEST(FormatRegisters, GeneralRegisters) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kGeneral};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(registers, &filtered_set, cats_to_show);
  ASSERT_FALSE(err.has_error()) << err.msg();

  // Force rcx to -2 to test negative integer formatting.
  auto& reg = filtered_set[RegisterCategory::Type::kGeneral];
  Register& rcx = reg[2];
  EXPECT_EQ(RegisterID::kX64_rcx, rcx.id());
  SetRegisterValue(&rcx, static_cast<uint64_t>(-2));

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "rax                 0x1 = 1\n"
      "rbx               0x102 = 258\n"
      "rcx  0xfffffffffffffffe = -2\n"
      "rdx   0x102030405060708 \n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, VectorRegisters) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kVector};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(registers, &filtered_set, cats_to_show);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "xmm0 00000000 00000000 00000000 00000001\n"
      "xmm1 00000000 00000000 00000000 00000102\n"
      "xmm2 00000000 00000000 00000000 01020304\n"
      "xmm3 00000000 00000000 01020304 05060708\n"
      "xmm4 01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, AllRegisters) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      {RegisterCategory::Type::kGeneral, RegisterCategory::Type::kFloatingPoint,
       RegisterCategory::Type::kVector, RegisterCategory::Type::kMisc}};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(registers, &filtered_set, cats_to_show);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  // TODO(donosoc): Detect the maximum length and make the the tables coincide.
  EXPECT_EQ(
      "General Purpose Registers\n"
      "rax                0x1 = 1\n"
      "rbx              0x102 = 258\n"
      "rcx          0x1020304 \n"
      "rdx  0x102030405060708 \n"
      "\n"
      "Floating Point Registers\n"
      "st0  6.163689759657267600e-4944  00000000 00000000 00000000 01020304\n"
      "st1  6.163689759657267600e-4944  00000000 00000000 00000000 01020304\n"
      "st2  0.000000000000000000e+00    00000000 00000000 00000000 00000000\n"
      "\n"
      "Vector Registers\n"
      "xmm0 00000000 00000000 00000000 00000001\n"
      "xmm1 00000000 00000000 00000000 00000102\n"
      "xmm2 00000000 00000000 00000000 01020304\n"
      "xmm3 00000000 00000000 01020304 05060708\n"
      "xmm4 01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, OneRegister) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      {RegisterCategory::Type::kGeneral, RegisterCategory::Type::kFloatingPoint,
       RegisterCategory::Type::kVector, RegisterCategory::Type::kMisc}};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(registers, &filtered_set, cats_to_show, "xmm3");
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "xmm3 00000000 00000000 01020304 05060708\n"
      "\n",
      out.AsString());
}

TEST(FormatRegister, RegexSearch) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kVector};
  FilteredRegisterSet filtered_set;
  Err err =
      FilterRegisters(registers, &filtered_set, cats_to_show, "XMm[2-4]$");
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "xmm2 00000000 00000000 00000000 01020304\n"
      "xmm3 00000000 00000000 01020304 05060708\n"
      "xmm4 01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, CannotFindRegister) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      {RegisterCategory::Type::kGeneral, RegisterCategory::Type::kFloatingPoint,
       RegisterCategory::Type::kVector, RegisterCategory::Type::kMisc}};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(registers, &filtered_set, cats_to_show, "W0");
  EXPECT_TRUE(err.has_error());
}

TEST(FormatRegisters, WithRflags) {
  RegisterSet register_set;
  GetCategories(&register_set);
  auto& cat = register_set.category_map()[RegisterCategory::Type::kGeneral];
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_rflags, 0));

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kGeneral};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(register_set, &filtered_set, cats_to_show);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "   rax                0x1 = 1\n"
      "   rbx              0x102 = 258\n"
      "   rcx          0x1020304 \n"
      "   rdx  0x102030405060708 \n"
      "rflags         0x00000000 CF=0, PF=0, AF=0, ZF=0, SF=0, TF=0, IF=0, "
      "DF=0, OF=0\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, RFlagsValues) {
  RegisterSet register_set;
  auto& cat = register_set.category_map()[RegisterCategory::Type::kGeneral];
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_rflags, 0));

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kGeneral};
  FilteredRegisterSet filtered_set;
  Err err =
      FilterRegisters(register_set, &filtered_set, cats_to_show, "rflags");
  ASSERT_FALSE(err.has_error()) << err.msg();

  // filtered_set now holds a pointer to rflags that we can change.
  auto& reg = filtered_set[RegisterCategory::Type::kGeneral].front();
  SetRegisterValue(&reg, 0b1110100110010101010101);

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "rflags  0x003a6555 CF=1, PF=1, AF=1, ZF=1, SF=0, TF=1, IF=0, DF=1, "
      "OF=0\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, CPSRValues) {
  RegisterSet register_set;
  auto& cat = register_set.category_map()[RegisterCategory::Type::kGeneral];
  cat.push_back(CreateRegisterWithValue(RegisterID::kARMv8_cpsr, 0));

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kGeneral};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(register_set, &filtered_set, cats_to_show, "cpsr");
  ASSERT_FALSE(err.has_error()) << err.msg();

  // filtered_set now holds a pointer to rflags that we can change.
  auto& reg = filtered_set[RegisterCategory::Type::kGeneral].front();
  SetRegisterValue(&reg, 0xA1234567);

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kArm64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "cpsr  0xa1234567 V=0, C=1, Z=0, N=1\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, DebugRegisters) {
  RegisterSet register_set;
  auto& cat = register_set.category_map()[RegisterCategory::Type::kDebug];
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_dr0, 0x1234));
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_dr1, 0x1234567));
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_dr2, 0x123456789ab));
  cat.push_back(
      CreateRegisterWithValue(RegisterID::kX64_dr3, 0x123456789abcdef));

  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_dr6, 0xaffa));
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_dr7, 0xaaaa26aa));

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kDebug};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(register_set, &filtered_set, cats_to_show);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Debug Registers\n"
      "dr0             0x1234 = 4660\n"
      "dr1          0x1234567 \n"
      "dr2      0x123456789ab \n"
      "dr3  0x123456789abcdef \n"
      "dr6         0x0000affa B0=0, B1=1, B2=0, B3=1, BD=1, BS=0, BT=1\n"
      "dr7         0xaaaa26aa L0=0, G0=1, L1=0, G1=1, L2=0, G2=1, L3=0, G4=1, "
      "LE=0, GE=1, GD=1\n"
      "                       R/W0=2, LEN0=2, R/W1=2, LEN1=2, R/W2=2, LEN2=2, "
      "R/W3=2, LEN3=2\n"
      "\n",
      out.AsString());
}

}  // namespace zxdb
