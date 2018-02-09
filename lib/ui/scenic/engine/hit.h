// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_ENGINE_HIT_H_
#define GARNET_LIB_UI_SCENIC_ENGINE_HIT_H_

#include "lib/escher/geometry/types.h"

namespace scene_manager {

// Describes where a hit occurred within the content of a tagged node.
struct Hit {
  // The node's tag value.
  uint32_t tag_value;

  // The ray that was used to perform the hit test, in the hit node's coordinate
  // system.
  escher::ray4 ray;

  // The inverse transformation matrix which maps the coordinate system of
  // the hit node to the node at which the hit test was initiated.
  escher::mat4 inverse_transform;

  // The distance from the ray's origin to the closest point of intersection
  // in multiples of the ray's direction vector.  To compute the point of
  // intersection, multiply the ray's direction vector by |distance| and
  // add the ray's origin point.
  float distance;
};

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_ENGINE_HIT_H_
