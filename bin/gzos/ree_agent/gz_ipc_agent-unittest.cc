// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <gzos-utils/shm_resource.h>
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
#include "lib/fsl/handles/object_info.h"

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

class VmoReceiverImpl : public VmoReceiver, public ServiceBase<VmoReceiver> {
 public:
  VmoReceiverImpl() : ServiceBase(this) {}

  zx::vmo& vmo() { return vmo_; }

 private:
  void SendVmo(zx::vmo vmo) override { vmo_ = std::move(vmo); }

  zx::vmo vmo_;
};

class VmoMapper {
 public:
  VmoMapper() = delete;

  VmoMapper(zx::vmo& vmo) : vmo_(vmo) {
    FXL_CHECK(vmo_.get_size(&vmo_size_) == ZX_OK);
  }

  ~VmoMapper() { Unmap(); }

  void* Map() {
    if (vmo_) {
      // If an error occurs, then |mapping_| will remain 0.
      zx::vmar::root_self()->map(0, vmo_, 0u, vmo_size_, kMapFlags, &mapping_);
    }
    return reinterpret_cast<void*>(mapping_);
  }

  void Unmap() {
    if (mapping_) {
      FXL_CHECK(zx::vmar::root_self()->unmap(mapping_, vmo_size_) == ZX_OK);
      mapping_ = 0u;
    }
  }

 private:
  static constexpr uint32_t kMapFlags =
      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE;

  zx::vmo& vmo_;
  size_t vmo_size_;

  uintptr_t mapping_ = 0u;
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

    ASSERT_EQ(gzos_utils::get_shm_resource(&shm_rsc_), ZX_OK);

    // Create server IPC Agent
    size_t max_message_size = PAGE_SIZE;
    server_ =
        fbl::make_unique<GzIpcServer>(zx::unowned_resource(shm_rsc_),
                                      std::move(ch0), max_message_size, *this);
    ASSERT_TRUE(server_ != nullptr);
    ASSERT_EQ(server_->Start(), ZX_OK);

    // Create client IPC Agent
    client_ = fbl::make_unique<GzIpcClient>(zx::unowned_resource(shm_rsc_),
                                            std::move(ch1), max_message_size);
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

    // Register Vmo receiver
    AddService<VmoReceiver>([this](zx::channel channel) {
      vmo_receiver_impl_.Bind(std::move(channel));
      return ZX_OK;
    });
  }

 protected:
  fbl::unique_ptr<GzIpcClient> client_;
  fbl::unique_ptr<GzIpcServer> server_;

  EchoServiceImpl echo_service_impl_;
  VmoReceiverImpl vmo_receiver_impl_;
  async::Loop loop_;

 private:
  // |EchoServiceFinder| implementation
  void RequestService(fidl::InterfaceRequest<EchoService> request) override {
    echo_service_impl_.Bind(std::move(request));
  }

  void GetService(GetServiceCallback callback) override {
    callback(echo_service_impl_.binding().NewBinding());
  }

  zx::channel root_handle_;
  zx::event event_;
  zx::resource shm_rsc_;

  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_;

  fidl::Binding<EchoServiceFinder> binding_;
};

TEST_F(GzIpcAgentTest, AsyncEchoRequest) {
  EchoServicePtr echo;
  ASSERT_EQ(client_->Connect<EchoService>(echo.NewRequest()), ZX_OK);

  std::string test_string = "async_echo_test";
  std::string response_string;

  async::PostTask(
      async_get_default_dispatcher(), [&echo, &test_string, &response_string] {
        echo->EchoString(test_string, [&response_string](std::string response) {
          response_string = response;
        });
      });

  loop_.RunUntilIdle();
  EXPECT_EQ(test_string, response_string);
}

TEST_F(GzIpcAgentTest, AsyncBadService) {
  EchoServicePtr echo;

  bool error_handler_triggered = false;
  echo.set_error_handler(
      [&error_handler_triggered] { error_handler_triggered = true; });

  ASSERT_EQ(client_->Connect("non-existence service",
                             echo.NewRequest().TakeChannel()),
            ZX_OK);

  loop_.RunUntilIdle();
  EXPECT_TRUE(error_handler_triggered);
}

TEST_F(GzIpcAgentTest, SyncEchoRequest) {
  EchoServiceSyncPtr echo;
  ASSERT_EQ(client_->Connect<EchoService>(echo.NewRequest()), ZX_OK);

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
  ASSERT_EQ(client_->Connect<EchoService>(echo.NewRequest()), ZX_OK);
  echo.Unbind();

  loop_.RunUntilIdle();
  EXPECT_TRUE(error_handler_triggered);
}

