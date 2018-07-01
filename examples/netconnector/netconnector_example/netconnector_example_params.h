// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_NETCONNECTOR_NETCONNECTOR_EXAMPLE_NETCONNECTOR_EXAMPLE_PARAMS_H_
#define GARNET_EXAMPLES_NETCONNECTOR_NETCONNECTOR_EXAMPLE_NETCONNECTOR_EXAMPLE_PARAMS_H_

#include <string>

#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace examples {

class NetConnectorExampleParams {
 public:
  NetConnectorExampleParams(const fxl::CommandLine& command_line);

  bool is_valid() const { return is_valid_; }

  bool register_provider() const { return register_provider_; }

  const std::string& request_device_name() const {
    return request_device_name_;
  }

 private:
  void Usage();

  bool is_valid_;
  bool register_provider_;
  std::string request_device_name_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetConnectorExampleParams);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_NETCONNECTOR_NETCONNECTOR_EXAMPLE_NETCONNECTOR_EXAMPLE_PARAMS_H_
