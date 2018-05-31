// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CALLBACK_TO_FUNCTION_H_
#define LIB_CALLBACK_TO_FUNCTION_H_

#include <functional>
#include <utility>

#include "lib/callback/ensure_copyable.h"
#include "lib/fxl/functional/make_copyable.h"

namespace callback {
namespace internal {

template <typename T>
struct LambdaType {
  using type = void;
};

template <typename Ret, typename Class, typename... Args>
struct LambdaType<Ret (Class::*)(Args...) const> {
  using type = std::function<Ret(Args...)>;
};

}  // namespace internal

// Returns a std::function from a lambda.
template <typename F>
typename ::callback::internal::LambdaType<decltype(&F::operator())>::type
ToStdFunction(F func) {
  return EnsureCopyable(std::move(func));
}

}  // namespace callback

#endif  // LIB_CALLBACK_TO_FUNCTION_H_
