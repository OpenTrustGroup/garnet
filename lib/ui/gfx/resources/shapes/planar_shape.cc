// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/shapes/planar_shape.h"

namespace scenic {
namespace gfx {

PlanarShape::PlanarShape(Session* session,
                         scenic::ResourceId id,
                         const ResourceTypeInfo& type_info)
    : Shape(session, id, type_info) {}

bool PlanarShape::GetIntersection(const escher::ray4& ray,
                                  float* out_distance) const {
  // Reject if the ray origin is behind the Z=0 plane.
  if (ray.origin.z < 0.f)
    return false;

  // Reject if the ray is not pointing down towards the Z=0 plane.
  float delta_z = -ray.direction.z;
  if (delta_z < std::numeric_limits<float>::epsilon())
    return false;

  // Compute the distance to the plane in multiples of the ray's direction
  // vector and the point of intersection.
  float distance = ray.origin.z / delta_z;
  escher::vec2 point =
      (escher::vec2(ray.origin) + distance * escher::vec2(ray.direction)) /
      ray.origin.w;

  // Reject if the shape does not contain the point of intersection.
  if (!ContainsPoint(point))
    return false;

  *out_distance = distance;
  return true;
}

}  // namespace gfx
}  // namespace scenic
