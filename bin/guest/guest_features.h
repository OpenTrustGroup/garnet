// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_GUEST_FEATURES_H_
#define GARNET_BIN_GUEST_GUEST_FEATURES_H_

#include <utility>

#include "garnet/lib/features/feature_provider_client.h"

using GuestFeatures = features::FeatureSet;

inline fxl::RefPtr<callback::Cancellable> LoadGuestFeatures(
    component::ApplicationContext* context,
    features::LoadFeaturesCallback callback) {
  return features::LoadFromFeatureProvider(context, std::move(callback));
}
#endif  // GARNET_BIN_GUEST_GUEST_FEATURES_H_
