// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <type_traits>

#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/code_block.h"
#include "garnet/bin/zxdb/client/symbols/data_member.h"
#include "garnet/bin/zxdb/client/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/client/symbols/struct_class.h"
#include "garnet/bin/zxdb/client/symbols/type_test_support.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/common/test_with_loop.h"
#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/expr_node.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/symbol_eval_context.h"
#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

// Custom ExprNode that just returns a known value, either synchronously or
// asynchronously.
class TestExprNode : public ExprNode {
 public:
  void Eval(fxl::RefPtr<ExprEvalContext> context,
            EvalCallback cb) const override {
    if (is_synchronous_) {
      cb(Err(), value_);
    } else {
      debug_ipc::MessageLoop::Current()->PostTask(
          [ value = value_, cb ]() { cb(Err(), value); });
    }
  }
  void Print(std::ostream& out, int indent) const override {}

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(TestExprNode);
  FRIEND_MAKE_REF_COUNTED(TestExprNode);

  TestExprNode(bool is_synchronous, ExprValue value)
      : is_synchronous_(is_synchronous), value_(value) {}
  ~TestExprNode() override = default;

  bool is_synchronous_;
  ExprValue value_;
};

class TestEvalContext : public ExprEvalContext {
 public:
  TestEvalContext()
      : data_provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()),
        resolver_(data_provider_) {}
  ~TestEvalContext() = default;

  MockSymbolDataProvider* data_provider() { return data_provider_.get(); }

  void AddVariable(const std::string& name, ExprValue v) { values_[name] = v; }

  // ExprEvalContext implementation.
  void GetVariable(
      const std::string& name,
      std::function<void(const Err& err, ExprValue value)> cb) override {
    auto found = values_.find(name);
    if (found == values_.end())
      cb(Err("Not found"), ExprValue());
    else
      cb(Err(), found->second);
  }
  SymbolVariableResolver& GetVariableResolver() override { return resolver_; }
  fxl::RefPtr<SymbolDataProvider> GetDataProvider() override {
    return data_provider_;
  }

 private:
  fxl::RefPtr<MockSymbolDataProvider> data_provider_;
  SymbolVariableResolver resolver_;
  std::map<std::string, ExprValue> values_;
};

class ExprNodeTest : public TestWithLoop {};

}  // namespace

TEST_F(ExprNodeTest, EvalIdentifier) {
  auto context = fxl::MakeRefCounted<TestEvalContext>();
  ExprValue foo_expected(12);
  context->AddVariable("foo", foo_expected);

  // This identifier should be found synchronously and returned.
  auto good_identifier = fxl::MakeRefCounted<IdentifierExprNode>(
      ExprToken(ExprToken::Type::kName, "foo", 0));
  bool called = false;
  Err out_err;
  ExprValue out_value;
  good_identifier->Eval(context, [&called, &out_err, &out_value](
                                     const Err& err, ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
  });

  // This should succeed synchronously.
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(foo_expected, out_value);

  // This identifier should be not found.
  auto bad_identifier = fxl::MakeRefCounted<IdentifierExprNode>(
      ExprToken(ExprToken::Type::kName, "bar", 0));
  called = false;
  out_value = ExprValue();
  bad_identifier->Eval(context, [&called, &out_err, &out_value](
                                    const Err& err, ExprValue value) {
    called = true;
    out_err = err;
    out_value = ExprValue();  // value;
  });

  // It should fail synchronously.
  EXPECT_TRUE(called);
  EXPECT_TRUE(out_err.has_error());
  EXPECT_EQ(ExprValue(), out_value);
}

template <typename T>
void DoUnaryMinusTest(T in) {
  auto context = fxl::MakeRefCounted<TestEvalContext>();
  ExprValue foo_expected(in);
  context->AddVariable("foo", foo_expected);

  auto identifier = fxl::MakeRefCounted<IdentifierExprNode>(
      ExprToken(ExprToken::kName, "foo", 0));

  // Validate the value by itself. This also has the effect of checking the
  // ExprValue type-specific constructor.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  identifier->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                            ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
  });
  EXPECT_TRUE(called);
  EXPECT_EQ(sizeof(T), out_value.data().size());

  called = false;
  out_err = Err();
  out_value = ExprValue();

  // Apply a unary '-' to that value.
  auto unary = fxl::MakeRefCounted<UnaryOpExprNode>(
      ExprToken(ExprToken::kMinus, "-", 0), std::move(identifier));
  unary->Eval(context,
              [&called, &out_err, &out_value](const Err& err, ExprValue value) {
                called = true;
                out_err = err;
                out_value = value;
              });

  // This checked that the type conversions have followed C rules. This is
  // the expected value (int/unsigned unchanged, everything smaller than an int
  // is promoted to an int, everything larger remains unchanged).
  auto expected = -in;

  // The type of the output should be the same as the input for unary '-'.
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  EXPECT_EQ(sizeof(expected), out_value.data().size());
  if (std::is_unsigned<decltype(expected)>::value) {
    EXPECT_EQ(BaseType::kBaseTypeUnsigned, out_value.GetBaseType());
  } else {
    EXPECT_EQ(BaseType::kBaseTypeSigned, out_value.GetBaseType());
  }
  EXPECT_EQ(expected, out_value.GetAs<decltype(expected)>());
}

