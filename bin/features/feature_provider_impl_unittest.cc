// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/features/feature_provider_impl.h"

#include <zircon/errors.h>

#include "gtest/gtest.h"

namespace features {
namespace {

FeatureSet MakeFeatureSet(bool virtio_console_enabled,
                          bool virtio_rtc_enabled = false,
                          bool smc_enabled = false,
                          bool pvblk_enabled = false,
                          bool cvm_lock_enabled = false,
                          bool wdt_sink_enabled = false) {
  FeatureSet::Values values;
  values.virtio_console_enabled = virtio_console_enabled;
  values.virtio_rtc_enabled = virtio_rtc_enabled;
  values.smc_enabled = smc_enabled;
  values.pvblk_enabled = pvblk_enabled;
  values.cvm_lock_enabled = cvm_lock_enabled;
  values.wdt_sink_enabled = wdt_sink_enabled;
  return FeatureSet::FromValues(values);
}

TEST(FeatureProviderImplTest, DoesNotLoadBeforeFirstRequest) {
  bool called = false;
  FeatureProviderImpl provider([&called](FeatureSet* out) {
    called = true;
    return ZX_OK;
  });

  EXPECT_FALSE(called);
}

TEST(FeatureProviderImplTest, ReturnsAllFeaturesInOneResponse) {
  FeatureProviderImpl provider([](FeatureSet* out) {
    *out = MakeFeatureSet(false);
    return ZX_OK;
  });

  int32_t result = ZX_ERR_INTERNAL;
  FeatureState state = {};
  provider.GetFeatures([&result, &state](int32_t status, FeatureState value) {
    result = status;
    state = value;
  });

  EXPECT_EQ(ZX_OK, result);
  EXPECT_FALSE(state.virtio_console_enabled);
  EXPECT_FALSE(state.mmio_bus_enabled);
  EXPECT_FALSE(state.pci_bus_enabled);
  EXPECT_FALSE(state.virtio_rtc_enabled);
  EXPECT_FALSE(state.pvblk_enabled);
}

TEST(FeatureProviderImplTest, ReturnsRtcFeatureState) {
  FeatureProviderImpl provider([](FeatureSet* out) {
    *out = MakeFeatureSet(false, true);
    return ZX_OK;
  });

  int32_t result = ZX_ERR_INTERNAL;
  FeatureState state = {};
  provider.GetFeatures([&result, &state](int32_t status, FeatureState value) {
    result = status;
    state = value;
  });

  EXPECT_EQ(ZX_OK, result);
  EXPECT_TRUE(state.virtio_rtc_enabled);
}

TEST(FeatureProviderImplTest, ReturnsSmcFeatureState) {
  FeatureProviderImpl provider([](FeatureSet* out) {
    *out = MakeFeatureSet(false, false, true);
    return ZX_OK;
  });

  int32_t result = ZX_ERR_INTERNAL;
  FeatureState state = {};
  provider.GetFeatures([&result, &state](int32_t status, FeatureState value) {
    result = status;
    state = value;
  });

  EXPECT_EQ(ZX_OK, result);
  EXPECT_TRUE(state.smc_enabled);
}

TEST(FeatureProviderImplTest, ReturnsPvblkFeatureState) {
  FeatureProviderImpl provider([](FeatureSet* out) {
    *out = MakeFeatureSet(false, false, false, true);
    return ZX_OK;
  });

  int32_t result = ZX_ERR_INTERNAL;
  FeatureState state = {};
  provider.GetFeatures([&result, &state](int32_t status, FeatureState value) {
    result = status;
    state = value;
  });

  EXPECT_EQ(ZX_OK, result);
  EXPECT_TRUE(state.pvblk_enabled);
}

TEST(FeatureProviderImplTest, ReturnsCvmLockFeatureState) {
  FeatureProviderImpl provider([](FeatureSet* out) {
    *out = MakeFeatureSet(false, false, false, false, true);
    return ZX_OK;
  });

  int32_t result = ZX_ERR_INTERNAL;
  FeatureState state = {};
  provider.GetFeatures([&result, &state](int32_t status, FeatureState value) {
    result = status;
    state = value;
  });

  EXPECT_EQ(ZX_OK, result);
  EXPECT_TRUE(state.cvm_lock_enabled);
}

TEST(FeatureProviderImplTest, ReturnsWdtSinkFeatureState) {
  FeatureProviderImpl provider([](FeatureSet* out) {
    *out = MakeFeatureSet(false, false, false, false, false, true);
    return ZX_OK;
  });

  int32_t result = ZX_ERR_INTERNAL;
  FeatureState state = {};
  provider.GetFeatures([&result, &state](int32_t status, FeatureState value) {
    result = status;
    state = value;
  });

  EXPECT_EQ(ZX_OK, result);
  EXPECT_TRUE(state.wdt_sink_enabled);
}

TEST(FeatureProviderImplTest, DoesNotCacheFailedLoad) {
  int calls = 0;
  FeatureProviderImpl provider([&calls](FeatureSet* out) {
    ++calls;
    if (calls == 1) {
      return ZX_ERR_UNAVAILABLE;
    }
    *out = MakeFeatureSet(false);
    return ZX_OK;
  });

  int32_t result = ZX_OK;
  FeatureState state = {};
  provider.GetFeatures([&result, &state](int32_t status, FeatureState value) {
    result = status;
    state = value;
  });
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, result);
  EXPECT_EQ(1, calls);

  provider.GetFeatures([&result, &state](int32_t status, FeatureState value) {
    result = status;
    state = value;
  });
  EXPECT_EQ(ZX_OK, result);
  EXPECT_EQ(2, calls);
  EXPECT_FALSE(state.virtio_console_enabled);
}

TEST(FeatureProviderImplTest, CachesSuccessfulLoad) {
  int calls = 0;
  FeatureProviderImpl provider([&calls](FeatureSet* out) {
    ++calls;
    *out = MakeFeatureSet(calls == 1);
    return ZX_OK;
  });

  int32_t result = ZX_ERR_INTERNAL;
  FeatureState state = {};
  provider.GetFeatures([&result, &state](int32_t status, FeatureState value) {
    result = status;
    state = value;
  });
  EXPECT_EQ(ZX_OK, result);
  EXPECT_TRUE(state.virtio_console_enabled);

  provider.GetFeatures([&result, &state](int32_t status, FeatureState value) {
    result = status;
    state = value;
  });
  EXPECT_EQ(ZX_OK, result);
  EXPECT_EQ(1, calls);
  EXPECT_TRUE(state.virtio_console_enabled);
}

}  // namespace
}  // namespace features
