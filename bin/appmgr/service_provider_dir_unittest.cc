// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/service_provider_dir_impl.h"

#include <fidl/examples/echo/cpp/fidl.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include "garnet/bin/appmgr/util.h"
#include "gtest/gtest.h"
#include "lib/fxl/strings/substitute.h"
#include "lib/gtest/real_loop_fixture.h"

namespace component {
namespace {

class FakeEcho : public fidl::examples::echo::Echo {
 public:
  FakeEcho(fidl::InterfaceRequest<fidl::examples::echo::Echo> request)
      : binding_(this) {
    binding_.Bind(std::move(request));
  };
  ~FakeEcho() override {}
  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
    callback(answer_);
  }

  void SetAnswer(fidl::StringPtr answer) { answer_ = answer; }

 private:
  fidl::StringPtr answer_;
  ::fidl::Binding<fidl::examples::echo::Echo> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeEcho);
};

class ServiceProviderTest : public ::gtest::RealLoopFixture {
 protected:
  ServiceProviderTest() : vfs_(dispatcher()) {}

  fbl::RefPtr<fs::Service> CreateService(int set_value) {
    return fbl::AdoptRef(
        new fs::Service([this, set_value](zx::channel channel) {
          value_ = set_value;
          return ZX_OK;
        }));
  }

  fbl::RefPtr<fs::Service> CreateEchoService(fidl::StringPtr answer) {
    return fbl::AdoptRef(new fs::Service([this, answer](zx::channel channel) {
      auto echo = std::make_unique<FakeEcho>(
          fidl::InterfaceRequest<fidl::examples::echo::Echo>(
              std::move(channel)));
      echo->SetAnswer(answer);
      echo_services_.push_back(std::move(echo));
      return ZX_OK;
    }));
  }

  void AddService(ServiceProviderDirImpl* service_provider,
                  const std::string& prefix, int val) {
    AddService(service_provider, prefix, val, CreateService(val));
  }
  void AddService(ServiceProviderDirImpl* service_provider,
                  const std::string& prefix, int i,
                  fbl::RefPtr<fs::Service> svc) {
    const std::string service_name = fxl::Substitute("$0_service$1", prefix,
                                                     std::to_string(i));
    service_provider->AddService(service_name, svc);
  }

  void GetService(ServiceProviderDirImpl* service_provider,
                  const fbl::String& service_name,
                  fbl::RefPtr<fs::Service>* out) {
    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(ZX_OK, service_provider->Lookup(&child, service_name))
        << service_name.c_str();
    *out = fbl::RefPtr<fs::Service>(static_cast<fs::Service*>(child.get()));
  }

  void TestService(ServiceProviderDirImpl* service_provider,
                   const fbl::String& service_name, int expected_value) {
    // Using Lookup.
    value_ = -1;
    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(ZX_OK, service_provider->Lookup(&child, service_name))
        << service_name.c_str();
    fbl::RefPtr<fs::Service> child_node;
    GetService(service_provider, service_name, &child_node);
    ASSERT_TRUE(child_node);
    ASSERT_EQ(ZX_OK, child_node->Serve(&vfs_, zx::channel(), 0));
    EXPECT_EQ(expected_value, value_) << service_name.c_str();

    // Using ConnectToService.
    value_ = -1;
    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    service_provider->ConnectToService(fidl::StringPtr(service_name.c_str()),
                                       std::move(h2));
    EXPECT_EQ(expected_value, value_) << service_name.c_str();
  }

  void TestMissingService(ServiceProviderDirImpl* service_provider,
                          const fbl::String& service_name) {
    // Using Lookup.
    value_ = -1;
    fbl::RefPtr<fs::Vnode> child;
    ASSERT_EQ(ZX_ERR_NOT_FOUND, service_provider->Lookup(&child, service_name))
        << service_name.c_str();
    // Never connected to service.
    EXPECT_EQ(value_, -1) << service_name.c_str();

    // Using ConnectToService.
    value_ = -1;
    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    service_provider->ConnectToService(fidl::StringPtr(service_name.c_str()),
                                       std::move(h2));
    // Never connected to service.
    EXPECT_EQ(value_, -1) << service_name.c_str();
  }

  zx::channel OpenAsDirectory(fbl::RefPtr<ServiceProviderDirImpl> service) {
    return Util::OpenAsDirectory(&vfs_, service);
  }

  fs::SynchronousVfs vfs_;
  int value_ = 0;
  std::vector<std::unique_ptr<FakeEcho>> echo_services_;
};

TEST_F(ServiceProviderTest, SimpleService) {
  ServiceProviderDirImpl service_provider;
  AddService(&service_provider, "my", 1);
  TestService(&service_provider, "my_service1", 1);
  TestMissingService(&service_provider, "nonexistent_service");
}

