// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/cmx_metadata.h"

#include "gtest/gtest.h"

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {
namespace {

TEST(CmxMetadata, ParseSandboxMetadata) {
  CmxMetadata cmx;
  rapidjson::Value& sandbox = cmx.ParseSandboxMetadata(
      R"JSON({ "sandbox": { "dev": [ "class/input" ]}, "other": "stuff" })JSON");

  EXPECT_TRUE(sandbox.IsObject());
  EXPECT_TRUE(sandbox.HasMember("dev"));
  EXPECT_FALSE(sandbox.HasMember("other"));
}

TEST(CmxMetadata, GetCmxPath) {
  EXPECT_EQ("meta/sysmgr.cmx",
            CmxMetadata::GetCmxPath("file:///pkgfs/packages/sysmgr/0"));
  EXPECT_EQ("", CmxMetadata::GetCmxPath("file:///pkgfs/nothing/sysmgr/0"));
}

}  // namespace
}  // namespace component
