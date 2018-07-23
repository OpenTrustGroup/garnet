// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/synchronous-vfs.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/gzos/sysmgr/service_provider_impl.h"
#include "gtest/gtest.h"

namespace sysmgr {

class ServiceProviderTest : public ::testing::Test {
 public:
  ServiceProviderTest()
      : root_(fbl::AdoptRef(new fs::PseudoDir)),
        loop_(&kAsyncLoopConfigMakeDefault),
        vfs_(loop_.async()) {}

 protected:
  void SetUp() override {
    service_provider_.AddBinding(ta_services_.NewRequest());
    service_provider_.set_backing_dir(GetRootDirectory());
  }

  zx::channel GetRootDirectory() {
    zx::channel h1, h2;
    if (zx::channel::create(0, &h1, &h2) != ZX_OK)
      return zx::channel();
    if (vfs_.ServeDirectory(root_, std::move(h1)) != ZX_OK)
      return zx::channel();
    return h2;
  }

  ServiceProviderImpl service_provider_;
  ServiceProviderPtr ta_services_;
  fbl::RefPtr<fs::PseudoDir> root_;
  async::Loop loop_;
  fs::SynchronousVfs vfs_;
};

TEST_F(ServiceProviderTest, AddService) {
  zx::channel srv, cli;
  ASSERT_EQ(zx::channel::create(0, &srv, &cli), ZX_OK);
  zx_handle_t expected_handle = cli.get();
  zx_handle_t actual_handle;

  ta_services_->AddService("foo", [&actual_handle](zx::channel channel) {
    actual_handle = channel.get();
  });

  ta_services_->ConnectToService("foo", std::move(cli));

  loop_.RunUntilIdle();
  EXPECT_EQ(expected_handle, actual_handle);
}

TEST_F(ServiceProviderTest, WaitOnService) {
  zx::channel srv, cli;
  ASSERT_EQ(zx::channel::create(0, &srv, &cli), ZX_OK);
  zx_handle_t expected_handle = cli.get();
  zx_handle_t actual_handle;

  ta_services_->WaitOnService("foo", [this, &cli] {
    ta_services_->ConnectToService("foo", std::move(cli));
  });

  ta_services_->AddService("foo", [&actual_handle](zx::channel channel) {
    actual_handle = channel.get();
  });

  loop_.RunUntilIdle();
  EXPECT_EQ(expected_handle, actual_handle);
}

TEST_F(ServiceProviderTest, LookupService) {
  bool result;
  ta_services_->LookupService("foo", [&result](bool found) { result = found; });

  loop_.RunUntilIdle();
  EXPECT_FALSE(result);

  ta_services_->AddService("foo", [](zx::channel channel) {});

  ta_services_->LookupService("foo", [&result](bool found) { result = found; });

  loop_.RunUntilIdle();
  EXPECT_TRUE(result);
}

TEST_F(ServiceProviderTest, Fallback) {
  zx::channel srv, cli;
  ASSERT_EQ(zx::channel::create(0, &srv, &cli), ZX_OK);
  zx_handle_t expected_handle = cli.get();
  zx_handle_t actual_handle;

  root_->AddEntry(
      "foo",
      fbl::AdoptRef(new fs::Service([&actual_handle](zx::channel channel) {
        actual_handle = channel.get();
        return ZX_OK;
      })));

  ta_services_->ConnectToService("foo", std::move(cli));

  loop_.RunUntilIdle();
  EXPECT_EQ(expected_handle, actual_handle);
}

};  // namespace sysmgr
