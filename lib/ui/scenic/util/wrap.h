// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_UTIL_WRAP_H_
#define GARNET_LIB_UI_SCENIC_UTIL_WRAP_H_

#include "lib/escher/geometry/transform.h"
#include "lib/ui/scenic/fidl/types.fidl.h"

namespace scene_manager {

inline scenic::mat4Ptr Wrap(const escher::mat4& args) {
  auto result = scenic::mat4::New();
  auto& m = result->matrix;
  m.resize(16);
  m[0] = args[0][0];
  m[1] = args[0][1];
  m[2] = args[0][2];
  m[3] = args[0][3];
  m[4] = args[1][0];
  m[5] = args[1][1];
  m[6] = args[1][2];
  m[7] = args[1][3];
  m[8] = args[2][0];
  m[9] = args[2][1];
  m[10] = args[2][2];
  m[11] = args[2][3];
  m[12] = args[3][0];
  m[13] = args[3][1];
  m[14] = args[3][2];
  m[15] = args[3][3];
  return result;
}

inline scenic::vec4Ptr Wrap(const escher::vec4& p) {
  auto result = scenic::vec4::New();
  result->x = p[0];
  result->y = p[1];
  result->z = p[2];
  result->w = p[3];
  return result;
}

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_UTIL_WRAP_H_
