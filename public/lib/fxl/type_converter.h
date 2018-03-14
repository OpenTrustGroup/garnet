// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_TYPE_CONVERTER_H_
#define LIB_FXL_TYPE_CONVERTER_H_

namespace fxl {

// Specialize the following class:
//   template <typename T, typename U> struct TypeConverter;
// to perform desired type conversions. Here, T is the target type; U is the
// input type.
//
// To convert from type Y to type X, create a specialization of TypeConverter
// like this:
//
//   namespace fxl {
//
//   template <>
//   struct TypeConverter<X, Y> {
//     static X Convert(const Y& input);
//   };
//
//   } // namespace fxl
//
// With this specialization, it's possible to write code like this:
//
//   Y y;
//   X x = fxl::To<X>(y);
//

template <typename T, typename U>
struct TypeConverter;

template <typename T, typename U>
inline T To(const U& obj) {
  return TypeConverter<T, U>::Convert(obj);
};

}  // namespace fxl

#endif  // LIB_FXL_TYPE_CONVERTER_H_
