// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_TIME_SERVICE_TIME_SERVICE_H_
#define GARNET_BIN_NETWORK_TIME_SERVICE_TIME_SERVICE_H_

#include <vector>

#include <fuchsia/timezone/cpp/fidl.h>
#include "garnet/bin/network_time/timezone.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"

namespace network_time_service {

// Implementation of the FIDL time service. Handles setting/getting the
// timezone offset by ICU timezone ID.  Also supports getting the raw UTC
// offset in minutes.
//
// For information on ICU ID's and timezone information see:
// http://userguide.icu-project.org/formatparse/datetime
class TimeServiceImpl : public fuchsia::timezone::TimeService {
 public:
  // Constructs the time service with a caller-owned application context.
  TimeServiceImpl(std::unique_ptr<component::StartupContext> context,
                  const char server_config_path[]);
  ~TimeServiceImpl();

  // |TimeServiceImpl|:
  void Update(uint8_t num_retries, UpdateCallback callback) override;

 private:
  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::timezone::TimeService> bindings_;
  time_server::Timezone time_server_;
};

}  // namespace network_time_service

#endif  // GARNET_BIN_TIMEZONE_TIMEZONE_H_