TEST_F(GzIpcAgentTest, AsyncServerUnbind) {
  EchoServicePtr echo;

  bool error_handler_triggered = false;
  echo.set_error_handler(
      [&error_handler_triggered] { error_handler_triggered = true; });

  ASSERT_EQ(client_->Connect<EchoService>(echo.NewRequest()), ZX_OK);
  echo_service_impl_.Unbind();

  loop_.RunUntilIdle();
  EXPECT_TRUE(error_handler_triggered);
}

TEST_F(GzIpcAgentTest, SyncServerUnbind) {
  EchoServiceSyncPtr echo;
  ASSERT_EQ(client_->Connect<EchoService>(echo.NewRequest()), ZX_OK);

  echo_service_impl_.Unbind();

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_EQ(echo->EchoString(test_string, &response), ZX_ERR_PEER_CLOSED);
}

TEST_F(GzIpcAgentTest, SendChannel) {
  EchoServiceFinderSyncPtr finder;
  ASSERT_EQ(client_->Connect<EchoServiceFinder>(finder.NewRequest()), ZX_OK);

  EchoServiceSyncPtr echo;
  ASSERT_EQ(finder->RequestService(echo.NewRequest()), ZX_OK);

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_EQ(echo->EchoString(test_string, &response), ZX_OK);
  EXPECT_EQ(test_string, response);
}

TEST_F(GzIpcAgentTest, ReceiveChannel) {
  EchoServiceFinderSyncPtr finder;
  ASSERT_EQ(client_->Connect<EchoServiceFinder>(finder.NewRequest()), ZX_OK);

  fidl::InterfaceHandle<EchoService> handle;
  ASSERT_EQ(finder->GetService(&handle), ZX_OK);

  EchoServiceSyncPtr echo;
  echo.Bind(std::move(handle));

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_EQ(echo->EchoString(test_string, &response), ZX_OK);
  EXPECT_EQ(test_string, response);
}

TEST_F(GzIpcAgentTest, SendBadVmo) {
  VmoReceiverPtr vmo_receiver;

  bool error_handler_triggered = false;
  vmo_receiver.set_error_handler(
      [&error_handler_triggered] { error_handler_triggered = true; });

  ASSERT_EQ(client_->Connect<VmoReceiver>(vmo_receiver.NewRequest()), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(PAGE_SIZE, 0, &vmo), ZX_OK);

  vmo_receiver->SendVmo(std::move(vmo));

  loop_.RunUntilIdle();
  EXPECT_TRUE(error_handler_triggered);
}

