// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "fuchsia/sys/cpp/fidl.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace sysmgr {

using ManifestCallback = std::function<void(
    const std::string& app_name, const std::string& manifest_path,
    const rapidjson::Document& document)>;

void ForEachManifest(ManifestCallback callback);

void ParsePublicServices(
    const rapidjson::Document& document,
    std::function<void(const std::string& service)> callback);

void MountPackageData(fuchsia::sys::LaunchInfo& launch_info);

};  // namespace sysmgr
