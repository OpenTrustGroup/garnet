// SPDX-License-Identifier: BSD-3-Clause


// Copyright 2023 GoldenRiver Inc.
// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/tracing.h>
#include <lib/async/cpp/loop.h>
#include <lib/async/cpp/wait.h>

#include "garnet/lib/trace_converters/chromium_exporter.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/time/time_delta.h"
#include "third_party/hee-trusty/nbl_tracer/tracer.h"

namespace nbl_vmm {

struct Options {
  std::vector<std::string> categories = {"app", "kernel"};
  fxl::TimeDelta duration = fxl::TimeDelta::FromSeconds(10);
  uint32_t buffer_size_megabytes_hint = 4;
  std::string output_file_name = "/system/data/trace.json";
};

class Tracer {
 public:
  explicit Tracer(component::ApplicationContext* context);
  ~Tracer();
  void Start();

 private:
  void StopTrace();
  void DoneTrace();
  void StartTimer();

  std::unique_ptr<tracing::ChromiumExporter> exporter_;
  std::unique_ptr<tracing::Tracer> tracer_;
  bool tracing_ = false;
  Options options_;

  tracing::TraceControllerPtr trace_controller_;
  fxl::WeakPtrFactory<Tracer> weak_ptr_factory_;
  async::Loop loop_;
};

}  // namespace nbl_vmm