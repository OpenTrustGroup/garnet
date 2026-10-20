// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FEATURES_FEATURE_PROVIDER_IMPL_H_
#define GARNET_BIN_FEATURES_FEATURE_PROVIDER_IMPL_H_

#include <functional>

#include <fuchsia/cpp/features.h>

#include "garnet/lib/features/features.h"

namespace features {

using LoadFeatures = std::function<zx_status_t(FeatureSet*)>;

FeatureState ToFidlFeatureState(const FeatureSet& features);

class FeatureProviderImpl : public FeatureProvider {
 public:
  explicit FeatureProviderImpl(
      LoadFeatures load_features = FeatureSet::LoadFromSysinfo);

  void GetFeatures(GetFeaturesCallback callback) override;

 private:
  zx_status_t EnsureLoaded();

  LoadFeatures load_features_;
  bool loaded_ = false;
  FeatureSet features_;
};

}  // namespace features

#endif  // GARNET_BIN_FEATURES_FEATURE_PROVIDER_IMPL_H_
