// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_TESTS_SESSION_TEST_H_
#define GARNET_LIB_UI_SCENIC_TESTS_SESSION_TEST_H_

#include "garnet/lib/ui/scenic/engine/engine.h"
#include "garnet/lib/ui/scenic/engine/session.h"
#include "garnet/lib/ui/scenic/tests/mocks.h"
#include "gtest/gtest.h"
#include "lib/fsl/threading/thread.h"
#include "lib/fxl/synchronization/waitable_event.h"

namespace scene_manager {
namespace test {

class SessionTest : public ::testing::Test,
                    public scene_manager::ErrorReporter,
                    public scene_manager::EventReporter {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  // Subclasses should override to provide their own Engine.
  virtual std::unique_ptr<Engine> CreateEngine();

 protected:
  // |ErrorReporter|
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override;

  // |EventReporter|
  void SendEvents(::fidl::Array<scenic::EventPtr> events) override;

  // Apply the specified Op, and verify that it succeeds.
  bool Apply(scenic::OpPtr op) { return session_->ApplyOp(std::move(op)); }

  template <class ResourceT>
  fxl::RefPtr<ResourceT> FindResource(scenic::ResourceId id) {
    return session_->resources()->FindResource<ResourceT>(id);
  }

  // Verify that the last reported error is as expected.  If no error is
  // expected, use nullptr as |expected_error_string|.
  void ExpectLastReportedError(const char* expected_error_string) {
    if (!expected_error_string) {
      EXPECT_TRUE(reported_errors_.empty());
    } else {
      EXPECT_EQ(reported_errors_.back(), expected_error_string);
    }
  }

  DisplayManager display_manager_;
  std::unique_ptr<Engine> engine_;
  fxl::RefPtr<SessionForTest> session_;
  std::vector<std::string> reported_errors_;
  std::vector<scenic::EventPtr> events_;
};

class SessionThreadedTest : public SessionTest {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

 protected:
  fxl::RefPtr<fxl::TaskRunner> TaskRunner() const;

  void PostTaskSync(fxl::Closure callback);

  void PostTask(fxl::AutoResetWaitableEvent& latch, fxl::Closure callback);

 private:
  fsl::Thread thread_;
};

}  // namespace test
}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_TESTS_SESSION_TEST_H_
