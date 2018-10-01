// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/util.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc_service.h>

#include "garnet/bin/gzos/ree_agent/gz_ipc_client.h"
#include "garnet/bin/gzos/ree_agent/gz_ipc_server.h"
#include "gtest/gtest.h"

#include <gzos/ipc/test/cpp/fidl.h>

namespace ree_agent {

using namespace gzos::ipc::test;

template <typename T>
class ServiceBase {
 public:
  ServiceBase(T* impl) : binding_(impl) {}

  void Bind(zx::channel request) { binding_.Bind(std::move(request)); }

  void Bind(fidl::InterfaceRequest<T> request) {
    binding_.Bind(std::move(request));
  }

  void Unbind() { binding_.Unbind(); }

  auto& binding() { return binding_; }

 private:
  fidl::Binding<T> binding_;
};

class EchoServiceImpl : public EchoService, public ServiceBase<EchoService> {
 public:
  EchoServiceImpl() : ServiceBase(this) {}

 private:
  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
    callback(value);
  }
};

class GzIpcAgentTest : public ::testing::Test,
                       public TaServices,
                       public EchoServiceFinder {
 public:
  GzIpcAgentTest()
      : loop_(&kAsyncLoopConfigAttachToThread),
        vfs_(async_get_default_dispatcher()),
        root_(fbl::AdoptRef(new fs::PseudoDir)),
        binding_(this) {}

  template <typename Interface>
  void AddService(fs::Service::Connector connector,
                  const std::string name = Interface::Name_) {
    auto child = fbl::AdoptRef(new fs::Service(std::move(connector)));
    root_->AddEntry(name, std::move(child));
  }

  void ConnectToService(zx::channel request,
                        const std::string& service_name) override {
    fdio_service_connect_at(root_handle_.get(), service_name.c_str(),
                            request.release());
  }

  virtual void SetUp() override {
    ASSERT_EQ(zx::event::create(0, &event_), ZX_OK);

    ASSERT_EQ(loop_.StartThread(), ZX_OK);

    zx::channel ch0, ch1;
    ASSERT_EQ(zx::channel::create(0, &ch0, &ch1), ZX_OK);

    // Create server IPC Agent
    size_t max_message_size = PAGE_SIZE;
    server_ =
        fbl::make_unique<GzIpcServer>(std::move(ch0), max_message_size, *this);
    ASSERT_TRUE(server_ != nullptr);
    ASSERT_EQ(server_->Start(), ZX_OK);

    // Create client IPC Agent
    client_ = fbl::make_unique<GzIpcClient>(std::move(ch1), max_message_size);
    ASSERT_TRUE(client_ != nullptr);
    ASSERT_EQ(client_->Start(), ZX_OK);

    ASSERT_EQ(zx::channel::create(0, &root_handle_, &ch0), ZX_OK);
    ASSERT_EQ(vfs_.ServeDirectory(root_, std::move(ch0)), ZX_OK);

    // Register Echo service
    AddService<EchoService>([this](zx::channel channel) {
      echo_service_impl_.Bind(std::move(channel));
      return ZX_OK;
    });

    // Register Echo service finder
    AddService<EchoServiceFinder>([this](zx::channel channel) {
      binding_.Bind(std::move(channel));
      return ZX_OK;
    });
  }

 protected:
  void SignalEvent() { event_.signal(0, ZX_USER_SIGNAL_0); }

  zx_status_t WaitEvent() {
    return event_.wait_one(ZX_USER_SIGNAL_0, zx::deadline_after(zx::msec(1)),
                           nullptr);
  }

  fbl::unique_ptr<GzIpcClient> client_;
  fbl::unique_ptr<GzIpcAgent> server_;

  EchoServiceImpl echo_service_impl_;

 private:
  // |EchoServiceFinder| implementation
  void RequestService(fidl::InterfaceRequest<EchoService> request) override {
    echo_service_impl_.Bind(std::move(request));
  }

  void GetService(GetServiceCallback callback) override {
    callback(echo_service_impl_.binding().NewBinding());
  }

  zx::channel root_handle_;
  async::Loop loop_;
  zx::event event_;

  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_;

  fidl::Binding<EchoServiceFinder> binding_;
};

TEST_F(GzIpcAgentTest, AsyncEchoRequest) {
  EchoServicePtr echo;
  ASSERT_EQ(
      client_->Connect(EchoService::Name_, echo.NewRequest().TakeChannel()),
      ZX_OK);

  async::PostTask(async_get_default_dispatcher(), [this, &echo] {
    std::string test_string = "async_echo_test";
    echo->EchoString(test_string, [this, test_string](std::string response) {
      EXPECT_EQ(test_string, response);
      SignalEvent();
    });
  });

  EXPECT_EQ(WaitEvent(), ZX_OK);
}

TEST_F(GzIpcAgentTest, AsyncBadService) {
  EchoServicePtr echo;

  bool error_handler_triggered = false;
  echo.set_error_handler(
      [&error_handler_triggered] { error_handler_triggered = true; });

  ASSERT_EQ(client_->Connect("none-existence service",
                             echo.NewRequest().TakeChannel()),
            ZX_OK);

  EXPECT_EQ(WaitEvent(), ZX_ERR_TIMED_OUT);
  EXPECT_TRUE(error_handler_triggered);
}

TEST_F(GzIpcAgentTest, SyncEchoRequest) {
  EchoServiceSyncPtr echo;
  ASSERT_EQ(
      client_->Connect(EchoService::Name_, echo.NewRequest().TakeChannel()),
      ZX_OK);

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_EQ(echo->EchoString(test_string, &response), ZX_OK);
  EXPECT_EQ(test_string, response);
}

