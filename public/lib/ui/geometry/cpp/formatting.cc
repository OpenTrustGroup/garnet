// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/geometry/cpp/formatting.h"

#include <ostream>

#include "lib/fxl/logging.h"

namespace geometry {

std::ostream& operator<<(std::ostream& os, const geometry::Point& value) {
  return os << "{x=" << value.x << ", y=" << value.y << "}";
}

std::ostream& operator<<(std::ostream& os, const geometry::PointF& value) {
  return os << "{x=" << value.x << ", y=" << value.y << "}";
}

std::ostream& operator<<(std::ostream& os, const geometry::Rect& value) {
  return os << "{x=" << value.x << ", y=" << value.y
            << ", width=" << value.width << ", height=" << value.height << "}";
}

std::ostream& operator<<(std::ostream& os, const geometry::RectF& value) {
  return os << "{x=" << value.x << ", y=" << value.y
            << ", width=" << value.width << ", height=" << value.height << "}";
}

std::ostream& operator<<(std::ostream& os, const geometry::RRectF& value) {
  return os << "{x=" << value.x << ", y=" << value.y
            << ", width=" << value.width << ", height=" << value.height
            << ", top_left_radius_x=" << value.top_left_radius_x
            << ", top_left_radius_y=" << value.top_left_radius_y
            << ", top_right_radius_x=" << value.top_right_radius_x
            << ", top_right_radius_y=" << value.top_right_radius_y
            << ", bottom_left_radius_x=" << value.bottom_left_radius_x
            << ", bottom_left_radius_y=" << value.bottom_left_radius_y
            << ", bottom_right_radius_x=" << value.bottom_right_radius_x
            << ", bottom_right_radius_y=" << value.bottom_right_radius_y << "}";
}

std::ostream& operator<<(std::ostream& os, const geometry::Size& value) {
  return os << "{width=" << value.width << ", height=" << value.height << "}";
}

std::ostream& operator<<(std::ostream& os, const geometry::SizeF& value) {
  return os << "{width=" << value.width << ", height=" << value.height << "}";
}

std::ostream& operator<<(std::ostream& os, const geometry::Inset& value) {
  return os << "{left=" << value.left << ", top=" << value.top
            << ", right=" << value.right << ", bottom=" << value.bottom << "}";
}

std::ostream& operator<<(std::ostream& os, const geometry::InsetF& value) {
  return os << "{left=" << value.left << ", top=" << value.top
            << ", right=" << value.right << ", bottom=" << value.bottom << "}";
}

std::ostream& operator<<(std::ostream& os, const geometry::Transform& value) {
  FXL_DCHECK(value.matrix.count() == 16);
  os << "[";
  for (size_t i = 0; i < 4; i++) {
    if (i != 0)
      os << ", ";
    os << "[";
    for (size_t j = 0; j < 4; j++) {
      if (j != 0)
        os << ", ";
      os << value.matrix.at(i * 4 + j);
    }
    os << "]";
  }
  os << "]";
  return os;
}

}  // namespace geometry
