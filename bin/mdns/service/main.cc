// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/mdns/service/mdns_service_impl.h"
#include "lib/app/cpp/startup_context.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context =
      fuchsia::sys::StartupContext::CreateFromStartupInfo();

  mdns::MdnsServiceImpl impl(startup_context.get());

  loop.Run();
  return 0;
}
