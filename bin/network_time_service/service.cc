// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time_service/service.h"
#include "lib/syslog/cpp/logger.h"

#include <zircon/syscalls.h>
#include <fstream>

namespace network_time_service {

TimeServiceImpl::TimeServiceImpl(
    std::unique_ptr<component::StartupContext> context,
    const char server_config_path[])
    : context_(std::move(context)), time_server_(server_config_path) {
  context_->outgoing().AddPublicService(bindings_.GetHandler(this));
}

TimeServiceImpl::~TimeServiceImpl() = default;

void TimeServiceImpl::Update(uint8_t num_retries, UpdateCallback callback) {
  callback(time_server_.UpdateSystemTime(num_retries));
}

}  // namespace network_time_service
