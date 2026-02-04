// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iostream>

#include "garnet/bin/trace_manager/trace_manager.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fsl/tasks/message_loop.h"

namespace tracing {
namespace {

// For large traces or when verbosity is on it can take awhile to write out
// all the records. E.g., ipm_provider can take 40 seconds with --verbose=2
const fxl::TimeDelta kStopTimeout = fxl::TimeDelta::FromSeconds(60);
static constexpr uint32_t kMinBufferSizeMegabytes = 1;
static constexpr uint32_t kMaxBufferSizeMegabytes = 64;

}  // namespace

TraceManager::TraceManager(component::ApplicationContext* context,
                           const Config& config)
    : context_(context), config_(config) {
}

TraceManager::~TraceManager() = default;

void TraceManager::StartTracing(TraceOptions options,
                                zx::socket output,
                                StartTracingCallback start_callback) {
  if (session_) {
    FXL_LOG(ERROR) << "Trace already in progress";
    return;
  }

  LaunchConfiguredProviders();

  uint32_t buffer_size_megabytes = std::min(
      std::max(options.buffer_size_megabytes_hint, kMinBufferSizeMegabytes),
      kMaxBufferSizeMegabytes);
  FXL_LOG(INFO) << "Starting trace with " << buffer_size_megabytes
                << " MB buffers";

  session_ = fxl::MakeRefCounted<TraceSession>(
      std::move(output), std::move(options.categories),
      buffer_size_megabytes * 1024 * 1024, [this]() { session_ = nullptr; });

  for (auto& bundle : providers_) {
    auto& tracees = options.tracees;
    auto result = find(tracees->begin(), tracees->end(), bundle.pid);
    if (result != tracees->end()) {
      session_->AddProvider(&bundle);
    }
  }

  session_->WaitForProvidersToStart(
      start_callback,
      fxl::TimeDelta::FromMilliseconds(options.start_timeout_milliseconds));
}

void TraceManager::StopTracing() {
  if (!session_)
    return;

  FXL_LOG(INFO) << "Stopping trace";
  session_->Stop(
      [this]() {
        FXL_LOG(INFO) << "Stopped trace";
        session_ = nullptr;

        KillConfiguredProviders();
      },
      kStopTimeout);
}

void TraceManager::GetKnownCategories(GetKnownCategoriesCallback callback) {
  fidl::VectorPtr<KnownCategory> known_categories;
  for (const auto& it : config_.known_categories()) {
    known_categories.push_back(KnownCategory{it.first, it.second});
  }
  callback(std::move(known_categories));
}

void TraceManager::RegisterTraceProvider(
    fidl::InterfaceHandle<trace_link::Provider> handle, uint64_t pid) {
  auto it = providers_.emplace(
      providers_.end(),
      TraceProviderBundle{handle.Bind(), next_provider_id_++, pid});

  FXL_LOG(INFO) << "Registered TraceProvider, pid = " << pid;
  it->provider.set_error_handler([this, it]() {
    if (session_)
      session_->RemoveDeadProvider(&(*it));
    FXL_LOG(INFO) << "Remove TraceProvider, pid = " << it->pid;
    providers_.erase(it);
  });

  if (session_)
    session_->AddProvider(&(*it));
}

void TraceManager::LaunchConfiguredProviders() {
  if (config_.providers().empty())
    return;

  if (!context_->launcher()) {
    FXL_LOG(ERROR)
        << "Cannot access application launcher to launch configured providers";
    return;
  }

  for (const auto& pair : config_.providers()) {
    // TODO(jeffbrown): Only do this if the provider isn't already running.
    // Also keep track of the provider so we can kill it when the trace
    // manager exits or restart it if needed.
    FXL_VLOG(1) << "Starting configured provider: " << pair.first;
    FXL_VLOG(2) << "URL: " << pair.second->url;
    if (FXL_VLOG_IS_ON(2)) {
      std::string args;
      for (const auto& arg : *pair.second->arguments) {
        args += " ";
        args += arg;
      }
      FXL_VLOG(2) << "Args:" << args;
    }
    component::ApplicationLaunchInfo launch_info;
    launch_info.url = pair.second->url;
    fidl::Clone(pair.second->arguments, &launch_info.arguments);

    component::ApplicationControllerPtr app_controller;
    context_->launcher()->CreateApplication(std::move(launch_info),
                                            app_controller.NewRequest());

    provider_controllers_.push_back(std::move(app_controller));
  }
}

void TraceManager::KillConfiguredProviders() {
  provider_controllers_.clear();
}

}  // namespace tracing
