// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc.
// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tracer.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <unordered_set>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zx/time.h>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/types/type_converters.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/fxl/strings/string_number_conversions.h"

namespace nbl_vmm {
zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

Tracer::Tracer(component::ApplicationContext* context)
    : weak_ptr_factory_(this) {
  context->ConnectToEnvironmentService(
      trace_controller_.NewRequest(loop_.async()));

  auto status = loop_.StartThread();
  FXL_CHECK(status == ZX_OK);
}

Tracer::~Tracer() {
  loop_.Quit();
  loop_.JoinThreads();
}

void Tracer::Start() {
  std::ofstream out_file(options_.output_file_name,
                         std::ios_base::out | std::ios_base::trunc);
  if (!out_file.is_open()) {
    FXL_LOG(ERROR) << "Failed to open " << options_.output_file_name
                   << " for writing";
    return;
  }

  exporter_.reset(new tracing::ChromiumExporter(std::move(out_file)));
  tracer_.reset(new tracing::Tracer(trace_controller_.get(), loop_.async()));
  tracing_ = true;

  tracing::TraceOptions trace_options;
  trace_options.categories =
      fxl::To<fidl::VectorPtr<fidl::StringPtr>>(options_.categories);
  trace_options.buffer_size_megabytes_hint =
      options_.buffer_size_megabytes_hint;

  std::vector<uint64_t> tracees = {GetKoid(zx_process_self())};
  trace_options.tracees = fxl::To<fidl::VectorPtr<uint64_t>>(tracees);

  tracer_->Start(
      std::move(trace_options),
      [this](trace::Record record) { exporter_->ExportRecord(record); },
      [](fbl::String error) { FXL_LOG(ERROR) << error.c_str(); },
      [this] {
        if (options_.duration.ToNanoseconds())
          StartTimer();
      },
      [this] { DoneTrace(); });
}

void Tracer::StopTrace() {
  if (tracing_) {
    FXL_LOG(INFO) << "Stopping trace..." << std::endl;
    tracing_ = false;
    tracer_->Stop();
  }
}

void Tracer::DoneTrace() {
  tracer_.reset();
  exporter_.reset();

  FXL_LOG(INFO) << "Trace file written to " << options_.output_file_name
                << std::endl;
}

void Tracer::StartTimer() {
  async::PostDelayedTask(
      async_get_default(),
      [weak = weak_ptr_factory_.GetWeakPtr()] {
        if (weak)
          weak->StopTrace();
      },
      zx::nsec(options_.duration.ToNanoseconds()));
  FXL_LOG(INFO) << "Starting trace; will stop in "
                << options_.duration.ToSecondsF() << " seconds..." << std::endl;
}

}  // namespace nbl_vmm
