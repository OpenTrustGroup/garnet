// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/env_config.h"

#include "garnet/lib/json/json_parser.h"
#include "lib/fxl/strings/substitute.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace run {
namespace {

using fxl::Substitute;

}  // namespace

void EnvironmentConfig::CreateMap(const std::string& environment_name,
                                  EnvironmentType env_type,
                                  const rapidjson::Document& document) {
  if (!document.HasMember(environment_name)) {
    json_parser_.ReportError(
        Substitute("Environment '$0' not found.", environment_name));
    return;
  }
  const rapidjson::Value& urls = document[environment_name];
  if (!urls.IsArray()) {
    json_parser_.ReportError(Substitute("'$0' section should be an array.",
                                        environment_name));
    return;
  }
  for (const auto& url : urls.GetArray()) {
    if (!url.IsString()) {
      json_parser_.ReportError(Substitute(
          "'$0' section should be a string array.", environment_name));
      return;
    }
    url_map_[url.GetString()] = env_type;
  }
}

bool EnvironmentConfig::ParseFromFile(const std::string& file_path) {
  rapidjson::Document document = json_parser_.ParseFromFile(file_path);
  if (json_parser_.HasError()) {
    return false;
  }
  CreateMap("root", EnvironmentType::ROOT, document);
  CreateMap("sys", EnvironmentType::SYS, document);
  return !json_parser_.HasError();
}

}  // namespace run
