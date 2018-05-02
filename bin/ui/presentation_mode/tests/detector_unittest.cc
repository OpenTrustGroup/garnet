// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/array.h>
#include <limits>

#include "garnet/bin/ui/presentation_mode/detector.h"
#include "gtest/gtest.h"

namespace presentation_mode {
namespace test {

input::SensorDescriptor CreateAccelerometer(input::SensorLocation loc) {
  input::SensorDescriptor desc;
  desc.type = input::SensorType::ACCELEROMETER;
  desc.loc = loc;
  return desc;
}

constexpr int16_t kMaxVal = std::numeric_limits<int16_t>::max();
constexpr int16_t kMinVal = std::numeric_limits<int16_t>::min();
constexpr AccelerometerData kZero = {0, 0, 0};
const input::SensorDescriptor kBaseSensor =
    CreateAccelerometer(input::SensorLocation::BASE);
const input::SensorDescriptor kLidSensor =
    CreateAccelerometer(input::SensorLocation::LID);

// Exercise expected values over partial and full history (ie 0-4 events), as
// well as overflow and underflow values.
TEST(PositiveData, MovingAverage) {
  constexpr AccelerometerData kMax = {kMaxVal, kMaxVal, kMaxVal};

  presentation_mode::internal::MovingAverage mv(/*history*/ 3);
  EXPECT_EQ(mv.Average(), kZero);

  mv.Update(kMax);
  EXPECT_EQ(mv.Average(), kMax);

  mv.Update(kMax);
  EXPECT_EQ(mv.Average(), kMax);

  mv.Update(kMax);
  EXPECT_EQ(mv.Average(), kMax);

  mv.Update(kMax);
  EXPECT_EQ(mv.Average(), kMax);
}

TEST(NegativeData, MovingAverage) {
  constexpr AccelerometerData kMin = {kMinVal, kMinVal, kMinVal};

  presentation_mode::internal::MovingAverage mv(/*history*/ 3);
  EXPECT_EQ(mv.Average(), kZero);

  mv.Update(kMin);
  EXPECT_EQ(mv.Average(), kMin);

  mv.Update(kMin);
  EXPECT_EQ(mv.Average(), kMin);

  mv.Update(kMin);
  EXPECT_EQ(mv.Average(), kMin);

  mv.Update(kMin);
  EXPECT_EQ(mv.Average(), kMin);
}

input::InputReport CreateVector(int16_t x, int16_t y, int16_t z) {
  input::InputReport report;
  report.sensor = input::SensorReport::New();
  fidl::Array<int16_t, 3> data;
  data[0] = x;
  data[1] = y;
  data[2] = z;
  report.sensor->set_vector(data);
  return report;
}

TEST(Closed, Detector) {
  Detector detector(/*history*/ 2);

  input::InputReport base_report = CreateVector(0, 0, kMaxVal);
  std::pair<bool, presentation::PresentationMode> result =
      detector.Update(kBaseSensor, std::move(base_report));
  EXPECT_FALSE(result.first);

  input::InputReport lid_report = CreateVector(0, 0, kMinVal);
  result = detector.Update(kLidSensor, std::move(lid_report));
  EXPECT_TRUE(result.first);
  EXPECT_EQ(result.second, presentation::PresentationMode::CLOSED);

  input::InputReport base_shift = CreateVector(0, 0, kMinVal);
  result = detector.Update(kBaseSensor, std::move(base_shift));
  EXPECT_FALSE(result.first);
}

TEST(Laptop, Detector) {
  Detector detector(/*history*/ 2);

  input::InputReport base_report = CreateVector(0, 0, kMaxVal);
  std::pair<bool, presentation::PresentationMode> result =
      detector.Update(kBaseSensor, std::move(base_report));
  EXPECT_FALSE(result.first);

  input::InputReport lid_report = CreateVector(0, kMaxVal, 0);
  result = detector.Update(kLidSensor, std::move(lid_report));
  EXPECT_TRUE(result.first);
  EXPECT_EQ(result.second, presentation::PresentationMode::LAPTOP);

  input::InputReport base_shift = CreateVector(0, 0, kMinVal);
  result = detector.Update(kBaseSensor, std::move(base_shift));
  EXPECT_FALSE(result.first);
}

TEST(Tablet, Detector) {
  Detector detector(/*history*/ 2);

  input::InputReport base_report = CreateVector(0, 0, kMinVal);
  std::pair<bool, presentation::PresentationMode> result =
      detector.Update(kBaseSensor, std::move(base_report));
  EXPECT_FALSE(result.first);

  input::InputReport lid_report = CreateVector(0, 0, kMaxVal);
  result = detector.Update(kLidSensor, std::move(lid_report));
  EXPECT_TRUE(result.first);
  EXPECT_EQ(result.second, presentation::PresentationMode::TABLET);

  input::InputReport base_shift = CreateVector(0, 0, kMaxVal);
  result = detector.Update(kBaseSensor, std::move(base_shift));
  EXPECT_FALSE(result.first);
}

TEST(Tent, Detector) {
  Detector detector(/*history*/ 2);

  input::InputReport base_report = CreateVector(0, kMaxVal, 0);
  std::pair<bool, presentation::PresentationMode> result =
      detector.Update(kBaseSensor, std::move(base_report));
  EXPECT_FALSE(result.first);

  input::InputReport lid_report = CreateVector(0, kMinVal, 0);
  result = detector.Update(kLidSensor, std::move(lid_report));
  EXPECT_TRUE(result.first);
  EXPECT_EQ(result.second, presentation::PresentationMode::TENT);

  input::InputReport base_shift = CreateVector(0, kMinVal, 0);
  result = detector.Update(kBaseSensor, std::move(base_shift));
  EXPECT_FALSE(result.first);
}

}  // namespace test
}  // namespace presentation_mode
