// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_TOOL_MDNS_PARAMS_H_
#define GARNET_BIN_MDNS_TOOL_MDNS_PARAMS_H_

#include <string>

#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace mdns {

class MdnsParams {
 public:
  enum class CommandVerb {
    kVerbose,
    kQuiet,
    kResolve,
    kSubscribe,
    kPublish,
    kUnpublish,
    kRespond,
  };

  MdnsParams(const fxl::CommandLine& command_line);

  bool is_valid() const { return is_valid_; }
  CommandVerb command_verb() const { return command_verb_; }
  const std::string& host_name() const { return host_name_; }
  const std::string& service_name() const { return service_name_; }
  const std::string& instance_name() const { return instance_name_; }
  uint16_t port() const { return port_; }
  uint32_t timeout_seconds() const { return timeout_seconds_; }
  const std::vector<std::string>& text() const { return text_; }
  const std::vector<std::string>& announce() const { return announce_; }

 private:
  void Usage();
  bool Parse(const std::string& string_value, uint16_t* out);
  bool Parse(const std::string& string_value, uint32_t* out);
  bool Parse(const std::string& string_value, std::vector<std::string>* out);
  bool ParseHostName(const std::string& string_value, std::string* out);
  bool ParseServiceName(const std::string& string_value, std::string* out);
  bool ParseInstanceName(const std::string& string_value, std::string* out);

  bool is_valid_;
  CommandVerb command_verb_;
  std::string host_name_;
  std::string service_name_;
  std::string instance_name_;
  uint16_t port_;
  uint32_t timeout_seconds_ = 10;
  std::vector<std::string> text_;
  std::vector<std::string> announce_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MdnsParams);
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_TOOL_MDNS_PARAMS_H_
