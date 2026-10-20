// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/app/cpp/application_context.h>

#include "backend_impl.h"
#include "client.h"
#include "transport.h"

int main() {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  std::unique_ptr<component::ApplicationContext> application_context =
      component::ApplicationContext::CreateFromStartupInfo();

  BackendImpl backend;
  if (backend.Init() != ZX_OK) {
    LOG(ERROR) << "Failed to initialize backend";
    return -1;
  }

  SpiTransportClient client;
  SpiTransport transport(application_context.get(), loop.async(), &backend);

  auto req_handle = client.NewRequest();
  transport.Bind(std::move(req_handle));

  if (client.Init() != ZX_OK) {
    LOG(ERROR) << "Failed to initialize client";
    return -1;
  }

  loop.Run();
  return 0;
}
