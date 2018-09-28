// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr_value.h"

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

ExprValue::ExprValue() = default;

ExprValue::ExprValue(int8_t value)
    : ExprValue(BaseType::kBaseTypeSigned, "int8_t", &value, sizeof(int8_t)) {}
ExprValue::ExprValue(uint8_t value)
    : ExprValue(BaseType::kBaseTypeUnsigned, "uint8_t", &value,
                sizeof(uint8_t)) {}
ExprValue::ExprValue(int16_t value)
    : ExprValue(BaseType::kBaseTypeSigned, "int16_t", &value, sizeof(int16_t)) {
}
ExprValue::ExprValue(uint16_t value)
    : ExprValue(BaseType::kBaseTypeUnsigned, "uint16_t", &value,
                sizeof(uint16_t)) {}
ExprValue::ExprValue(int32_t value)
    : ExprValue(BaseType::kBaseTypeSigned, "int32_t", &value, sizeof(int32_t)) {
}
ExprValue::ExprValue(uint32_t value)
    : ExprValue(BaseType::kBaseTypeUnsigned, "uint32_t", &value,
                sizeof(uint32_t)) {}
ExprValue::ExprValue(int64_t value)
    : ExprValue(BaseType::kBaseTypeSigned, "int64_t", &value, sizeof(int64_t)) {
}
ExprValue::ExprValue(uint64_t value)
    : ExprValue(BaseType::kBaseTypeUnsigned, "uint64_t", &value,
                sizeof(uint64_t)) {}

ExprValue::ExprValue(int base_type, const char* type_name, void* data,
                     uint32_t data_size)
    : type_(fxl::MakeRefCounted<BaseType>(base_type, data_size, type_name)) {
  data_.resize(data_size);
  memcpy(&data_[0], data, data_size);
}

ExprValue::ExprValue(fxl::RefPtr<Type> type, std::vector<uint8_t> data,
                     const ExprValueSource& source)
    : type_(type), source_(source), data_(data) {}

ExprValue::~ExprValue() = default;

bool ExprValue::operator==(const ExprValue& other) const {
  // Currently this does a comparison of the raw bytes oif the value. This
  // will be fine for most primitive values but will be incorrect for some
  // composite structs.
  return data_ == other.data_;
}

int ExprValue::GetBaseType() const {
  if (!type_)
    return BaseType::kBaseTypeNone;

  // Remove "const", etc. and see if it's a base type.
  const BaseType* base_type = type_->GetConcreteType()->AsBaseType();
  if (!base_type)
    return BaseType::kBaseTypeNone;
  return base_type->base_type();
}

Err ExprValue::EnsureSizeIs(size_t size) const {
  if (data_.size() != size) {
    return Err(
        fxl::StringPrintf("The value of type '%s' is the incorrect size "
                          "(expecting %zu, got %zu). Please file a bug.",
                          type_ ? type_->GetFullName().c_str() : "<unknown>",
                          size, data_.size()));
  }
  return Err();
}

template <>
int8_t ExprValue::GetAs<int8_t>() const {
  FXL_DCHECK(data_.size() == sizeof(int8_t));
  int8_t result;
  memcpy(&result, &data_[0], sizeof(int8_t));
  return result;
}

template <>
uint8_t ExprValue::GetAs<uint8_t>() const {
  FXL_DCHECK(data_.size() == sizeof(uint8_t));
  uint8_t result;
  memcpy(&result, &data_[0], sizeof(uint8_t));
  return result;
}

template <>
int16_t ExprValue::GetAs<int16_t>() const {
  FXL_DCHECK(data_.size() == sizeof(int16_t));
  int16_t result;
  memcpy(&result, &data_[0], sizeof(int16_t));
  return result;
}

template <>
uint16_t ExprValue::GetAs<uint16_t>() const {
  FXL_DCHECK(data_.size() == sizeof(uint16_t));
  uint16_t result;
  memcpy(&result, &data_[0], sizeof(uint16_t));
  return result;
}

template <>
int32_t ExprValue::GetAs<int32_t>() const {
  FXL_DCHECK(data_.size() == sizeof(int32_t));
  int32_t result;
  memcpy(&result, &data_[0], sizeof(int32_t));
  return result;
}

template <>
uint32_t ExprValue::GetAs<uint32_t>() const {
  FXL_DCHECK(data_.size() == sizeof(uint32_t));
  uint32_t result;
  memcpy(&result, &data_[0], sizeof(uint32_t));
  return result;
}

template <>
int64_t ExprValue::GetAs<int64_t>() const {
  FXL_DCHECK(data_.size() == sizeof(int64_t));
  int64_t result;
  memcpy(&result, &data_[0], sizeof(int64_t));
  return result;
}

template <>
uint64_t ExprValue::GetAs<uint64_t>() const {
  FXL_DCHECK(data_.size() == sizeof(uint64_t));
  uint64_t result;
  memcpy(&result, &data_[0], sizeof(uint64_t));
  return result;
}

template <>
float ExprValue::GetAs<float>() const {
  FXL_DCHECK(data_.size() == sizeof(float));
  float result;
  memcpy(&result, &data_[0], sizeof(float));
  return result;
}

template <>
double ExprValue::GetAs<double>() const {
  FXL_DCHECK(data_.size() == sizeof(double));
  double result;
  memcpy(&result, &data_[0], sizeof(double));
  return result;
}

Err ExprValue::PromoteToInt64(int64_t* output) const {
  if (data_.empty())
    return Err("Value has no data.");
  switch (data_.size()) {
    case sizeof(int8_t):
      *output = GetAs<int8_t>();
      break;
    case sizeof(int16_t):
      *output = GetAs<int16_t>();
      break;
    case sizeof(int32_t):
      *output = GetAs<int32_t>();
      break;
    case sizeof(int64_t):
      *output = GetAs<int64_t>();
      break;
    default:
      return Err(fxl::StringPrintf(
          "Unexpected value size (%zu), please file a bug.", data_.size()));
  }
  return Err();
}

Err ExprValue::PromoteToUint64(uint64_t* output) const {
  if (data_.empty())
    return Err("Value has no data.");
  switch (data_.size()) {
    case sizeof(uint8_t):
      *output = GetAs<uint8_t>();
      break;
    case sizeof(uint16_t):
      *output = GetAs<uint16_t>();
      break;
    case sizeof(uint32_t):
      *output = GetAs<uint32_t>();
      break;
    case sizeof(uint64_t):
      *output = GetAs<uint64_t>();
      break;
    default:
      return Err(fxl::StringPrintf(
          "Unexpected value size (%zu), please file a bug.", data_.size()));
  }
  return Err();
}

}  // namespace zxdb
