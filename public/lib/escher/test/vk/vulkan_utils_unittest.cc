// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/vulkan_utils.h"

#include "garnet/public/lib/escher/test/gtest_escher.h"

namespace {
using namespace escher;

bool IsEnclosedBy(const vk::Rect2D& rect,
                  const vk::Rect2D& potential_encloser) {
  int64_t left, right, top, bottom, encloser_left, encloser_right, encloser_top,
      encloser_bottom;

  left = rect.offset.x;
  right = left + rect.extent.width;
  top = rect.offset.y;
  bottom = top + rect.extent.height;

  encloser_left = potential_encloser.offset.x;
  encloser_right = encloser_left + potential_encloser.extent.width;
  encloser_top = potential_encloser.offset.y;
  encloser_bottom = encloser_top + potential_encloser.extent.height;

  return left >= encloser_left && right <= encloser_right &&
         top >= encloser_top && bottom <= encloser_bottom;
}

TEST(VulkanUtils, ClipToRect) {
  vk::Rect2D rect, encloser{{1000, 1000}, {2000, 2000}};

  rect = vk::Rect2D({500, 500}, {3000, 3000});
  EXPECT_FALSE(IsEnclosedBy(rect, encloser));
  impl::ClipToRect(&rect, encloser);
  EXPECT_TRUE(IsEnclosedBy(rect, encloser));
  EXPECT_EQ(rect, encloser);

  rect = vk::Rect2D({500, 500}, {2000, 2000});
  EXPECT_FALSE(IsEnclosedBy(rect, encloser));
  impl::ClipToRect(&rect, encloser);
  EXPECT_TRUE(IsEnclosedBy(rect, encloser));
  EXPECT_NE(rect, encloser);
  EXPECT_EQ(rect, vk::Rect2D({1000, 1000}, {1500, 1500}));

  rect = vk::Rect2D({1200, 1200}, {200, 200});
  EXPECT_TRUE(IsEnclosedBy(rect, encloser));
  vk::Rect2D copy = rect;
  impl::ClipToRect(&rect, encloser);
  EXPECT_EQ(rect, copy);
}

}  // anonymous namespace
