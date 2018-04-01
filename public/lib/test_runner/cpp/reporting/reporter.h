// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TEST_RUNNER_CPP_REPORTING_REPORTER_H_
#define LIB_TEST_RUNNER_CPP_REPORTING_REPORTER_H_

#include <vector>

#include "lib/app/cpp/application_context.h"
#include <fuchsia/cpp/test_runner.h>

namespace test_runner {

void ReportResult(std::string identity, component::ApplicationContext* context,
                  std::vector<TestResultPtr> results);

}  // namespace test_runner

#endif  // LIB_TEST_RUNNER_CPP_REPORTING_REPORTER_H_
