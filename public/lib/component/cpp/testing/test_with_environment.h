// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_CPP_TESTING_TEST_WITH_ENVIRONMENT_H_
#define LIB_COMPONENT_CPP_TESTING_TEST_WITH_ENVIRONMENT_H_

#include <fs/pseudo-dir.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "lib/component/cpp/testing/enclosing_environment.h"
#include "lib/component/cpp/testing/launcher_impl.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/gtest/real_loop_fixture.h"

namespace component {
namespace testing {

// Test fixture for tests to run Components inside a new isolated Environment,
// wrapped in a enclosing Environment.
//
// The new isolated Environment, provided to the Component under test, is not
// visible to any real Environments, such as the Environment that the test
// program was launched in.
//
// That isloated environment needs to be created using
// |CreateNewEnclosingEnvironment*| APIs.
//
// The isolated Environment is enclosed in a enclosing Environment, allowing the
// test to provide Loader, Services, and other Directories that are visible to
// Components under test, and only to those Components.
//
//
// This fixture also allows you to create components in the real environment in
// which this test was launched. Those components should only be used to
// validate real system state.
//
// To use this fixture you need to whitelist "fuchsia.sys.Environment" and
// "fuchsia.sys.Loader" in your component manifest file in "sandbox.services".
// It is necessary because this fixture needs to access
// "fuchsia.sys.Environment" and if you create a |EnclosingEnvironment| then it
// will need access to parent's "fuchsia.sys.Loader" to serve it own Loader
// service.
//
// If you are going to create a new EnclosingEnvironment then you also need to
// run your own loop to serve services provided by that environment. So use of
// SyncPtrs is not advisable unless you can run a loop safely in a new thread
// which stops running before you kill your EnclosingEnvironment.
class TestWithEnvironment : public gtest::RealLoopFixture {
 protected:
  TestWithEnvironment();

  fuchsia::sys::LauncherPtr launcher_ptr() {
    fuchsia::sys::LauncherPtr launcher;
    real_launcher_.AddBinding(launcher.NewRequest());
    return launcher;
  }

  const std::shared_ptr<component::Services>& real_services() {
    return real_services_;
  }

  // Creates a new enclosing environment inside current real environment with
  // the given services.
  //
  // This environment and components created in it will not have access to any
  // of services(except Loader) and resources from the real environment unless
  // explicitly allowed by calling AllowPublicService.
  //
  // After all services are added/passed through to the environment, you must
  // call Launch() to actually start it.
  std::unique_ptr<EnclosingEnvironment> CreateNewEnclosingEnvironment(
      const std::string& label,
      std::unique_ptr<EnvironmentServices> services) const {
    return EnclosingEnvironment::Create(label, real_env_, std::move(services));
  }

  // Returns an EnvironmentServices object that the caller can use to pass
  // services to a new EnclosingEnvironment.
  //
  // The returned object has the parent's loader, but no other services by
  // default.
  std::unique_ptr<EnvironmentServices> CreateServices() {
    return EnvironmentServices::Create(real_env_);
  }

  std::unique_ptr<EnvironmentServices> CreateServicesWithCustomLoader(
      const fbl::RefPtr<fs::Service>& loader_service) {
    return EnvironmentServices::CreateWithCustomLoader(real_env_,
                                                       loader_service);
  }

  // Creates component in current real environment. This component will have
  // access to the services, directories and other resources from the
  // environment in which your test was launched.
  //
  // This should be mostly used for observing the state of system and for
  // nothing else. For eg. Try launching "glob" component and validate how it
  // behaves in various environments.
  void CreateComponentInCurrentEnvironment(
      fuchsia::sys::LaunchInfo launch_info,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> request);

  // Returns true if environment was created.
  //
  // You should either use this function to wait or run your own loop if you
  // want CreateComponent* to succed on |enclosing_environment|.
  bool WaitForEnclosingEnvToStart(
      const EnclosingEnvironment* enclosing_environment,
      zx::duration timeout = zx::sec(5)) {
    return RunLoopWithTimeoutOrUntil(
        [enclosing_environment] { return enclosing_environment->is_running(); },
        timeout);
  }

 private:
  std::shared_ptr<component::Services> real_services_;
  fuchsia::sys::EnvironmentPtr real_env_;
  LauncherImpl real_launcher_;
};

}  // namespace testing
}  // namespace component

#endif  // LIB_COMPONENT_CPP_TESTING_TEST_WITH_ENVIRONMENT_H_