TEST_F(ServiceProviderTest, Parent) {
  auto service3 = CreateService(3);
  auto service4 = CreateService(4);

  // 'fake_service1' overlaps in parent and child; the child's service should
  // take priority.
  ServiceProviderDirImpl service_provider;
  AddService(&service_provider, "fake", 1);
  auto parent_service_provider = fbl::AdoptRef(new ServiceProviderDirImpl());
  AddService(parent_service_provider.get(), "fake", 1, service3);
  AddService(parent_service_provider.get(), "fake", 2);
  service_provider.set_parent(parent_service_provider);
  AddService(&service_provider, "fake", 2, service4);

  // should call child service
  TestService(&service_provider, "fake_service1", 1);

  // check that we can get parent service, because service4 was added after
  // parent was set
  TestService(&service_provider, "fake_service2", 2);

  // check that parent is able to access its service
  TestService(parent_service_provider.get(), "fake_service1", 3);

  // check nonexistence of bogus service
  TestMissingService(&service_provider, "nonexistent_service");
}

TEST_F(ServiceProviderTest, RestrictedServices) {
  static const std::vector<std::string> kWhitelist{"parent_service1",
                                                   "my_service1"};
  auto parent_service_provider = fbl::AdoptRef(new ServiceProviderDirImpl);
  AddService(parent_service_provider.get(), "parent", 1);
  AddService(parent_service_provider.get(), "parent", 2);
  ServiceProviderDirImpl service_provider(&kWhitelist);
  AddService(&service_provider, "my", 1);
  AddService(&service_provider, "my", 2);
  service_provider.set_parent(parent_service_provider);

  // should be able to call "my_service1" and "parent_service1"
  TestService(&service_provider, "my_service1", 1);
  TestService(&service_provider, "parent_service1", 1);

  // "my_service2" and "parent_service2" are not accessible
  TestMissingService(&service_provider, "my_service2");
  TestMissingService(&service_provider, "parent_service2");
}

class DirentChecker {
 public:
  DirentChecker(const void* buffer, size_t length)
      : current_(reinterpret_cast<const uint8_t*>(buffer)),
        remaining_(length) {}

  void ExpectEnd() {
    EXPECT_EQ(0u, remaining_);
  }

  void ExpectEntry(const char* name, uint32_t vtype) {
    ASSERT_NE(0u, remaining_);
    const auto entry = reinterpret_cast<const vdirent_t*>(current_);
    const size_t entry_size = entry->size + sizeof(vdirent_t);
    ASSERT_GE(remaining_, entry_size);
    current_ += entry_size;
    remaining_ -= entry_size;
    const std::string entry_name(entry->name, strlen(name));
    EXPECT_STREQ(entry_name.c_str(), name);
    EXPECT_EQ(VTYPE_TO_DTYPE(vtype), entry->type)
        << "Unexpected DTYPE for '" << name << "'.";
  }

 private:
  const uint8_t* current_;
  size_t remaining_;
};

constexpr size_t kBufSz = 4096;

TEST_F(ServiceProviderTest, Readdir_Simple) {
  ServiceProviderDirImpl service_provider;
  AddService(&service_provider, "my", 1);
  AddService(&service_provider, "my", 2);
  AddService(&service_provider, "my", 3);

  fs::vdircookie_t cookie = {};
  uint8_t buffer[kBufSz];
  size_t len;
  {
    EXPECT_EQ(ZX_OK,
              service_provider.Readdir(&cookie, buffer, sizeof(buffer), &len));
    DirentChecker dc(buffer, len);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("my_service1", V_TYPE_FILE);
    dc.ExpectEntry("my_service2", V_TYPE_FILE);
    dc.ExpectEntry("my_service3", V_TYPE_FILE);
    dc.ExpectEnd();
  }
  {
    EXPECT_EQ(ZX_OK,
              service_provider.Readdir(&cookie, buffer, sizeof(buffer), &len));
    EXPECT_EQ(len, 0u);
  }
}

TEST_F(ServiceProviderTest, Readdir_WithParent) {
  ServiceProviderDirImpl service_provider;
  AddService(&service_provider, "my", 1);
  AddService(&service_provider, "my", 2);
  auto parent_service_provider = fbl::AdoptRef(new ServiceProviderDirImpl());
  AddService(parent_service_provider.get(), "parent", 1);
  AddService(parent_service_provider.get(), "parent", 2);
  service_provider.set_parent(parent_service_provider);

  fs::vdircookie_t cookie = {};
  uint8_t buffer[kBufSz];
  size_t len;
  {
    EXPECT_EQ(ZX_OK,
              service_provider.Readdir(&cookie, buffer, sizeof(buffer), &len));
    DirentChecker dc(buffer, len);
    dc.ExpectEntry(".", V_TYPE_DIR);
    dc.ExpectEntry("my_service1", V_TYPE_FILE);
    dc.ExpectEntry("my_service2", V_TYPE_FILE);
    dc.ExpectEntry("parent_service1", V_TYPE_FILE);
    dc.ExpectEntry("parent_service2", V_TYPE_FILE);
    dc.ExpectEnd();
  }
  {
    EXPECT_EQ(ZX_OK,
              service_provider.Readdir(&cookie, buffer, sizeof(buffer), &len));
    EXPECT_EQ(len, 0u);
  }
}

// NOTE: We don't test readdir with backing_dir because we know it is broken.
// This will be fixed by removing backing_dir.

}  // namespace
}  // namespace component
