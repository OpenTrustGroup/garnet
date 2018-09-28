// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_GEOMETRY_BOUNDING_BOX_H_
#define LIB_ESCHER_GEOMETRY_BOUNDING_BOX_H_

#include "lib/escher/geometry/types.h"

namespace escher {

class BoundingBox {
 public:
  // Non-empty box.  No error-checking; it is up to the caller to ensure that
  // all components of max are >= the corresponding component of min.
  BoundingBox(vec3 min, vec3 max);
  // Empty box.
  constexpr BoundingBox() : min_{1, 1, 1}, max_{0, 0, 0} {}
  // Return an empty box if max < min along any of the coordinate axes,
  // or if max == min along more than |max_degenerate_dimensions| of the
  // coordinate axes.  Otherwise return a non-empty box.
  static BoundingBox NewChecked(vec3 min, vec3 max,
                                uint32_t max_degenerate_dimensions = 0);

  const vec3& min() const { return min_; }
  const vec3& max() const { return max_; }

  bool operator==(const BoundingBox& box) const {
    return min_ == box.min_ && max_ == box.max_;
  }
  bool operator!=(const BoundingBox& box) const { return !(*this == box); }

  // Expand this bounding box to encompass the other.  Return this box.
  BoundingBox& Join(const BoundingBox& box);

  // Shrink this box to be the intersection of this with the other.  If the
  // boxes do not intersect, this box becomes empty.  Return this box.
  BoundingBox& Intersect(const BoundingBox& box);

  // Return true if the other box is completely contained by this one.
  bool Contains(const BoundingBox& box) const {
    // We don't need to check if this box is empty, because the way we define
    // an empty box guarantees that the subsequent tests can't pass.
    return glm::all(glm::lessThanEqual(min_, box.min_)) &&
           glm::all(glm::greaterThanEqual(max_, box.max_)) && !box.is_empty();
  }

  bool is_empty() const { return *this == BoundingBox(); }

  // Return the number of bounding box corners that are clipped by the the
  // specified plane (between 0 and 8).  Since this is a 2D plane, the z
  // coordinate is ignored, and only 4 corners need to be tested.
  uint32_t NumClippedCorners(const plane2& plane) const;

  // Return the number of bounding box corners that are clipped by the the
  // specified plane (between 0 and 8).
  uint32_t NumClippedCorners(const plane3& plane) const;

 private:
  vec3 min_;
  vec3 max_;
};

// Return a new BoundingBox that encloses the 8 corners of this box, after
// they are transformed by the matrix.  Note: this can cause the box to grow,
// e.g. if you rotate it by 45 degrees.
BoundingBox operator*(const mat4& matrix, const BoundingBox& box);

// Return a new Bounding box by translating the input box.
inline BoundingBox operator+(const vec3& translation, const BoundingBox& box) {
  return box.is_empty()
             ? BoundingBox()
             : BoundingBox(box.min() + translation, box.max() + translation);
}

// Return a new Bounding box by translating the input box.
inline BoundingBox operator+(const BoundingBox& box, const vec3& translation) {
  return translation + box;
}

// Debugging.
ESCHER_DEBUG_PRINTABLE(BoundingBox);

}  // namespace escher

#endif  // LIB_ESCHER_GEOMETRY_BOUNDING_BOX_H_
