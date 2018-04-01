// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/sandbox_metadata.h"

#include <algorithm>

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {
namespace {

template <typename Value>
bool CopyArrayToVector(const Value& value, std::vector<std::string>* vector) {
  if (!value.IsArray())
    return false;
  for (const auto& entry : value.GetArray()) {
    if (!entry.IsString())
      return false;
    vector->push_back(entry.GetString());
  }
  return true;
}

}  // namespace

constexpr char kDev[] = "dev";
constexpr char kSystem[] = "system";
constexpr char kPkgfs[] = "pkgfs";
constexpr char kFeatures[] = "features";

SandboxMetadata::SandboxMetadata() = default;

SandboxMetadata::~SandboxMetadata() = default;

bool SandboxMetadata::Parse(const std::string& data) {
  dev_.clear();
  features_.clear();

  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject())
    return false;

  auto dev = document.FindMember(kDev);
  if (dev != document.MemberEnd()) {
    if (!CopyArrayToVector(dev->value, &dev_))
      return false;
  }

  auto system = document.FindMember(kSystem);
  if (system != document.MemberEnd()) {
    if (!CopyArrayToVector(system->value, &system_))
      return false;
  }

  auto pkgfs = document.FindMember(kPkgfs);
  if (pkgfs != document.MemberEnd()) {
    if (!CopyArrayToVector(pkgfs->value, &pkgfs_))
      return false;
  }

  auto features = document.FindMember(kFeatures);
  if (features != document.MemberEnd()) {
    if (!CopyArrayToVector(features->value, &features_))
      return false;
  }

  return true;
}

bool SandboxMetadata::HasFeature(const std::string& feature) {
  return std::find(features_.begin(), features_.end(), feature) !=
         features_.end();
}

}  // namespace component
