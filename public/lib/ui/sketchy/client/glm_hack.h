// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SKETCHY_CLIENT_GLM_HACK_H_
#define LIB_UI_SKETCHY_CLIENT_GLM_HACK_H_

// Workaround for compiler error due to Magenta defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway.
//
// Tracked by MG-377.

#if defined(countof)
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
#include <glm/glm.hpp>
#endif

#endif  // LIB_UI_SKETCHY_CLIENT_GLM_HACK_H_
