// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_APP_H_
#define GARNET_BIN_UI_SKETCHY_APP_H_

#include "garnet/bin/ui/sketchy/canvas.h"
#include "lib/app/cpp/application_context.h"
#include "lib/escher/escher.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/ui/fun/sketchy/fidl/canvas.fidl.h"

namespace sketchy_service {

class App {
 public:
  App(escher::Escher* escher);

 private:
  fsl::MessageLoop* loop_;
  std::unique_ptr<app::ApplicationContext> context_;
  ui_mozart::MozartPtr mozart_;
  std::unique_ptr<scenic_lib::Session> session_;
  f1dl::BindingSet<sketchy::Canvas> bindings_;
  std::unique_ptr<CanvasImpl> canvas_;
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_APP_H_
