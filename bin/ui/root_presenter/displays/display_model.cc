// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/displays/display_model.h"

#include <math.h>

#include "lib/fxl/logging.h"

namespace root_presenter {
namespace {

// Returns true if two non-zero values are within 1% of each other.
bool WithinOnePercent(float a, float b) { return fabs((a - b) / b) < 0.01f; }

// Quantizes the specified floating point number to 8 significant bits of
// precision in its mantissa (including the implicit leading 1 bit).
//
// We quantize scale factors to reduce the likelihood of round-off errors in
// subsequent calculations due to excess precision.  Since IEEE 754 float
// has 24 significant bits, by using only 8 significant bits for the scaling
// factor we're guaranteed that we can multiply the factor by any integer
// between -65793 and 65793 without any loss of precision.  The scaled integers
// can likewise be added or subtracted without any loss of precision.
float Quantize(float f) {
  int exp = 0;
  float frac = frexpf(f, &exp);
  return ldexpf(round(frac * 256.0), exp - 8);
}

// The default pixel visual angle.
// This assumes a 96 dpi desktop monitor at arm's length.
constexpr float kDefaultPixelVisualAngleDegrees = 0.0213;

// The ideal visual angle of a pip unit in degrees assuming default
// settings. Empirically determined to be around 0.255.
constexpr float kIdealPipVisualAngleDegrees = 0.0255;

constexpr float GetDefaultViewingDistanceInMm(
    presentation::DisplayUsage usage) {
  switch (usage) {
    case presentation::DisplayUsage::kHandheld:
      return 360.f;
    case presentation::DisplayUsage::kClose:
      return 500.f;
    case presentation::DisplayUsage::kNear:
      return 720.f;
    case presentation::DisplayUsage::kMidrange:
      return 1200.f;
    case presentation::DisplayUsage::kFar:
      return 3000.f;
    default:
    case presentation::DisplayUsage::kUnknown:
      return 0.f;
  }
}

}  // namespace

DisplayModel::DisplayModel() = default;

DisplayModel::~DisplayModel() = default;

DisplayMetrics DisplayModel::GetMetrics() const {
  FXL_DCHECK(display_info_.width_in_px > 0u);
  FXL_DCHECK(display_info_.height_in_px > 0u);

  // Compute the pixel density based on known information.
  // Assumes pixels are square for now.
  float ppm = display_info_.density_in_px_per_mm;
  if (display_info_.width_in_mm > 0.f && display_info_.height_in_mm > 0.f) {
    float xppm = display_info_.width_in_px / display_info_.width_in_mm;
    float yppm = display_info_.height_in_px / display_info_.height_in_mm;
    if (!WithinOnePercent(xppm, yppm)) {
      FXL_DLOG(WARNING) << "The display's pixels are not square: xppm=" << xppm
                        << ", yppm=" << yppm;
    }
    if (ppm <= 0.f) {
      ppm = xppm;
    } else if (!WithinOnePercent(xppm, ppm)) {
      FXL_DLOG(WARNING) << "The display's physical dimensions are inconsistent "
                           "with the density: xppm="
                        << xppm << ", ppm=" << ppm;
    }
  }

  // Compute the nominal viewing distance.
  float vdist = environment_info_.viewing_distance_in_mm;
  if (vdist <= 0.f) {
    vdist = GetDefaultViewingDistanceInMm(environment_info_.usage);
  }

  // Compute the pixel visual size as a function of viewing distance in
  // millimeters per millimeter.
  float pvsize_in_mm_per_mm;
  if (ppm >= 0.f && vdist >= 0.f) {
    pvsize_in_mm_per_mm = 1.f / (ppm * vdist);
  } else {
    pvsize_in_mm_per_mm = tanf(kDefaultPixelVisualAngleDegrees * M_PI / 180);
  }

  // Compute the adaptation factor. This is a empirically-derived fudge factor
  // to take into account human perceptual differences for objects at varying
  // distances, even if those objects are adjusted to be the same size to the
  // eye.
  float adaptation_factor = (vdist * 0.5f + 180.f) / vdist;

  // Compute the pip visual size as a function of viewing distance in
  // millimeters per millimeter.
  float gvsize_in_mm_per_mm = tanf(kIdealPipVisualAngleDegrees * M_PI / 180) *
                              adaptation_factor * user_info_.user_scale_factor;

  // Compute the quantized pip scale factor.
  float scale_in_px_per_pp =
      Quantize(gvsize_in_mm_per_mm / pvsize_in_mm_per_mm);

  // Compute the pip density if we know the physical pixel density.
  float density_in_pp_per_mm = 0.f;
  if (ppm >= 0.f) {
    density_in_pp_per_mm = ppm / scale_in_px_per_pp;
  }

  return DisplayMetrics(display_info_.width_in_px, display_info_.height_in_px,
                        scale_in_px_per_pp, scale_in_px_per_pp,
                        density_in_pp_per_mm);
}

}  // namespace root_presenter
