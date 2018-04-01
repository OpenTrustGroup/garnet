// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_ESCHER_TEST_ENVIRONMENT_H_
#define GARNET_LIB_UI_GFX_TESTS_ESCHER_TEST_ENVIRONMENT_H_

#include "gtest/gtest.h"
#include "lib/escher/escher.h"

class DemoHarness;

namespace scenic {
namespace gfx {
namespace test {

class EscherTestEnvironment {
 public:
  void SetUp(std::string tests_name);
  void TearDown();
  escher::Escher* escher() { return escher_.get(); }

 private:
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<DemoHarness> escher_demo_harness_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_TESTS_ESCHER_TEST_ENVIRONMENT_H_
