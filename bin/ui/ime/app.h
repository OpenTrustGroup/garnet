// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_IME_APP_H_
#define GARNET_BIN_UI_IME_APP_H_

#include <memory>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "lib/ui/input/fidl/ime_service.fidl.h"
#include "lib/ui/input/fidl/input_events.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace ime {

class ImeImpl;

class App : public mozart::ImeService {
 public:
  explicit App(const fxl::CommandLine& command_line);
  ~App();

 private:
  // |mozart::ImeService|
  void GetInputMethodEditor(
      mozart::KeyboardType keyboard_type,
      mozart::InputMethodAction action,
      mozart::TextInputStatePtr initial_state,
      f1dl::InterfaceHandle<mozart::InputMethodEditorClient> client,
      f1dl::InterfaceRequest<mozart::InputMethodEditor> editor) override;

  void OnImeDisconnected(ImeImpl* ime);

  std::unique_ptr<app::ApplicationContext> application_context_;
  std::vector<std::unique_ptr<ImeImpl>> ime_;
  f1dl::BindingSet<mozart::ImeService> ime_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace ime

#endif  // GARNET_BIN_UI_IME_APP_H_
