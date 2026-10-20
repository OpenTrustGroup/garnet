// SPDX-License-Identifier: BSD-3-Clause

#include "gtest/gtest.h"

#include "garnet/bin/guest_manager/guest_manager.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/logging.h"

#include "garnet/bin/guest_manager/tests/lib/guest_console.h"
#include "garnet/lib/machina/vm_id.h"

#include <fuchsia/cpp/machina.h>
#include <fuchsia/cpp/virtualization.h>

#include <fcntl.h>
#include <openssl/evp.h>
#include <stdlib.h>
#include <zircon/vmid.h>

#define SHA1_DIGEST_LENGTH 20
#define ENABLE_GZFS_DEBUG 0  // for QEMU platform only
#define VERBOSE_LOGGING 0

constexpr int kSosVmid = ZX_USER_VMID_SOS;
constexpr int kTboxVmid = ZX_USER_VMID_TBOX;
constexpr int8_t kTestVmid = 1;
constexpr int8_t kMissingVmid = 42;
constexpr zx::duration kDefaultTimeout = zx::sec(5);

class GuestManagerTestPeer {
 public:
  static std::unique_ptr<GuestManagerImpl> Build(
      component::ApplicationContext* app_context,
      async_t* async,
      guest_manager::ManagerConfig cfg) {
    return GuestManagerImpl::Build(app_context, async, std::move(cfg),
                                    machina::WatchdogPolicy(true));
  }

  static Guest* GetGuest(GuestManagerImpl* manager, int8_t vmid) {
    auto it = manager->guests_.find(vmid);
    return it == manager->guests_.end() ? nullptr : it->second.get();
  }
};

namespace {

guest_manager::ManagerConfig MakeManagerConfig() {
  guest_manager::ManagerConfig manager_cfg;
  auto* guest_cfg = manager_cfg.add_guest_configs();
  guest_cfg->set_vmid(kTestVmid);
  guest_cfg->set_name("test");
  guest_cfg->set_auto_start(false);
  return manager_cfg;
}

class TestGuestLifecycle : public virtualization::GuestLifecycle {
 public:
  explicit TestGuestLifecycle(async_t* async) : async_(async), binding_(this) {}

  zx_status_t Bind(zx::channel channel) {
    return binding_.Bind(
        fidl::InterfaceRequest<virtualization::GuestLifecycle>(
            std::move(channel)),
        async_);
  }

  void Create(virtualization::Config, CreateCallback callback) override {
    callback(virtualization::GuestError::OK);
  }

  void Bind(fidl::InterfaceRequest<virtualization::GuestController>,
            BindCallback callback) override {
    callback(virtualization::GuestError::OK);
  }

  void Run(RunCallback) override {}

  void Stop(StopCallback callback) override {
    (void)callback;
    ++stop_count_;
  }

  size_t stop_count() const { return stop_count_; }

 private:
  async_t* const async_;
  fidl::Binding<virtualization::GuestLifecycle> binding_;
  size_t stop_count_ = 0;
};

void BindLifecycle(Guest* guest,
                   TestGuestLifecycle* lifecycle,
                   async_t* async) {
  zx::channel client;
  zx::channel server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &client, &server));
  ASSERT_EQ(ZX_OK, guest->lifecycle_.Bind(std::move(client), async));
  ASSERT_EQ(ZX_OK, lifecycle->Bind(std::move(server)));
}

}  // namespace

class GuestManagerTest : public ::testing::Test,
                         public ::testing::WithParamInterface<size_t> {
 protected:
  GuestManagerTest() : loop_(&kAsyncLoopConfigMakeDefault) {}
  ~GuestManagerTest() override = default;

  void Sha1String(const char* data, size_t len, char* md_hex_string) {
    uint8_t md[EVP_MAX_MD_SIZE];
    unsigned int md_len;

    ASSERT_NE(0, EVP_Digest(data, len, md, &md_len, EVP_sha1(), NULL));
    for (unsigned int i = 0; i < md_len; i++) {
      sprintf(&md_hex_string[i * 2], "%02x", md[i]);
    }
  }

  void GzfsVsockTest(size_t test_size, std::string console_prompt);

  virtualization::GuestManagerSyncPtr guest_manager_;
  virtualization::GuestControllerSyncPtr guest_controller_;

  async::Loop loop_;
  std::unique_ptr<GuestConsole> serial_;
};

