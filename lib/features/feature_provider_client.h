// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FEATURES_FEATURE_PROVIDER_CLIENT_H_
#define GARNET_LIB_FEATURES_FEATURE_PROVIDER_CLIENT_H_

#include <functional>

#include <zircon/types.h>

#include "garnet/lib/callback/cancellable.h"
#include "garnet/lib/features/features.h"
#include "lib/app/cpp/application_context.h"

namespace features {

using LoadFeaturesCallback = std::function<void(zx_status_t, FeatureSet)>;

fxl::RefPtr<callback::Cancellable> LoadFromFeatureProvider(
    component::ApplicationContext* context, LoadFeaturesCallback callback);

}  // namespace features
#endif  // GARNET_LIB_FEATURES_FEATURE_PROVIDER_CLIENT_H_