template <typename T>
void DoUnaryMinusTypeTest() {
  DoUnaryMinusTest<T>(0);
  DoUnaryMinusTest<T>(std::numeric_limits<T>::max());
  DoUnaryMinusTest<T>(std::numeric_limits<T>::lowest());
}

TEST_F(ExprNodeTest, UnaryMinus) {
  // Test the limits of all built-in types.
  DoUnaryMinusTypeTest<int8_t>();
  DoUnaryMinusTypeTest<uint8_t>();
  DoUnaryMinusTypeTest<int16_t>();
  DoUnaryMinusTypeTest<uint16_t>();
  DoUnaryMinusTypeTest<int32_t>();
  DoUnaryMinusTypeTest<uint32_t>();
  DoUnaryMinusTypeTest<int64_t>();
  DoUnaryMinusTypeTest<uint64_t>();

  // Try an unsupported value (a 3-byte signed). This should throw an error and
  // compute an empty value.
  auto context = fxl::MakeRefCounted<TestEvalContext>();
  ExprValue expected(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 3, "uint24_t"),
      {0, 0, 0});
  context->AddVariable("foo", expected);

  auto identifier = fxl::MakeRefCounted<IdentifierExprNode>(
      ExprToken(ExprToken::kName, "foo", 0));
  auto unary = fxl::MakeRefCounted<UnaryOpExprNode>(
      ExprToken(ExprToken::kMinus, "-", 0), std::move(identifier));

  bool called = false;
  Err out_err;
  ExprValue out_value;
  unary->Eval(context,
              [&called, &out_err, &out_value](const Err& err, ExprValue value) {
                called = true;
                out_err = err;
                out_value = value;
              });
  EXPECT_TRUE(called);
  EXPECT_TRUE(out_err.has_error());
  EXPECT_EQ("Negation for this value is not supported.", out_err.msg());
  EXPECT_EQ(ExprValue(), out_value);
}

// This test mocks at the SymbolDataProdiver level because most of the
// dereference logic is in the SymbolEvalContext.
TEST_F(ExprNodeTest, DereferenceReference) {
  auto data_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      SymbolContext::ForRelativeAddresses(), data_provider,
      fxl::RefPtr<CodeBlock>());

  // Dereferencing should remove the const on the pointer but not the pointee.
  auto base_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 4, "uint32_t");
  auto const_base_type = fxl::MakeRefCounted<ModifiedType>(
      Symbol::kTagConstType, LazySymbol(base_type));
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(
      Symbol::kTagPointerType, LazySymbol(const_base_type));
  auto const_ptr_type = fxl::MakeRefCounted<ModifiedType>(Symbol::kTagConstType,
                                                          LazySymbol(ptr_type));

  // The value being pointed to.
  constexpr uint32_t kValue = 0x12345678;
  constexpr uint64_t kAddress = 0x1020;
  data_provider->AddMemory(kAddress, {0x78, 0x56, 0x34, 0x12});

  // The pointer.
  ExprValue ptr_value(const_ptr_type,
                      {0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  // Execute the dereference.
  auto deref_node = fxl::MakeRefCounted<DereferenceExprNode>(
      fxl::MakeRefCounted<TestExprNode>(true, ptr_value));
  bool called = false;
  Err out_err;
  ExprValue out_value;
  deref_node->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                            ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // Should complete asynchronously.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();

  // The type should be the const base type.
  EXPECT_EQ(const_base_type.get(), out_value.type());

  ASSERT_EQ(4u, out_value.data().size());
  EXPECT_EQ(kValue, out_value.GetAs<uint32_t>());

  // Now go backwards and get the address of the value.
  auto addr_node = fxl::MakeRefCounted<AddressOfExprNode>(
      fxl::MakeRefCounted<TestExprNode>(true, out_value));

  called = false;
  out_err = Err();
  out_value = ExprValue();
  addr_node->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                           ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
  });

  // Taking the address should always complete synchronously.
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();

  // The value should be the address.
  ASSERT_EQ(8u, out_value.data().size());
  EXPECT_EQ(kAddress, out_value.GetAs<uint64_t>());

  // The type should be a pointer modifier on the old type. The pointer
  // modifier will be a dynamically created one so won't match the original we
  // made above, but the underlying "const int" should still match.
  const ModifiedType* out_mod_type = out_value.type()->AsModifiedType();
  ASSERT_TRUE(out_mod_type);
  EXPECT_EQ(Symbol::kTagPointerType, out_mod_type->tag());
  EXPECT_EQ(const_base_type.get(),
            out_mod_type->modified().Get()->AsModifiedType());
  EXPECT_EQ("const uint32_t*", out_mod_type->GetFullName());

  // Try to dereference an invalid address.
  ExprValue bad_ptr_value(const_ptr_type, {0, 0, 0, 0, 0, 0, 0, 0});
  auto bad_deref_node = fxl::MakeRefCounted<DereferenceExprNode>(
      fxl::MakeRefCounted<TestExprNode>(true, bad_ptr_value));
  called = false;
  out_err = Err();
  out_value = ExprValue();
  bad_deref_node->Eval(context, [&called, &out_err, &out_value](
                                    const Err& err, ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // Should complete asynchronously.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_TRUE(out_err.has_error());
  EXPECT_EQ("Invalid pointer 0x0", out_err.msg());
}

TEST_F(ExprNodeTest, ArrayAccess) {
  // The base address of the array (of type uint32_t*).
  auto uint32_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 4, "uint32_t");
  auto uint32_ptr_type = fxl::MakeRefCounted<ModifiedType>(
      Symbol::kTagPointerType, LazySymbol(uint32_type));
  constexpr uint64_t kAddress = 0x12345678;
  ExprValue pointer_value(uint32_ptr_type,
                          {0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00});
  auto pointer_node = fxl::MakeRefCounted<TestExprNode>(false, pointer_value);

  // The index of the array access.
  constexpr uint32_t kIndex = 5;
  auto index = fxl::MakeRefCounted<TestExprNode>(false, ExprValue(kIndex));

  // The node to evaluate the access. Note the pointer are index nodes are
  // moved here so the source reference is gone. This allows us to test that
  // they stay in scope during an async call below.
  auto access = fxl::MakeRefCounted<ArrayAccessExprNode>(
      std::move(pointer_node), std::move(index));

  // We expect it to read @ kAddress[kIndex]. Insert a value there.
  constexpr uint64_t kExpectedAddr = kAddress + 4 * kIndex;
  constexpr uint32_t kExpectedValue = 0x11223344;
  auto context = fxl::MakeRefCounted<TestEvalContext>();
  context->data_provider()->AddMemory(kExpectedAddr, {0x44, 0x33, 0x22, 0x11});

  // Execute.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  access->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                        ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // The two parts of the expression were set as async above, so it should not
  // have been called yet.
  EXPECT_FALSE(called);

  // Clear out references to the stuff being executed. It should not crash, the
  // relevant data should remain alive.
  context = fxl::RefPtr<TestEvalContext>();
  access = fxl::RefPtr<ArrayAccessExprNode>();

  loop().Run();

  // Should have succeeded asynchronously.
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();

  // Should have found our data at the right place.
  EXPECT_EQ(uint32_type.get(), out_value.type());
  EXPECT_EQ(kExpectedValue, out_value.GetAs<uint32_t>());
  EXPECT_EQ(kExpectedAddr, out_value.source().address());
}

