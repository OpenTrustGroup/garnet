// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TRACE_MANAGER_H_
#define GARNET_BIN_TRACE_MANAGER_TRACE_MANAGER_H_

#include <list>

#include "lib/app/cpp/application_context.h"
#include "lib/tracing/fidl/trace_controller.fidl.h"
#include "lib/tracing/fidl/trace_registry.fidl.h"
#include "garnet/bin/trace_manager/config.h"
#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "garnet/bin/trace_manager/trace_session.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/one_shot_timer.h"

namespace tracing {

class TraceManager : public TraceRegistry, public TraceController {
 public:
  TraceManager(app::ApplicationContext* context, const Config& config);
  ~TraceManager() override;

 private:
  // |TraceController| implementation.
  void StartTracing(TraceOptionsPtr options,
                    zx::socket output,
                    const StartTracingCallback& cb) override;
  void StopTracing() override;
  void DumpProvider(uint32_t provider_id, zx::socket output) override;
  void GetKnownCategories(const GetKnownCategoriesCallback& callback) override;
  void GetRegisteredProviders(
      const GetRegisteredProvidersCallback& callback) override;

  // |TraceRegistry| implementation.
  void RegisterTraceProvider(
      f1dl::InterfaceHandle<tracing::TraceProvider> provider,
      const f1dl::String& label) override;

  void FinalizeTracing();
  void LaunchConfiguredProviders();

  app::ApplicationContext* const context_;
  const Config& config_;

  uint32_t next_provider_id_ = 1u;
  fxl::RefPtr<TraceSession> session_;
  fxl::OneShotTimer session_finalize_timeout_;
  std::list<TraceProviderBundle> providers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TraceManager);
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACE_MANAGER_H_
