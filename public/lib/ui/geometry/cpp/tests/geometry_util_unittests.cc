// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/geometry/cpp/geometry_util.h"

#include <array>

#include "gtest/gtest.h"

namespace geometry {
namespace {

Transform CreateTransformFromData(const std::array<float, 16>& data) {
  Transform transform;
  memcpy(transform.matrix.mutable_data(), &data.front(), 16 * sizeof(float));
  return transform;
}

Transform CreateTestTransform() {
  return CreateTransformFromData(
      {{0.34, 123.7, 89.22, 65.17, 871.12, 87.34, -0.3, -887, 76.2, 2.22222332,
        11.00992, -19, 42, 42, 42, 42}});
}

void ExpectTransformsAreFloatEq(const Transform& lhs, const Transform& rhs) {
  for (size_t row = 0; row < 4; row++) {
    for (size_t col = 0; col < 4; col++) {
      size_t idx = row * 4 + col;
      EXPECT_FLOAT_EQ(lhs.matrix.at(idx), rhs.matrix.at(idx))
          << "row=" << row << ", col=" << col;
    }
  }
}

TEST(RectTest, Comparisons) {
  Rect r1;
  r1.x = 0;
  r1.y = 1;
  r1.width = 2;
  r1.height = 3;

  EXPECT_EQ(r1, r1);

  Rect r2 = r1;
  r2.x = 4;

  EXPECT_NE(r1, r2);

  r2 = r1;
  r2.y = 5;

  EXPECT_NE(r1, r2);

  r2 = r1;
  r2.width = 6;

  EXPECT_NE(r1, r2);

  r2 = r1;
  r2.height = 7;

  EXPECT_NE(r1, r2);
}

TEST(SizeTest, Comparisons) {
  Size s1;
  s1.width = 0;
  s1.height = 1;

  EXPECT_EQ(s1, s1);

  Size s2 = s1;
  s2.width = 2;

  EXPECT_NE(s1, s2);

  s2 = s1;
  s2.height = 3;

  EXPECT_NE(s1, s2);
}

TEST(PointTest, Comparisons) {
  Point p1;
  p1.x = 0;
  p1.y = 1;

  EXPECT_EQ(p1, p1);

  Point p2 = p1;
  p2.x = 2;

  EXPECT_NE(p1, p2);

  p2 = p1;
  p2.y = 3;

  EXPECT_NE(p1, p2);
}

TEST(TransformFunctionsTest, SetIdentityTransform) {
  Transform identity = CreateTransformFromData(
      {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}});
  Transform transform = CreateTestTransform();

  SetIdentityTransform(&transform);
  ExpectTransformsAreFloatEq(identity, transform);
}

TEST(TransformFunctionsTest, SetTranslationTransform) {
  const float x = 0.5;
  const float y = 10.2;
  const float z = -1.5;

  Transform translated = CreateTransformFromData(
      {{1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1}});
  Transform transform = CreateTestTransform();

  SetTranslationTransform(&transform, x, y, z);
  ExpectTransformsAreFloatEq(translated, transform);
}

TEST(TransformFunctionsTest, Translate) {
  const float x = 10.2;
  const float y = 0.5;
  const float z = -4.5;

  Transform transform = CreateTestTransform();
  Transform transform_pristine = transform;
  TransformPtr transformed = Transform::New();
  *transformed = transform;

  transform_pristine.matrix.at(0 * 4 + 3) += x;
  transform_pristine.matrix.at(1 * 4 + 3) += y;
  transform_pristine.matrix.at(2 * 4 + 3) += z;

  transformed = Translate(std::move(transformed), x, y, z);
  Translate(&transform, x, y, z);

  ExpectTransformsAreFloatEq(transform_pristine, *transformed);
  ExpectTransformsAreFloatEq(transform_pristine, transform);
}

TEST(TransformFunctionsTest, Scale) {
  const float x = 2.5;
  const float y = -10.2;
  const float z = -7.3;

  Transform transform = CreateTestTransform();
  Transform transform_pristine = transform;
  TransformPtr transformed = Transform::New();
  *transformed = transform;

  transform_pristine.matrix.at(0 * 4 + 0) *= x;
  transform_pristine.matrix.at(1 * 4 + 1) *= y;
  transform_pristine.matrix.at(2 * 4 + 2) *= z;

  transformed = Scale(std::move(transformed), x, y, z);
  Scale(&transform, x, y, z);

  ExpectTransformsAreFloatEq(transform_pristine, *transformed);
  ExpectTransformsAreFloatEq(transform_pristine, transform);
}

TEST(TransformFunctionsTest, CreateIdentityTransform) {
  Transform identity = CreateTransformFromData(
      {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}});

  ExpectTransformsAreFloatEq(identity, *CreateIdentityTransform());
}

TEST(TransformFunctionsTest, CreateTranslationTransform) {
  const float x = -0.5;
  const float y = 123.2;
  const float z = -9.2;

  Transform translation = CreateTransformFromData(
      {{1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1}});

  ExpectTransformsAreFloatEq(translation, *CreateTranslationTransform(x, y, z));
}

TEST(TransformFunctionsTest, CreateScaleTransform) {
  const float x = 0.5;
  const float y = 10.2;
  const float z = -1.5;

  Transform translation = CreateTransformFromData(
      {{x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1}});

  ExpectTransformsAreFloatEq(translation, *CreateScaleTransform(x, y, z));
}

}  // namespace
}  // namespace geometry