TEST(GuestManagerForceShutdownTest,
     PublicCallbackWaitsForGuestLifecycleClose) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  component::ApplicationContext app_context{zx::channel(), zx::channel()};
  auto manager = GuestManagerTestPeer::Build(&app_context, loop.async(),
                                             MakeManagerConfig());
  Guest* guest = GuestManagerTestPeer::GetGuest(manager.get(), kTestVmid);
  ASSERT_NE(nullptr, guest);

  TestGuestLifecycle lifecycle(loop.async());
  ASSERT_NO_FATAL_FAILURE(BindLifecycle(guest, &lifecycle, loop.async()));
  guest->state_ = virtualization::GuestStatus::RUNNING;

  fidl::Binding<virtualization::GuestManager> binding(manager.get());
  virtualization::GuestManagerPtr guest_manager;
  ASSERT_EQ(ZX_OK,
            binding.Bind(guest_manager.NewRequest(loop.async()), loop.async()));

  bool callback_called = false;
  guest_manager->ForceShutdown(
      kTestVmid, [&callback_called] { callback_called = true; });
  ASSERT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(virtualization::GuestStatus::STOPPING, guest->state());
  EXPECT_EQ(1u, lifecycle.stop_count());
  EXPECT_FALSE(callback_called);

  guest->HandleLifecycleClosed();
  ASSERT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(virtualization::GuestStatus::STOPPED, guest->state());
  EXPECT_TRUE(callback_called);
}

TEST(GuestManagerForceShutdownTest, MissingVmidRepliesImmediately) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  component::ApplicationContext app_context{zx::channel(), zx::channel()};
  auto manager = GuestManagerTestPeer::Build(&app_context, loop.async(),
                                             MakeManagerConfig());

  fidl::Binding<virtualization::GuestManager> binding(manager.get());
  virtualization::GuestManagerPtr guest_manager;
  ASSERT_EQ(ZX_OK,
            binding.Bind(guest_manager.NewRequest(loop.async()), loop.async()));

  bool callback_called = false;
  guest_manager->ForceShutdown(
      kMissingVmid, [&callback_called] { callback_called = true; });
  ASSERT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_TRUE(callback_called);
}

void GuestManagerTest::GzfsVsockTest(size_t test_size,
                                     std::string console_prompt) {
  int fd = open("/data/testfile", O_CREAT | O_TRUNC | O_RDWR);
  ASSERT_GT(fd, 0);

  FXL_LOG(INFO) << "Generating test data, size: " << test_size;
  std::vector<char> test_data(test_size);
  for (size_t i = 0; i < test_size / sizeof(int); i++) {
    int rand_data = rand();

    test_data[i * sizeof(int) + 0] = (rand_data >> 0) & 0xFF;
    test_data[i * sizeof(int) + 1] = (rand_data >> 8) & 0xFF;
    test_data[i * sizeof(int) + 2] = (rand_data >> 16) & 0xFF;
    test_data[i * sizeof(int) + 3] = (rand_data >> 24) & 0xFF;
  }

  FXL_LOG(INFO) << "Writing test data to /data/testfile";
  size_t total_written = 0;
  do {
    ssize_t write_size = write(fd, test_data.data() + total_written, test_size);
    ASSERT_GT(write_size, 0);
    total_written += write_size;
  } while (total_written < test_size);
  close(fd);

  char sha1[SHA1_DIGEST_LENGTH * 2] = {0};
  Sha1String(test_data.data(), test_data.size(), sha1);

  FXL_LOG(INFO) << "Computed SHA1 of test data: " << sha1;

  std::vector<std::string> pre_cmds = {
    "mkdir -p /data/gzfs_test",
#if ENABLE_GZFS_DEBUG
    "echo 8 > /proc/sys/kernel/printk",
    "/sbin/syslogd -K",
    "gzfs -d /data/gzfs_test",
#else
    "gzfs /data/gzfs_test",
#endif
  };

  std::vector<std::string> post_cmds = {
    "umount /data/gzfs_test",
#if ENABLE_GZFS_DEBUG
    "killall syslogd",
#endif
  };

  for (const auto& cmd : pre_cmds) {
    EXPECT_EQ(ZX_OK, serial_->ExecuteBlocking(
                         cmd, console_prompt,
                         zx::deadline_after(kDefaultTimeout), nullptr));
  }

  std::string test_cmd = "sha1sum /data/gzfs_test/testfile";
  EXPECT_EQ(ZX_OK,
            serial_->SendBlocking(test_cmd + "\n", zx::time::infinite()));

  std::string sha1_string(sha1, sizeof(sha1));
  std::vector<std::string> expected_markers = {
      test_cmd,
      sha1_string + "  /data/gzfs_test/testfile",
      console_prompt,
  };

  for (const auto& marker : expected_markers) {
    EXPECT_EQ(ZX_OK, serial_->WaitForMarker(
                         marker, zx::deadline_after(kDefaultTimeout), nullptr));
  }

  for (const auto& cmd : post_cmds) {
    EXPECT_EQ(ZX_OK, serial_->ExecuteBlocking(
                         cmd, console_prompt,
                         zx::deadline_after(kDefaultTimeout), nullptr));
  }
}

