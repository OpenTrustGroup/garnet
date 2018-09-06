// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>

#include "gmock/gmock.h"
#include "lib/component/cpp/environment_services_helper.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/component/cpp/testing/test_with_environment.h"
#include "lib/fxl/files/glob.h"
#include "lib/fxl/strings/substitute.h"
#include "lib/svc/cpp/services.h"

namespace component {
namespace {

using ::fxl::Substitute;
using ::testing::ElementsAre;
using testing::EnclosingEnvironment;
using ::testing::UnorderedElementsAre;

const char kTestComponent[] = "inspect_test_app";

class InspectTest : public component::testing::TestWithEnvironment {
 protected:
  InspectTest() {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kTestComponent;

    environment_ = CreateNewEnclosingEnvironment("test");
    environment_->CreateComponent(std::move(launch_info),
                                  controller_.NewRequest());
    WaitForEnclosingEnvToStart(environment_.get());

    // TODO(CP-130): Remove this when we can wait for a component to be ready.
    RunLoopWithTimeoutOrUntil(
        [this] { return files::Glob(GetObjectPath("*/.channel")).size() > 0; },
        zx::sec(5));
  }
  ~InspectTest() { CheckShutdown(); }

  void CheckShutdown() {
    controller_->Kill();
    bool done = false;
    controller_.events().OnTerminated =
        [&done](int64_t code, fuchsia::sys::TerminationReason reason) {
          ASSERT_EQ(fuchsia::sys::TerminationReason::EXITED, reason);
          done = true;
        };
    ASSERT_TRUE(
        RunLoopWithTimeoutOrUntil([&done] { return done; }, zx::sec(10)));
  }

  std::string GetObjectPath(const std::string& relative_path) {
    files::Glob glob(
        Substitute("/hub/r/test/*/c/$0/*/out/objects", kTestComponent));
    if (glob.size() == 0) {
      return "";
    }

    std::string path = *glob.begin();

    return Substitute("$0/$1", path, relative_path);
  }

  std::vector<std::string> GetGlob(const std::string& path) {
    files::Glob glob(GetObjectPath("*"));
    return std::vector<std::string>{glob.begin(), glob.end()};
  }

 private:
  std::unique_ptr<EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
};

TEST_F(InspectTest, InspectTopLevel) {
  EXPECT_THAT(
      GetGlob(GetObjectPath("*")),
      ElementsAre(GetObjectPath("table-t1"), GetObjectPath("table-t2")));
}

MATCHER_P2(UIntMetric, name, value, "") {
  return arg.key == name && arg.value.is_uint_value() &&
         arg.value.uint_value() == (uint64_t)value;
}

TEST_F(InspectTest, InspectOpenRead) {
  fuchsia::inspect::InspectSyncPtr inspect;

  ASSERT_EQ(ZX_OK,
            fdio_service_connect(GetObjectPath("table-t1/.channel").c_str(),
                                 inspect.NewRequest().TakeChannel().release()));

  fidl::VectorPtr<fidl::StringPtr> children;
  ASSERT_EQ(ZX_OK, inspect->ListChildren(&children));
  EXPECT_THAT(*children, ElementsAre("item-0x0", "item-0x1"));

  fuchsia::inspect::Object obj;
  ASSERT_EQ(ZX_OK, inspect->ReadData(&obj));
  EXPECT_EQ("table-t1", obj.name);
  EXPECT_THAT(*obj.properties, UnorderedElementsAre(fuchsia::inspect::Property(
                                   {.key = "version", .value = "1.0"})));
  EXPECT_THAT(*obj.metrics, UnorderedElementsAre(UIntMetric("item_size", 32)));
}

}  // namespace
}  // namespace component