// This is more of an integration smoke test for "." and "->". The details are
// tested in resolve_member_unittest.cc.
TEST_F(ExprNodeTest, MemberAccess) {
  auto context = fxl::MakeRefCounted<TestEvalContext>();

  // Define a class.
  auto int32_type = MakeInt32Type();
  auto sc = MakeStruct2Members("Foo", int32_type, "a", int32_type, "b");

  // Set up a call to do "." synchronously.
  auto struct_node = fxl::MakeRefCounted<TestExprNode>(
      true, ExprValue(sc, {0x78, 0x56, 0x34, 0x12}));
  auto access_node = fxl::MakeRefCounted<MemberAccessExprNode>(
      struct_node, ExprToken(ExprToken::Type::kDot, ".", 0),
      ExprToken(ExprToken::Type::kName, "a", 0));

  // Do the call.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  access_node->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                             ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // Should have run synchronsly.
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(0x12345678, out_value.GetAs<int32_t>());

  // Test indirection: "foo->a".
  auto foo_ptr_type = fxl::MakeRefCounted<ModifiedType>(Symbol::kTagPointerType,
                                                        LazySymbol(sc));
  // Add memory in two chunks since the mock data provider can only respond
  // with the addresses it's given.
  constexpr uint64_t kAddress = 0x1000;
  context->data_provider()->AddMemory(kAddress, {0x44, 0x33, 0x22, 0x11});
  context->data_provider()->AddMemory(kAddress + 4, {0x88, 0x77, 0x66, 0x55});

  // Make this one evaluate the left-hand-size asynchronosly. This value
  // references kAddress (little-endian).
  auto struct_ptr_node = fxl::MakeRefCounted<TestExprNode>(
      false, ExprValue(foo_ptr_type,
                       {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
  auto access_ptr_node = fxl::MakeRefCounted<MemberAccessExprNode>(
      struct_ptr_node, ExprToken(ExprToken::Type::kArrow, "->", 0),
      ExprToken(ExprToken::Type::kName, "b", 0));

  // Do the call.
  called = false;
  out_err = Err();
  out_value = ExprValue();
  access_ptr_node->Eval(context, [&called, &out_err, &out_value](
                                     const Err& err, ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // Should have run asynchronsly.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  EXPECT_EQ(sizeof(int32_t), out_value.data().size());
  EXPECT_EQ(0x55667788, out_value.GetAs<int32_t>());
}

}  // namespace zxdb
