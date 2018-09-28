// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/namespace.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/errors.h>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/component/cpp/service_provider_impl.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/svc/cpp/services.h"
#include "lib/gtest/real_loop_fixture.h"

using fuchsia::sys::ServiceList;
using fuchsia::sys::ServiceListPtr;
using fuchsia::sys::ServiceProvider;
using fuchsia::sys::ServiceProviderPtr;

namespace component {
namespace {

class NamespaceTest : public ::gtest::RealLoopFixture {
 protected:
  fxl::RefPtr<Namespace> MakeNamespace(
      ServiceListPtr additional_services,
      fxl::RefPtr<Namespace> parent = nullptr) {
    return fxl::MakeRefCounted<Namespace>(
        parent, nullptr, std::move(additional_services), nullptr);
  }

  zx_status_t ConnectToService(zx_handle_t svc_dir, const std::string& name) {
    zx::channel h1, h2;
    zx_status_t r = zx::channel::create(0, &h1, &h2);
    if (r != ZX_OK) return r;
    fdio_service_connect_at(svc_dir, name.c_str(), h2.release());
    return ZX_OK;
  }
};

class NamespaceHostDirectoryTest : public NamespaceTest {
 protected:
  NamespaceHostDirectoryTest()
      : vfs_(dispatcher()),
        directory_(fbl::AdoptRef(new fs::PseudoDir())) {}

  zx::channel OpenAsDirectory() {
    return testing::OpenAsDirectory(&vfs_, directory_);
  }

  zx_status_t AddService(const std::string& name) {
    auto cb = [this, name](zx::channel channel) {
      ++connection_ctr_[name];
      return ZX_OK;
    };
    return directory_->AddEntry(name, fbl::AdoptRef(new fs::Service(cb)));
  }

  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> directory_;
  std::map<std::string, int> connection_ctr_;
};

class NamespaceProviderTest : public NamespaceTest {
 protected:
  fxl::RefPtr<Namespace> MakeNamespace(ServiceListPtr additional_services) {
    return fxl::MakeRefCounted<Namespace>(
        nullptr, nullptr, std::move(additional_services), nullptr);
  }

  void BindServiceProvider(fidl::InterfaceRequest<ServiceProvider> request) {
    provider_.AddBinding(std::move(request));
  }

  zx_status_t AddService(const std::string& name) {
    auto cb = [this, name](zx::channel channel) {
      ++connection_ctr_[name];
    };
    provider_.AddServiceForName(cb, name);
    return ZX_OK;
  }

  ServiceProviderImpl provider_;
  std::map<std::string, int> connection_ctr_;
};

std::pair<std::string, int> StringIntPair(const std::string& s, int i) {
  return {s, i};
}

TEST_F(NamespaceHostDirectoryTest, AdditionalServices) {
  static constexpr char kService1[] = "fuchsia.test.TestService1";
  static constexpr char kService2[] = "fuchsia.test.TestService2";
  ServiceListPtr service_list(new ServiceList);
  service_list->names.push_back(kService1);
  service_list->names.push_back(kService2);
  AddService(kService1);
  AddService(kService2);
  ServiceProviderPtr service_provider;
  service_list->host_directory = OpenAsDirectory();
  auto ns = MakeNamespace(std::move(service_list));

  zx::channel svc_dir = ns->OpenServicesAsDirectory();
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService1));
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService2));
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService2));
  // fdio_service_connect_at does not return an error if connection failed.
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), "fuchsia.test.NotExists"));
  RunLoopUntilIdle();
  std::vector<std::pair<std::string, int>> connection_ctr_vec;
  for (const auto& e : connection_ctr_) {
    connection_ctr_vec.push_back(e);
  }
  EXPECT_THAT(connection_ctr_vec, ::testing::ElementsAre(
      StringIntPair(kService1, 1), StringIntPair(kService2, 2)));
}

TEST_F(NamespaceHostDirectoryTest, AdditionalServices_InheritParent) {
  static constexpr char kService1[] = "fuchsia.test.TestService1";
  static constexpr char kService2[] = "fuchsia.test.TestService2";
  ServiceListPtr parent_service_list(new ServiceList);
  parent_service_list->names.push_back(kService1);
  ServiceListPtr service_list(new ServiceList);
  service_list->names.push_back(kService2);
  AddService(kService1);
  AddService(kService2);
  parent_service_list->host_directory = OpenAsDirectory();
  service_list->host_directory = OpenAsDirectory();
  auto parent_ns = MakeNamespace(std::move(parent_service_list));
  auto ns = MakeNamespace(std::move(service_list), parent_ns);

  zx::channel svc_dir = ns->OpenServicesAsDirectory();
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService1));
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService2));
  // fdio_service_connect_at does not return an error if connection failed.
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), "fuchsia.test.NotExists"));
  RunLoopUntilIdle();
  std::vector<std::pair<std::string, int>> connection_ctr_vec;
  for (const auto& e : connection_ctr_) {
    connection_ctr_vec.push_back(e);
  }
  EXPECT_THAT(connection_ctr_vec, ::testing::ElementsAre(
      StringIntPair(kService1, 1), StringIntPair(kService2, 1)));
}

TEST_F(NamespaceProviderTest, AdditionalServices) {
  static constexpr char kService1[] = "fuchsia.test.TestService1";
  static constexpr char kService2[] = "fuchsia.test.TestService2";
  ServiceListPtr service_list(new ServiceList);
  ServiceProviderPtr service_provider;
  BindServiceProvider(service_provider.NewRequest());
  service_list->names.push_back(kService1);
  service_list->names.push_back(kService2);
  EXPECT_EQ(ZX_OK, AddService(kService1));
  EXPECT_EQ(ZX_OK, AddService(kService2));
  service_list->provider = std::move(service_provider);
  auto ns = MakeNamespace(std::move(service_list));

  zx::channel svc_dir = ns->OpenServicesAsDirectory();
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService1));
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService2));
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), kService2));
  // fdio_service_connect_at does not return an error if connection failed.
  EXPECT_EQ(ZX_OK, ConnectToService(svc_dir.get(), "fuchsia.test.NotExists"));
  RunLoopUntilIdle();
  std::vector<std::pair<std::string, int>> connection_ctr_vec;
  for (const auto& e : connection_ctr_) {
    connection_ctr_vec.push_back(e);
  }
  EXPECT_THAT(connection_ctr_vec, ::testing::ElementsAre(
      StringIntPair(kService1, 1), StringIntPair(kService2, 2)));
}

}  // namespace
}  // namespace component
