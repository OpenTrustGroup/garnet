// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "third_party/rapidjson/rapidjson/document.h"

namespace trusty_app {

class Manifest {
 public:
  static Manifest* Instance();
  static Manifest* CreateFrom(std::string data);

  std::string GetUuid();

 private:
  Manifest() = default;

  void Parse(const std::string& string);
  bool ParseUuid(rapidjson::Document& document);

  std::string uuid_;
};

}  // namespace trusty_app
