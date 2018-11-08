// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gzos/samples/echo/cpp/fidl.h>

#include "lib/gzos/client_app/cpp/agent.h"
#include "lib/fxl/logging.h"

using gzos::samples::echo::EchoService;
using gzos::samples::echo::EchoServiceSyncPtr;

int main() {
  gzos::ipc::Agent agent;

  EchoServiceSyncPtr echo;
  agent.ConnectToService<EchoService>(echo.NewRequest());

  fidl::StringPtr resp;
  echo->EchoString("Hello World", &resp);

  FXL_LOG(INFO) << "Resp: " << resp.get();
  return 0;
}
