// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simple Fuchsia program that connects to the test_runner process,
// starts a test and exits with success or failure based on the success or
// failure of the test.

#include <lib/async-loop/cpp/loop.h>

#include "lib/fxl/command_line.h"
#include "lib/fxl/functional/closure.h"
#include "lib/test_runner/cpp/test_runner.h"

class QuitObserver : public test_runner::TestRunObserver {
 public:
  QuitObserver(async::Loop* loop) : loop_(loop) {}

  void SendMessage(const std::string& test_id, const std::string& operation,
                   const std::string& msg) override {}

  void Teardown(const std::string& test_id, bool success) override {
    success_ = success;
    loop_->Quit();
  }

  bool success() { return success_; }

 private:
  async::Loop* const loop_;
  bool success_;
};

int main(int argc, char** argv) {
  std::shared_ptr<component::StartupContext> app_context =
      component::StartupContext::CreateFromStartupInfo();
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fxl::CommandLine settings = fxl::CommandLineFromArgcArgv(argc, argv);
  std::vector<std::string> args = settings.positional_args();

  auto url = args.front();
  args.erase(args.begin());

  QuitObserver observer(&loop);
  test_runner::TestRunContext context(
      app_context,
      &observer,
      "test",
      url,
      args);

  loop.Run();
  return !observer.success();
}