TEST_F(GzIpcAgentTest, SyncBadService) {
  EchoServiceSyncPtr echo;
  ASSERT_EQ(client_->Connect("none-existence service",
                             echo.NewRequest().TakeChannel()),
            ZX_OK);

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_EQ(echo->EchoString(test_string, &response), ZX_ERR_PEER_CLOSED);
}

TEST_F(GzIpcAgentTest, ClientUnbind) {
  bool error_handler_triggered = false;
  echo_service_impl_.binding().set_error_handler(
      [&error_handler_triggered] { error_handler_triggered = true; });

  EchoServiceSyncPtr echo;
  ASSERT_EQ(
      client_->Connect(EchoService::Name_, echo.NewRequest().TakeChannel()),
      ZX_OK);
  echo.Unbind();

  EXPECT_EQ(WaitEvent(), ZX_ERR_TIMED_OUT);
  EXPECT_TRUE(error_handler_triggered);
}

TEST_F(GzIpcAgentTest, AsyncServerUnbind) {
  EchoServicePtr echo;

  bool error_handler_triggered = false;
  echo.set_error_handler(
      [&error_handler_triggered] { error_handler_triggered = true; });

  ASSERT_EQ(
      client_->Connect(EchoService::Name_, echo.NewRequest().TakeChannel()),
      ZX_OK);
  echo_service_impl_.Unbind();

  EXPECT_EQ(WaitEvent(), ZX_ERR_TIMED_OUT);
  EXPECT_TRUE(error_handler_triggered);
}

TEST_F(GzIpcAgentTest, SyncServerUnbind) {
  EchoServiceSyncPtr echo;
  ASSERT_EQ(
      client_->Connect(EchoService::Name_, echo.NewRequest().TakeChannel()),
      ZX_OK);

  echo_service_impl_.Unbind();

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_EQ(echo->EchoString(test_string, &response), ZX_ERR_PEER_CLOSED);
}

TEST_F(GzIpcAgentTest, SendChannel) {
  EchoServiceFinderSyncPtr finder;
  ASSERT_EQ(client_->Connect(EchoServiceFinder::Name_,
                             finder.NewRequest().TakeChannel()),
            ZX_OK);

  EchoServiceSyncPtr echo;
  ASSERT_EQ(finder->RequestService(echo.NewRequest()), ZX_OK);

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_EQ(echo->EchoString(test_string, &response), ZX_OK);
  EXPECT_EQ(test_string, response);
}

TEST_F(GzIpcAgentTest, ReceiveChannel) {
  EchoServiceFinderSyncPtr finder;
  ASSERT_EQ(client_->Connect(EchoServiceFinder::Name_,
                             finder.NewRequest().TakeChannel()),
            ZX_OK);

  fidl::InterfaceHandle<EchoService> handle;
  ASSERT_EQ(finder->GetService(&handle), ZX_OK);

  EchoServiceSyncPtr echo;
  echo.Bind(std::move(handle));

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_EQ(echo->EchoString(test_string, &response), ZX_OK);
  EXPECT_EQ(test_string, response);
}

class ShmVmoTest : public ::testing::Test {
 public:
  ShmVmoTest() = default;

 protected:
  virtual void SetUp() override {
    zx_info_ns_shm_t shm_info;
    ASSERT_EQ(zx::resource::create_ns_mem(0, &shm_info, &shm_rsc_), ZX_OK);

    ASSERT_EQ(zx::vmo::create_ns_mem(shm_rsc_, shm_info.base_phys,
                                     shm_info.size, &vmo_, &event_),
              ZX_OK);

    wait_.set_trigger(ZX_EVENTPAIR_SIGNALED);
    wait_.set_object(event_.get());
  }

  virtual void TearDown() override {
    zx_handle_close(smc_handle_);
    wait_.Cancel();
  }

  async::Wait wait_;
  zx::vmo vmo_;
  zx::eventpair event_;

 private:
  zx_handle_t smc_handle_;
  zx::resource shm_rsc_;
};

TEST_F(ShmVmoTest, VmoClose) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  bool handler_triggered = false;
  wait_.set_handler(
      [this, &handler_triggered](
          async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
          const zx_packet_signal_t* signal) { handler_triggered = true; });

  ASSERT_EQ(wait_.Begin(loop.dispatcher()), ZX_OK);

  // Release vmo, wait handler should be triggered
  vmo_.reset();

  loop.RunUntilIdle();
  EXPECT_TRUE(handler_triggered);
}

TEST_F(ShmVmoTest, VmarUnmap) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  bool handler_triggered = false;
  wait_.set_handler(
      [this, &handler_triggered](
          async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
          const zx_packet_signal_t* signal) { handler_triggered = true; });

  ASSERT_EQ(wait_.Begin(loop.dispatcher()), ZX_OK);

  uintptr_t vaddr;
  ASSERT_EQ(zx::vmar::root_self()->map(0, vmo_, 0, PAGE_SIZE,
                                       ZX_VM_FLAG_PERM_READ, &vaddr),
            ZX_OK);

  // Release vmo, wait handler should not be triggered (VmMapping still holds
  // VmObject reference)
  vmo_.reset();

  loop.RunUntilIdle();
  EXPECT_FALSE(handler_triggered);

  // Unmap it, now wait handler should be triggered
  zx::vmar::root_self()->unmap(vaddr, PAGE_SIZE);

  loop.RunUntilIdle();
  EXPECT_TRUE(handler_triggered);
}

}  // namespace ree_agent