class TboxVmTest : public GuestManagerTest {
 protected:
  void SetUp() override {
    component::ConnectToEnvironmentService(guest_manager_.NewRequest());

    virtualization::GuestError error;
    guest_manager_->Launch(kTboxVmid, &error);
    EXPECT_EQ(error, virtualization::GuestError::OK);

    guest_manager_->Connect(kTboxVmid, guest_controller_.NewRequest(), &error);
    EXPECT_EQ(error, virtualization::GuestError::OK);

    zx::socket console_channel;
    guest_controller_->GetConsole(&console_channel);
    ASSERT_TRUE(console_channel.is_valid());

    auto console_socket =
        std::make_unique<ZxSocket>(std::move(console_channel));

    serial_ = std::make_unique<GuestConsole>(std::move(console_socket));
    ASSERT_EQ(ZX_OK, serial_->Start(zx::time::infinite()));

    // Make sure the pty is running and that the guest will receive our
    // commands.
    ASSERT_EQ(ZX_OK, serial_->WaitForMarker(
                         "Please press Enter to activate this console.",
                         zx::time::infinite(), nullptr));

    ASSERT_EQ(ZX_OK, serial_->SendBlocking("\n", zx::time::infinite()));
  }

  void TearDown() override { guest_manager_->ForceShutdown(kTboxVmid); }
};

TEST_F(TboxVmTest, PoweroffTest) {
  EXPECT_EQ(ZX_OK, serial_->ExecuteBlocking("poweroff", "root@tbox",
                                            zx::time::infinite(), nullptr));

  EXPECT_EQ(ZX_OK, serial_->WaitForSocketClosed(zx::time::infinite()));
}

TEST_F(TboxVmTest, PingTest) {
  EXPECT_EQ(ZX_OK, serial_->SendBlocking("ping 192.168.0.1 -c 1\n",
                                         zx::time::infinite()));

  EXPECT_EQ(ZX_OK, serial_->WaitForMarker("64 bytes from 192.168.0.1",
                                          zx::time::infinite(), nullptr));
}

INSTANTIATE_TEST_SUITE_P(
    TestDataSize,       // Instantiation name (can be anything, no underscores
                        // recommended)
    TboxVmTest,         // Test fixture class name
    ::testing::Values(  // Generator function to provide parameters
        1024,
        1024 * 64,
        1024 * 1024));

TEST_P(TboxVmTest, GzfsVsockTestQEMU) {
  GzfsVsockTest(GetParam(), "root@tbox:");
}

TEST_P(TboxVmTest, DISABLED_GzfsVsockTestMT8668) {
  // TODO: implementation
}

class SosVmTest : public GuestManagerTest {
 protected:
  void SetUp() override {
    component::ConnectToEnvironmentService(guest_manager_.NewRequest());

    virtualization::GuestError error;
    bool launched = false;

    guest_manager_->Launch(kSosVmid, &error);
    if (error == virtualization::GuestError::ALREADY_RUNNING) {
      FXL_LOG(INFO) << "SOS VM already running, skipping launch.";
    } else {
      EXPECT_EQ(error, virtualization::GuestError::OK);
      launched = true;
    }

    guest_manager_->Connect(kSosVmid, guest_controller_.NewRequest(), &error);
    EXPECT_EQ(error, virtualization::GuestError::OK);

    zx::socket console_channel;
    guest_controller_->GetConsole(&console_channel);
    ASSERT_TRUE(console_channel.is_valid());

    auto console_socket =
        std::make_unique<ZxSocket>(std::move(console_channel));

    serial_ = std::make_unique<GuestConsole>(std::move(console_socket));

    if (launched) {
      // Make sure the pty is running and that the guest will receive our
      // commands.
      ASSERT_EQ(ZX_OK, serial_->WaitForMarker(
                           "Please press Enter to activate this console.",
                           zx::time::infinite(), nullptr));
    }

    ASSERT_EQ(ZX_OK, serial_->SendBlocking("\n", zx::time::infinite()));
  }

  void TearDown() override {}
};

INSTANTIATE_TEST_SUITE_P(
    TestDataSize,       // Instantiation name (can be anything, no underscores
                        // recommended)
    SosVmTest,          // Test fixture class name
    ::testing::Values(  // Generator function to provide parameters
        1024,
        1024 * 64,
        1024 * 1024));

TEST_P(SosVmTest, GzfsVsockTestQEMU) {
  GzfsVsockTest(GetParam(), "root@FVP:");
}

TEST_P(SosVmTest, GzfsVsockTestMT8668) {
  GzfsVsockTest(GetParam(), "root@auto8668p164sos:");
}

int main(int argc, char** argv) {
#if VERBOSE_LOGGING
  // Set log level to -1 (Verbose);
  fxl::LogSettings log_settings = fxl::GetLogSettings();
  log_settings.min_log_level = static_cast<fxl::LogSeverity>(-1);
  fxl::SetLogSettings(log_settings);
#endif

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
