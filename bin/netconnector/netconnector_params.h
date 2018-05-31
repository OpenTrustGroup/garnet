// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <unordered_map>

#include "garnet/bin/netconnector/ip_address.h"
#include <component/cpp/fidl.h>
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace netconnector {

class NetConnectorParams {
 public:
  NetConnectorParams(const fxl::CommandLine& command_line);

  bool is_valid() const { return is_valid_; }

  bool listen() const { return listen_; }

  bool show_devices() const { return show_devices_; }
  bool mdns_verbose() const { return mdns_verbose_; }

  std::unordered_map<std::string, component::LaunchInfoPtr>
  MoveServices() {
    return std::move(launch_infos_by_service_name_);
  }

  const std::unordered_map<std::string, IpAddress>& devices() {
    return device_addresses_by_name_;
  }

  void RegisterDevice(const std::string& name, const IpAddress& address);

  void UnregisterDevice(const std::string& name);

 private:
  void Usage();

  bool ReadConfigFrom(const std::string& config_file);

  bool ParseConfig(const std::string& string);

  void RegisterService(const std::string& selector,
                       component::LaunchInfoPtr launch_info);

  bool is_valid_;
  bool listen_ = false;
  bool show_devices_ = false;
  bool mdns_verbose_ = false;
  std::unordered_map<std::string, component::LaunchInfoPtr>
      launch_infos_by_service_name_;
  std::unordered_map<std::string, IpAddress> device_addresses_by_name_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetConnectorParams);
};

}  // namespace netconnector
