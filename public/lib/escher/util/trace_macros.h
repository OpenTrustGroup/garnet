// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_TRACE_MACROS_H_
#define LIB_ESCHER_UTIL_TRACE_MACROS_H_

// See <trace/event.h> for usage documentation.

#include "lib/fxl/build_config.h"

#ifdef OS_FUCHSIA
#include <trace/event.h>
#else
#include "lib/escher/util/impl/trace_macros_impl.h"

#define TRACE_DURATION(category_literal, name_literal, args...) \
  TRACE_INTERNAL_DURATION((category_literal), (name_literal), args)
#endif

#endif  // LIB_ESCHER_UTIL_TRACE_MACROS_H_