TEST_F(GzIpcAgentTest, SendVmo) {
  VmoReceiverPtr vmo_receiver;
  ASSERT_EQ(client_->Connect<VmoReceiver>(vmo_receiver.NewRequest()), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(client_->AllocSharedMemory(PAGE_SIZE, &vmo), ZX_OK);

  VmoMapper local_mapper(vmo);
  void* local_buf = local_mapper.Map();
  ASSERT_TRUE(local_buf != nullptr);

  vmo_receiver->SendVmo(std::move(vmo));

  loop_.RunUntilIdle();

  VmoMapper remote_mapper(vmo_receiver_impl_.vmo());
  void* remote_buf = remote_mapper.Map();
  ASSERT_TRUE(remote_buf != nullptr);

  EXPECT_TRUE(std::memcmp(local_buf, remote_buf, PAGE_SIZE) == 0);
}

TEST_F(GzIpcAgentTest, CheckSharedMemoryRecord) {
  VmoReceiverPtr vmo_receiver;
  ASSERT_EQ(client_->Connect<VmoReceiver>(vmo_receiver.NewRequest()), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(client_->AllocSharedMemory(PAGE_SIZE, &vmo), ZX_OK);

  auto id = SharedMemoryRecord::GetShmId(vmo.get());

  vmo_receiver->SendVmo(std::move(vmo));
  loop_.RunUntilIdle();

  auto rec = client_->LookupSharedMemoryRecord(id);
  ASSERT_TRUE(rec != nullptr);
  EXPECT_EQ(id, SharedMemoryRecord::GetShmId(rec->vmo().get()));

  id = SharedMemoryRecord::GetShmId(vmo_receiver_impl_.vmo().get());
  rec = server_->LookupSharedMemoryRecord(id);
  ASSERT_TRUE(rec != nullptr);
  EXPECT_EQ(rec->vmo(), zx::vmo());
}

TEST_F(GzIpcAgentTest, ClientAutoFreeVmoAfterUnmap) {
  VmoReceiverPtr vmo_receiver;
  ASSERT_EQ(client_->Connect<VmoReceiver>(vmo_receiver.NewRequest()), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(client_->AllocSharedMemory(PAGE_SIZE, &vmo), ZX_OK);

  auto id = SharedMemoryRecord::GetShmId(vmo.get());

  // Create a mapping, this increases a reference to Shared memory
  VmoMapper mapper(vmo);
  void* p = mapper.Map();
  ASSERT_TRUE(p != nullptr);

  // Transfer vmo to remote side
  vmo_receiver->SendVmo(std::move(vmo));
  loop_.RunUntilIdle();

  // Then, release vmo at remote side
  vmo_receiver_impl_.vmo().reset();
  loop_.RunUntilIdle();

  // Shared memory should not be released
  EXPECT_TRUE(client_->LookupSharedMemoryRecord(id) != nullptr);

  // Unmap at local side
  mapper.Unmap();
  loop_.RunUntilIdle();

  // Now, shared memory should be released
  EXPECT_TRUE(client_->LookupSharedMemoryRecord(id) == nullptr);
}

TEST_F(GzIpcAgentTest, ClientAutoFreeVmo) {
  VmoReceiverPtr vmo_receiver;
  ASSERT_EQ(client_->Connect<VmoReceiver>(vmo_receiver.NewRequest()), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(client_->AllocSharedMemory(PAGE_SIZE, &vmo), ZX_OK);

  uint64_t id = SharedMemoryRecord::GetShmId(vmo.get());

  vmo_receiver->SendVmo(std::move(vmo));
  loop_.RunUntilIdle();

  EXPECT_TRUE(client_->LookupSharedMemoryRecord(id) != nullptr);

  vmo_receiver_impl_.vmo().reset();
  loop_.RunUntilIdle();

  EXPECT_TRUE(client_->LookupSharedMemoryRecord(id) == nullptr);
}

class ShmVmoTest : public ::testing::Test {
 public:
  ShmVmoTest() = default;

 protected:
  virtual void SetUp() override {
    ASSERT_EQ(gzos_utils::get_shm_resource(&shm_rsc_), ZX_OK);

    zx_info_resource_t shm_info;
    ASSERT_EQ(shm_rsc_.get_info(ZX_INFO_RESOURCE, &shm_info, sizeof(shm_info),
                                nullptr, nullptr),
              ZX_OK);

    ASSERT_EQ(zx::vmo::create_ns_mem(shm_rsc_, shm_info.base, shm_info.size,
                                     &vmo_, &event_),
              ZX_OK);

    wait_.set_trigger(ZX_EVENTPAIR_PEER_CLOSED);
    wait_.set_object(event_.get());
  }

  virtual void TearDown() override {
    wait_.Cancel();
  }

  async::Wait wait_;
  zx::vmo vmo_;
  zx::eventpair event_;

 private:
  zx::resource shm_rsc_;
};

TEST_F(ShmVmoTest, VmoClose) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  bool handler_triggered = false;
  wait_.set_handler([&handler_triggered](async_dispatcher_t* dispatcher,
                                         async::Wait* wait, zx_status_t status,
                                         const zx_packet_signal_t* signal) {
    handler_triggered = true;
  });

  ASSERT_EQ(wait_.Begin(loop.dispatcher()), ZX_OK);

  // Release vmo, wait handler should be triggered
  vmo_.reset();

  loop.RunUntilIdle();
  EXPECT_TRUE(handler_triggered);
}

TEST_F(ShmVmoTest, VmarUnmap) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  bool handler_triggered = false;
  wait_.set_handler([&handler_triggered](async_dispatcher_t* dispatcher,
                                         async::Wait* wait, zx_status_t status,
                                         const zx_packet_signal_t* signal) {
    handler_triggered = true;
  });

  ASSERT_EQ(wait_.Begin(loop.dispatcher()), ZX_OK);

  VmoMapper mapper(vmo_);
  void* p = mapper.Map();
  ASSERT_TRUE(p != nullptr);

  // Release vmo, wait handler should not be triggered (VmMapping still holds
  // VmObject reference)
  vmo_.reset();

  loop.RunUntilIdle();
  EXPECT_FALSE(handler_triggered);

  // Unmap it, now wait handler should be triggered
  mapper.Unmap();

  loop.RunUntilIdle();
  EXPECT_TRUE(handler_triggered);
}

}  // namespace ree_agent
