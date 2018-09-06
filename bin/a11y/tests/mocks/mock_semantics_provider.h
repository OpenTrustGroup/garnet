// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SEMANTICS_PROVIDER_H_
#define GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SEMANTICS_PROVIDER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace accessibility_test {

class MockSemanticsProvider : public fuchsia::accessibility::SemanticsProvider {
 public:
  // On initialization, MockSemanticsProvider tries to connect to
  // |fuchsia::accessibility::SemanticsRoot| service in |context_| and
  // registers with its |view_id_| and |binding|.
  MockSemanticsProvider(component::StartupContext* context, int32_t view_id);
  ~MockSemanticsProvider() = default;

  // These functions directly call the |fuchsia::accessibility::SemanticsRoot|
  // functions for this semantics provider.
  void UpdateSemanticsNodes(
      fidl::VectorPtr<fuchsia::accessibility::Node> update_nodes);
  void DeleteSemanticsNodes(fidl::VectorPtr<int32_t> delete_nodes);
  void Commit();

 private:
  void PerformAccessibilityAction(
      int32_t node_id, fuchsia::accessibility::Action action) override {}

  fidl::Binding<fuchsia::accessibility::SemanticsProvider> binding_;
  component::StartupContext* context_;
  fuchsia::accessibility::SemanticsRootPtr root_;
  int32_t view_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockSemanticsProvider);
};

}  // namespace accessibility_test

#endif  // GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SEMANTICS_PROVIDER_H_