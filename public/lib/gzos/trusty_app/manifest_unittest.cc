// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gzos/trusty_app/manifest.h"

#include "gtest/gtest.h"

namespace trusty_app {

TEST(TrustyManifest, UUID) {
  auto manifest = Manifest::CreateFrom(
      R"JSON({ "uuid": "bff17336-86ac-11e8-adc0-fa7ae01bbebc" })JSON");

  ASSERT_TRUE(manifest != nullptr);
  EXPECT_STREQ(manifest->GetUuid().c_str(),
               "bff17336-86ac-11e8-adc0-fa7ae01bbebc");
}

}  // namespace trusty_app
