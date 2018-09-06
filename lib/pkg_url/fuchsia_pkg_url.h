// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PKG_URL_FUCHSIA_PKG_URL_H_
#define GARNET_LIB_PKG_URL_FUCHSIA_PKG_URL_H_

#include <string>

namespace component {

class FuchsiaPkgUrl {
 public:
  static bool IsFuchsiaPkgScheme(const std::string& url);

  bool Parse(const std::string& url);

  const std::string& package_name() const { return package_name_; }

  const std::string& resource_path() const { return resource_path_; }

  std::string pkgfs_dir_path();

 private:
  std::string package_name_;
  std::string resource_path_;
};

}  // namespace component

#endif  // GARNET_LIB_PKG_URL_FUCHSIA_PKG_URL_H_
