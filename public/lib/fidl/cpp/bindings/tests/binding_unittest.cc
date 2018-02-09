// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note: This file tests both binding.h (fidl::Binding) and strong_binding.h
// (fidl::StrongBinding).

#include "gtest/gtest.h"
#include "lib/fidl/compiler/interfaces/tests/sample_interfaces.fidl.h"
#include "lib/fidl/compiler/interfaces/tests/sample_service.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"

namespace fidl {
namespace test {
namespace {

class BindingTestBase : public testing::Test {
 public:
  BindingTestBase() {}
  ~BindingTestBase() override {}

  BindingTestBase(const BindingTestBase&) = delete;
  BindingTestBase& operator=(const BindingTestBase&) = delete;

  void TearDown() override { ClearAsyncWaiter(); }

  void PumpMessages() { WaitForAsyncWaiter(); }
};

class ServiceImpl : public sample::Service {
 public:
  explicit ServiceImpl(bool* was_deleted = nullptr)
      : was_deleted_(was_deleted) {}
  ~ServiceImpl() override {
    if (was_deleted_)
      *was_deleted_ = true;
  }

  ServiceImpl(const ServiceImpl&) = delete;
  ServiceImpl& operator=(const ServiceImpl&) = delete;

 private:
  // sample::Service implementation
  void Frobinate(sample::FooPtr foo,
                 BazOptions options,
                 fidl::InterfaceHandle<sample::Port> port,
                 const FrobinateCallback& callback) override {
    callback(1);
  }
  void GetPort(InterfaceRequest<sample::Port> port) override {}

  bool* const was_deleted_;
};

// BindingTest -----------------------------------------------------------------

using BindingTest = BindingTestBase;

TEST_F(BindingTest, Close) {
  bool called = false;
  sample::ServicePtr ptr;
  auto request = ptr.NewRequest();
  ptr.set_error_handler([&called]() { called = true; });
  ServiceImpl impl;
  Binding<sample::Service> binding(&impl, std::move(request));

  binding.Unbind();
  EXPECT_FALSE(called);
  PumpMessages();
  EXPECT_TRUE(called);
}

// Tests that destroying a fidl::Binding closes the bound channel handle.
TEST_F(BindingTest, DestroyClosesMessagePipe) {
  bool encountered_error = false;
  ServiceImpl impl;
  sample::ServicePtr ptr;
  auto request = ptr.NewRequest();
  ptr.set_error_handler(
      [&encountered_error]() { encountered_error = true; });
  bool called = false;
  auto called_cb = [&called](int32_t result) { called = true; };
  {
    Binding<sample::Service> binding(&impl, std::move(request));
    ptr->Frobinate(nullptr, sample::Service::BazOptions::REGULAR, nullptr,
                   called_cb);
    PumpMessages();
    EXPECT_TRUE(called);
    EXPECT_FALSE(encountered_error);
  }
  // Now that the Binding is out of scope we should detect an error on the other
  // end of the pipe.
  PumpMessages();
  EXPECT_TRUE(encountered_error);

  // And calls should fail.
  called = false;
  ptr->Frobinate(nullptr, sample::Service::BazOptions::REGULAR, nullptr,
                 called_cb);
  PumpMessages();
  EXPECT_FALSE(called);
}

// Tests that the binding's connection error handler gets called when the other
// end is closed.
TEST_F(BindingTest, ConnectionError) {
  bool called = false;
  {
    ServiceImpl impl;
    sample::ServicePtr ptr;
    Binding<sample::Service> binding(&impl, ptr.NewRequest());
    binding.set_error_handler([&called]() { called = true; });
    ptr.Unbind();
    EXPECT_FALSE(called);
    PumpMessages();
    EXPECT_TRUE(called);
    // We want to make sure that it isn't called again during destruction.
    called = false;
  }
  EXPECT_FALSE(called);
}

// Tests that calling Close doesn't result in the connection error handler being
// called.
TEST_F(BindingTest, CloseDoesntCallConnectionErrorHandler) {
  ServiceImpl impl;
  sample::ServicePtr ptr;
  Binding<sample::Service> binding(&impl, ptr.NewRequest());
  bool called = false;
  binding.set_error_handler([&called]() { called = true; });
  binding.Unbind();
  PumpMessages();
  EXPECT_FALSE(called);

  // We can also close the other end, and the error handler still won't be
  // called.
  ptr.Unbind();
  PumpMessages();
  EXPECT_FALSE(called);
}

class ServiceImplWithBinding : public ServiceImpl {
 public:
  ServiceImplWithBinding(bool* was_deleted,
                         InterfaceRequest<sample::Service> request)
      : ServiceImpl(was_deleted), binding_(this, std::move(request)) {
    binding_.set_error_handler([this]() { delete this; });
  }

  ServiceImplWithBinding(const ServiceImplWithBinding&) = delete;
  ServiceImplWithBinding& operator=(const ServiceImplWithBinding&) = delete;

 private:
  Binding<sample::Service> binding_;
};

// Tests that the binding may be deleted in the connection error handler.
TEST_F(BindingTest, SelfDeleteOnConnectionError) {
  bool was_deleted = false;
  sample::ServicePtr ptr;
  // This should delete itself on connection error.
  new ServiceImplWithBinding(&was_deleted, ptr.NewRequest());
  ptr.Unbind();
  EXPECT_FALSE(was_deleted);
  PumpMessages();
  EXPECT_TRUE(was_deleted);
}

// Tests that explicitly calling Unbind followed by rebinding works.
TEST_F(BindingTest, Unbind) {
  ServiceImpl impl;
  sample::ServicePtr ptr;
  Binding<sample::Service> binding(&impl, ptr.NewRequest());

  bool called = false;
  auto called_cb = [&called](int32_t result) { called = true; };
  ptr->Frobinate(nullptr, sample::Service::BazOptions::REGULAR, nullptr,
                 called_cb);
  PumpMessages();
  EXPECT_TRUE(called);

  called = false;
  auto request = binding.Unbind();
  EXPECT_FALSE(binding.is_bound());
  // All calls should fail when not bound...
  ptr->Frobinate(nullptr, sample::Service::BazOptions::REGULAR, nullptr,
                 called_cb);
  PumpMessages();
  EXPECT_FALSE(called);

  called = false;
  binding.Bind(std::move(request));
  EXPECT_TRUE(binding.is_bound());
  // ...and should succeed again when the rebound.
  ptr->Frobinate(nullptr, sample::Service::BazOptions::REGULAR, nullptr,
                 called_cb);
  PumpMessages();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace test
}  // namespace fidl
