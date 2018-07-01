// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_service_manager.h"

#include <vector>

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_loop_fixture.h"

#include "fake_client.h"

namespace btlib {
namespace gatt {

// This must be in the correct namespace for it to be visible to EXPECT_EQ.
static bool operator==(const CharacteristicData& chrc1,
                       const CharacteristicData& chrc2) {
  return chrc1.properties == chrc2.properties && chrc1.handle == chrc2.handle &&
         chrc1.value_handle == chrc2.value_handle && chrc1.type == chrc2.type;
}

// This must be in the correct namespace for it to be visible to EXPECT_EQ.
static bool operator==(const DescriptorData& desc1,
                       const DescriptorData& desc2) {
  return desc1.handle == desc2.handle && desc1.type == desc2.type;
}

namespace internal {
namespace {

using common::ByteBuffer;
using common::CreateStaticByteBuffer;
using common::HostError;

constexpr common::UUID kTestServiceUuid1((uint16_t)0xbeef);
constexpr common::UUID kTestServiceUuid2((uint16_t)0xcafe);
constexpr common::UUID kTestUuid3((uint16_t)0xfefe);
constexpr common::UUID kTestUuid4((uint16_t)0xefef);

const auto kCCCNotifyValue = CreateStaticByteBuffer(0x01, 0x00);
const auto kCCCIndicateValue = CreateStaticByteBuffer(0x02, 0x00);

using common::HostError;

void NopStatusCallback(att::Status) {}
void NopValueCallback(const ByteBuffer&) {}

class GATT_RemoteServiceManagerTest : public ::gtest::TestLoopFixture {
 public:
  GATT_RemoteServiceManagerTest() = default;
  ~GATT_RemoteServiceManagerTest() override = default;

 protected:
  void SetUp() override {
    auto client =
        std::make_unique<testing::FakeClient>(dispatcher());
    fake_client_ = client.get();

    mgr_ = std::make_unique<RemoteServiceManager>(std::move(client),
                                                  dispatcher());
  }

  void TearDown() override {
    // Clear any previous expectations that are based on the ATT Write Request,
    // so that write requests sent during RemoteService::ShutDown() are ignored.
    fake_client()->set_write_request_callback({});
    mgr_ = nullptr;
  }

  // Initializes a RemoteService based on |data|.
  fbl::RefPtr<RemoteService> SetUpFakeService(const ServiceData& data) {
    std::vector<ServiceData> fake_services{{data}};
    fake_client()->set_primary_services(std::move(fake_services));

    mgr()->Initialize(NopStatusCallback);

    ServiceList services;
    mgr()->ListServices(std::vector<common::UUID>(),
                        [&services](auto status, ServiceList cb_services) {
                          services = std::move(cb_services);
                        });

    RunLoopUntilIdle();

    FXL_DCHECK(services.size() == 1u);
    return services[0];
  }

  // Discover the characteristics of |service| based on the given |fake_data|.
  void SetupCharacteristics(
      fbl::RefPtr<RemoteService> service,
      std::vector<CharacteristicData> fake_chrs,
      std::vector<DescriptorData> fake_descrs = std::vector<DescriptorData>()) {
    FXL_DCHECK(service);

    fake_client()->set_characteristics(std::move(fake_chrs));
    fake_client()->set_descriptors(std::move(fake_descrs));
    fake_client()->set_characteristic_discovery_status(att::Status());

    service->DiscoverCharacteristics([](auto, const auto&) {});
    RunLoopUntilIdle();
  }

  // Create a fake service with one notifiable characteristic.
  fbl::RefPtr<RemoteService> SetupNotifiableService() {
    ServiceData data(1, 4, kTestServiceUuid1);
    auto service = SetUpFakeService(data);

    CharacteristicData chr(Property::kNotify, 2, 3, kTestUuid3);
    DescriptorData desc(4, types::kClientCharacteristicConfig);
    SetupCharacteristics(service, {{chr}}, {{desc}});

    fake_client()->set_write_request_callback(
        [&](att::Handle, const auto&, auto status_callback) {
          status_callback(att::Status());
        });

    RunLoopUntilIdle();

    return service;
  }

  void EnableNotifications(
      fbl::RefPtr<RemoteService> service, IdType chr_id,
      att::Status* out_status, IdType* out_id,
      RemoteService::ValueCallback callback = NopValueCallback) {
    FXL_DCHECK(out_status);
    FXL_DCHECK(out_id);
    service->EnableNotifications(chr_id, std::move(callback),
                                 [&](att::Status cb_status, IdType cb_id) {
                                   *out_status = cb_status;
                                   *out_id = cb_id;
                                 });
    RunLoopUntilIdle();
  }

  RemoteServiceManager* mgr() const { return mgr_.get(); }
  testing::FakeClient* fake_client() const { return fake_client_; }

 private:
  std::unique_ptr<RemoteServiceManager> mgr_;

  // The memory is owned by |mgr_|.
  testing::FakeClient* fake_client_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GATT_RemoteServiceManagerTest);
};

TEST_F(GATT_RemoteServiceManagerTest, InitializeNoServices) {
  std::vector<fbl::RefPtr<RemoteService>> services;
  mgr()->set_service_watcher(
      [&services](auto svc) { services.push_back(svc); });

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

  RunLoopUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_TRUE(services.empty());

  mgr()->ListServices(std::vector<common::UUID>(),
                      [&services](auto status, ServiceList cb_services) {
                        services = std::move(cb_services);
                      });
  EXPECT_TRUE(services.empty());
}

TEST_F(GATT_RemoteServiceManagerTest, Initialize) {
  ServiceData svc1(1, 1, kTestServiceUuid1);
  ServiceData svc2(2, 2, kTestServiceUuid2);
  std::vector<ServiceData> fake_services{{svc1, svc2}};
  fake_client()->set_primary_services(std::move(fake_services));

  ServiceList services;
  mgr()->set_service_watcher(
      [&services](auto svc) { services.push_back(svc); });

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

  RunLoopUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_EQ(2u, services.size());
  EXPECT_EQ(svc1.range_start, services[0]->handle());
  EXPECT_EQ(svc2.range_start, services[1]->handle());
  EXPECT_EQ(svc1.type, services[0]->uuid());
  EXPECT_EQ(svc2.type, services[1]->uuid());
}

TEST_F(GATT_RemoteServiceManagerTest, InitializeFailure) {
  fake_client()->set_service_discovery_status(
      att::Status(att::ErrorCode::kRequestNotSupported));

  ServiceList watcher_services;
  mgr()->set_service_watcher(
      [&watcher_services](auto svc) { watcher_services.push_back(svc); });

  ServiceList services;
  mgr()->ListServices(std::vector<common::UUID>(),
                      [&services](auto status, ServiceList cb_services) {
                        services = std::move(cb_services);
                      });
  ASSERT_TRUE(services.empty());

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

  RunLoopUntilIdle();

  EXPECT_FALSE(status);
  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(att::ErrorCode::kRequestNotSupported, status.protocol_error());
  EXPECT_TRUE(services.empty());
  EXPECT_TRUE(watcher_services.empty());
}

TEST_F(GATT_RemoteServiceManagerTest, ListServicesBeforeInit) {
  ServiceData svc(1, 1, kTestServiceUuid1);
  std::vector<ServiceData> fake_services{{svc}};
  fake_client()->set_primary_services(std::move(fake_services));

  ServiceList services;
  mgr()->ListServices(std::vector<common::UUID>(),
                      [&services](auto status, ServiceList cb_services) {
                        services = std::move(cb_services);
                      });
  EXPECT_TRUE(services.empty());

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

  RunLoopUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_EQ(1u, services.size());
  EXPECT_EQ(svc.range_start, services[0]->handle());
  EXPECT_EQ(svc.type, services[0]->uuid());
}

TEST_F(GATT_RemoteServiceManagerTest, ListServicesAfterInit) {
  ServiceData svc(1, 1, kTestServiceUuid1);
  std::vector<ServiceData> fake_services{{svc}};
  fake_client()->set_primary_services(std::move(fake_services));

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

  RunLoopUntilIdle();

  ASSERT_TRUE(status);

  ServiceList services;
  mgr()->ListServices(std::vector<common::UUID>(),
                      [&services](auto status, ServiceList cb_services) {
                        services = std::move(cb_services);
                      });
  EXPECT_EQ(1u, services.size());
  EXPECT_EQ(svc.range_start, services[0]->handle());
  EXPECT_EQ(svc.type, services[0]->uuid());
}

TEST_F(GATT_RemoteServiceManagerTest, ListServicesByUuid) {
  std::vector<common::UUID> uuids{kTestServiceUuid1};

  ServiceData svc1(1, 1, kTestServiceUuid1);
  ServiceData svc2(2, 2, kTestServiceUuid2);
  std::vector<ServiceData> fake_services{{svc1, svc2}};
  fake_client()->set_primary_services(std::move(fake_services));

  att::Status list_services_status;
  ServiceList services;
  mgr()->set_service_watcher(
      [&services](auto svc) { services.push_back(svc); });
  mgr()->ListServices(std::move(uuids),
                      [&](att::Status cb_status, ServiceList cb_services) {
                        list_services_status = cb_status;
                        services = std::move(cb_services);
                      });
  ASSERT_TRUE(services.empty());

  att::Status status(HostError::kFailed);
  mgr()->Initialize([this, &status](att::Status val) { status = val; });

  RunLoopUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_TRUE(list_services_status);
  EXPECT_EQ(1u, services.size());
  EXPECT_EQ(svc1.range_start, services[0]->handle());
  EXPECT_EQ(svc1.type, services[0]->uuid());
}

TEST_F(GATT_RemoteServiceManagerTest, DiscoverCharacteristicsAfterShutDown) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  service->ShutDown();

  att::Status status;
  size_t chrcs_size;
  service->DiscoverCharacteristics(
      [&](att::Status cb_status, const auto& chrcs) {
        status = cb_status;
        chrcs_size = chrcs.size();
      });

  RunLoopUntilIdle();
  EXPECT_FALSE(status);
  EXPECT_EQ(HostError::kFailed, status.error());
  EXPECT_EQ(0u, chrcs_size);
  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  EXPECT_FALSE(service->IsDiscovered());
}

TEST_F(GATT_RemoteServiceManagerTest, DiscoverCharacteristicsSuccess) {
  ServiceData data(1, 5, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData fake_chrc1(0, 2, 3, kTestUuid3);
  CharacteristicData fake_chrc2(0, 4, 5, kTestUuid4);
  std::vector<CharacteristicData> fake_chrcs{{fake_chrc1, fake_chrc2}};
  fake_client()->set_characteristics(std::move(fake_chrcs));

  att::Status status1(HostError::kFailed);
  service->DiscoverCharacteristics(
      [&](att::Status cb_status, const auto& chrcs) {
        status1 = cb_status;
        EXPECT_EQ(2u, chrcs.size());

        EXPECT_EQ(0u, chrcs[0].id());
        EXPECT_EQ(1u, chrcs[1].id());

        EXPECT_EQ(fake_chrc1, chrcs[0].info());
        EXPECT_EQ(fake_chrc2, chrcs[1].info());
      });

  // Queue a second request.
  att::Status status2(HostError::kFailed);
  RemoteCharacteristicList chrcs2;
  service->DiscoverCharacteristics(
      [&](att::Status cb_status, const auto& chrcs) {
        status2 = cb_status;
        EXPECT_EQ(2u, chrcs.size());

        EXPECT_EQ(0u, chrcs[0].id());
        EXPECT_EQ(1u, chrcs[1].id());

        EXPECT_EQ(fake_chrc1, chrcs[0].info());
        EXPECT_EQ(fake_chrc2, chrcs[1].info());
      });

  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  RunLoopUntilIdle();

  // Only one ATT request should have been made.
  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());

  EXPECT_TRUE(service->IsDiscovered());
  EXPECT_TRUE(status1);
  EXPECT_TRUE(status2);
  EXPECT_EQ(data.range_start,
            fake_client()->last_chrc_discovery_start_handle());
  EXPECT_EQ(data.range_end, fake_client()->last_chrc_discovery_end_handle());

  // Request discovery again. This should succeed without an ATT request.
  status1 = att::Status(HostError::kFailed);
  service->DiscoverCharacteristics(
      [&status1](att::Status cb_status, const auto&) { status1 = cb_status; });

  RunLoopUntilIdle();

  EXPECT_TRUE(status1);
  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());
  EXPECT_TRUE(service->IsDiscovered());
}

TEST_F(GATT_RemoteServiceManagerTest, DiscoverCharacteristicsError) {
  ServiceData data(1, 5, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData chrc1(0, 2, 3, kTestUuid3);
  CharacteristicData chrc2(0, 4, 5, kTestUuid4);
  std::vector<CharacteristicData> fake_chrcs{{chrc1, chrc2}};
  fake_client()->set_characteristics(std::move(fake_chrcs));

  fake_client()->set_characteristic_discovery_status(
      att::Status(HostError::kNotSupported));

  att::Status status1;
  RemoteCharacteristicList chrcs1;
  service->DiscoverCharacteristics(
      [&](att::Status cb_status, const auto& chrcs) {
        status1 = cb_status;
        EXPECT_TRUE(chrcs.empty());
      });

  // Queue a second request.
  att::Status status2;
  RemoteCharacteristicList chrcs2;
  service->DiscoverCharacteristics(
      [&](att::Status cb_status, const auto& chrcs) {
        status2 = cb_status;
        EXPECT_TRUE(chrcs.empty());
      });

  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  RunLoopUntilIdle();

  // Onle one request should have been made.
  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());

  EXPECT_FALSE(service->IsDiscovered());
  EXPECT_FALSE(status1);
  EXPECT_FALSE(status2);
  EXPECT_EQ(HostError::kNotSupported, status1.error());
  EXPECT_EQ(HostError::kNotSupported, status2.error());
}

// Discover descriptors of a service with one characteristic.
TEST_F(GATT_RemoteServiceManagerTest, DiscoverDescriptorsOfOneSuccess) {
  constexpr att::Handle kStart = 1;
  constexpr att::Handle kCharDecl = 2;
  constexpr att::Handle kCharValue = 3;
  constexpr att::Handle kDesc1 = 4;
  constexpr att::Handle kDesc2 = 5;
  constexpr att::Handle kEnd = 5;

  ServiceData data(kStart, kEnd, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData fake_chrc(0, kCharDecl, kCharValue, kTestUuid3);
  fake_client()->set_characteristics({{fake_chrc}});

  DescriptorData fake_desc1(kDesc1, kTestUuid3);
  DescriptorData fake_desc2(kDesc2, kTestUuid4);
  fake_client()->set_descriptors({{fake_desc1, fake_desc2}});

  att::Status status(HostError::kFailed);
  service->DiscoverCharacteristics(
      [&](att::Status cb_status, const auto& chrcs) {
        status = cb_status;
        EXPECT_EQ(1u, chrcs.size());

        EXPECT_EQ(2u, chrcs[0].descriptors().size());
        EXPECT_EQ(0u, chrcs[0].descriptors()[0].id());
        EXPECT_EQ(fake_desc1, chrcs[0].descriptors()[0].info());
        EXPECT_EQ(1u, chrcs[0].descriptors()[1].id());
        EXPECT_EQ(fake_desc2, chrcs[0].descriptors()[1].info());
      });

  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  RunLoopUntilIdle();

  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());
  EXPECT_EQ(1u, fake_client()->desc_discovery_count());
  EXPECT_TRUE(service->IsDiscovered());
  EXPECT_TRUE(status);
  EXPECT_EQ(kDesc1, fake_client()->last_desc_discovery_start_handle());
  EXPECT_EQ(kEnd, fake_client()->last_desc_discovery_end_handle());
}

// Discover descriptors of a service with one characteristic.
TEST_F(GATT_RemoteServiceManagerTest, DiscoverDescriptorsOfOneError) {
  constexpr att::Handle kStart = 1;
  constexpr att::Handle kCharDecl = 2;
  constexpr att::Handle kCharValue = 3;
  constexpr att::Handle kDesc1 = 4;
  constexpr att::Handle kDesc2 = 5;
  constexpr att::Handle kEnd = 5;

  ServiceData data(kStart, kEnd, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData fake_chrc(0, kCharDecl, kCharValue, kTestUuid3);
  fake_client()->set_characteristics({{fake_chrc}});

  DescriptorData fake_desc1(kDesc1, kTestUuid3);
  DescriptorData fake_desc2(kDesc2, kTestUuid4);
  fake_client()->set_descriptors({{fake_desc1, fake_desc2}});
  fake_client()->set_descriptor_discovery_status(
      att::Status(HostError::kNotSupported));

  att::Status status;
  service->DiscoverCharacteristics(
      [&](att::Status cb_status, const auto& chrcs) {
        status = cb_status;
        EXPECT_TRUE(chrcs.empty());
      });

  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  RunLoopUntilIdle();

  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());
  EXPECT_EQ(1u, fake_client()->desc_discovery_count());
  EXPECT_FALSE(service->IsDiscovered());
  EXPECT_FALSE(status);
  EXPECT_EQ(HostError::kNotSupported, status.error());
}

// Discover descriptors of a service with multiple characteristics
TEST_F(GATT_RemoteServiceManagerTest, DiscoverDescriptorsOfMultipleSuccess) {
  // Has one descriptor
  CharacteristicData fake_char1(0, 2, 3, kTestUuid3);
  DescriptorData fake_desc1(4, kTestUuid4);

  // Has no descriptors
  CharacteristicData fake_char2(0, 5, 6, kTestUuid3);

  // Has two descriptors
  CharacteristicData fake_char3(0, 7, 8, kTestUuid3);
  DescriptorData fake_desc2(9, kTestUuid4);
  DescriptorData fake_desc3(10, kTestUuid4);

  ServiceData data(1, fake_desc3.handle, kTestServiceUuid1);
  auto service = SetUpFakeService(data);
  fake_client()->set_characteristics({{fake_char1, fake_char2, fake_char3}});
  fake_client()->set_descriptors({{fake_desc1, fake_desc2, fake_desc3}});

  att::Status status(HostError::kFailed);
  service->DiscoverCharacteristics(
      [&](att::Status cb_status, const auto& chrcs) {
        status = cb_status;
        EXPECT_EQ(3u, chrcs.size());

        // Characteristic #1
        EXPECT_EQ(1u, chrcs[0].descriptors().size());
        EXPECT_EQ(0u, chrcs[0].descriptors()[0].id());
        EXPECT_EQ(fake_desc1, chrcs[0].descriptors()[0].info());

        // Characteristic #2
        EXPECT_TRUE(chrcs[1].descriptors().empty());

        // Characteristic #3
        EXPECT_EQ(2u, chrcs[2].descriptors().size());
        EXPECT_EQ(2u, chrcs[2].id());
        EXPECT_EQ(0x020000u, chrcs[2].descriptors()[0].id());
        EXPECT_EQ(fake_desc2, chrcs[2].descriptors()[0].info());
        EXPECT_EQ(0x020001u, chrcs[2].descriptors()[1].id());
        EXPECT_EQ(fake_desc3, chrcs[2].descriptors()[1].info());
      });

  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  RunLoopUntilIdle();

  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());

  // There should have been two descriptor discovery requests as discovery
  // should have been skipped for characteristic #2 due to its handles.
  EXPECT_EQ(2u, fake_client()->desc_discovery_count());
  EXPECT_TRUE(service->IsDiscovered());
  EXPECT_TRUE(status);
}

// Discover descriptors of a service with multiple characteristics. The first
// request results in an error though others succeed.
TEST_F(GATT_RemoteServiceManagerTest, DiscoverDescriptorsOfMultipleEarlyFail) {
  // Has one descriptor
  CharacteristicData fake_char1(0, 2, 3, kTestUuid3);
  DescriptorData fake_desc1(4, kTestUuid4);

  // Has no descriptors
  CharacteristicData fake_char2(0, 5, 6, kTestUuid3);

  // Has two descriptors
  CharacteristicData fake_char3(0, 7, 8, kTestUuid3);
  DescriptorData fake_desc2(9, kTestUuid4);
  DescriptorData fake_desc3(10, kTestUuid4);

  ServiceData data(1, fake_desc3.handle, kTestServiceUuid1);
  auto service = SetUpFakeService(data);
  fake_client()->set_characteristics({{fake_char1, fake_char2, fake_char3}});
  fake_client()->set_descriptors({{fake_desc1, fake_desc2, fake_desc3}});

  // The first request will fail
  fake_client()->set_descriptor_discovery_status(
      att::Status(HostError::kNotSupported), 1);

  att::Status status(HostError::kFailed);
  service->DiscoverCharacteristics(
      [&](att::Status cb_status, const auto& chrcs) {
        status = cb_status;
        EXPECT_TRUE(chrcs.empty());
      });

  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  RunLoopUntilIdle();

  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());

  // There should have been two descriptor discovery requests as discovery
  // should have been skipped for characteristic #2 due to its handles.
  EXPECT_EQ(2u, fake_client()->desc_discovery_count());
  EXPECT_FALSE(service->IsDiscovered());
  EXPECT_EQ(HostError::kNotSupported, status.error());
}

// Discover descriptors of a service with multiple characteristics. The last
// request results in an error while the preceding ones succeed.
TEST_F(GATT_RemoteServiceManagerTest, DiscoverDescriptorsOfMultipleLateFail) {
  // Has one descriptor
  CharacteristicData fake_char1(0, 2, 3, kTestUuid3);
  DescriptorData fake_desc1(4, kTestUuid4);

  // Has no descriptors
  CharacteristicData fake_char2(0, 5, 6, kTestUuid3);

  // Has two descriptors
  CharacteristicData fake_char3(0, 7, 8, kTestUuid3);
  DescriptorData fake_desc2(9, kTestUuid4);
  DescriptorData fake_desc3(10, kTestUuid4);

  ServiceData data(1, fake_desc3.handle, kTestServiceUuid1);
  auto service = SetUpFakeService(data);
  fake_client()->set_characteristics({{fake_char1, fake_char2, fake_char3}});
  fake_client()->set_descriptors({{fake_desc1, fake_desc2, fake_desc3}});

  // The last request will fail
  fake_client()->set_descriptor_discovery_status(
      att::Status(HostError::kNotSupported), 2);

  att::Status status(HostError::kFailed);
  service->DiscoverCharacteristics(
      [&](att::Status cb_status, const auto& chrcs) {
        status = cb_status;
        EXPECT_TRUE(chrcs.empty());
      });

  EXPECT_EQ(0u, fake_client()->chrc_discovery_count());
  RunLoopUntilIdle();

  EXPECT_EQ(1u, fake_client()->chrc_discovery_count());

  // There should have been two descriptor discovery requests as discovery
  // should have been skipped for characteristic #2 due to its handles.
  EXPECT_EQ(2u, fake_client()->desc_discovery_count());
  EXPECT_FALSE(service->IsDiscovered());
  EXPECT_EQ(HostError::kNotSupported, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, ReadCharAfterShutDown) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  service->ShutDown();

  att::Status status;
  service->ReadCharacteristic(
      0, [&](att::Status cb_status, const auto&) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kFailed, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, ReadCharWhileNotReady) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  att::Status status;
  service->ReadCharacteristic(
      0, [&](att::Status cb_status, const auto&) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotReady, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, ReadCharNotFound) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);
  SetupCharacteristics(service, std::vector<CharacteristicData>());

  att::Status status;
  service->ReadCharacteristic(
      0, [&](att::Status cb_status, const auto&) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotFound, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, ReadCharNotSupported) {
  ServiceData data(1, 3, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  // No "read" property set.
  CharacteristicData chr(0, 2, 3, kTestUuid3);
  SetupCharacteristics(service, {{chr}});

  att::Status status;
  service->ReadCharacteristic(
      0, [&](att::Status cb_status, const auto&) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotSupported, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, ReadCharSendsReadRequest) {
  constexpr att::Handle kValueHandle = 3;

  ServiceData data(1, kValueHandle, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData chr(Property::kRead, 2, kValueHandle, kTestUuid3);
  SetupCharacteristics(service, {{chr}});

  const auto kValue = CreateStaticByteBuffer('t', 'e', 's', 't');

  fake_client()->set_read_request_callback(
      [&](att::Handle handle, auto callback) {
        EXPECT_EQ(kValueHandle, handle);
        callback(att::Status(), kValue);
      });

  att::Status status(HostError::kFailed);
  service->ReadCharacteristic(0, [&](att::Status cb_status, const auto& value) {
    status = cb_status;
    EXPECT_TRUE(common::ContainersEqual(kValue, value));
  });

  RunLoopUntilIdle();

  EXPECT_TRUE(status);
}

TEST_F(GATT_RemoteServiceManagerTest, ReadCharSendsReadRequestWithDispatcher) {
  constexpr att::Handle kValueHandle = 3;

  ServiceData data(1, kValueHandle, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData chr(Property::kRead, 2, kValueHandle, kTestUuid3);
  SetupCharacteristics(service, {{chr}});

  const auto kValue = CreateStaticByteBuffer('t', 'e', 's', 't');

  fake_client()->set_read_request_callback(
      [&](att::Handle handle, auto callback) {
        EXPECT_EQ(kValueHandle, handle);
        callback(att::Status(), kValue);
      });

  att::Status status(HostError::kFailed);
  service->ReadCharacteristic(
      0,
      [&](att::Status cb_status, const auto& value) {
        status = cb_status;
        EXPECT_TRUE(common::ContainersEqual(kValue, value));
      },
      dispatcher());

  RunLoopUntilIdle();

  EXPECT_TRUE(status);
}

TEST_F(GATT_RemoteServiceManagerTest, WriteCharAfterShutDown) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  service->ShutDown();

  att::Status status;
  service->WriteCharacteristic(
      0, std::vector<uint8_t>(),
      [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kFailed, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, WriteCharWhileNotReady) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  att::Status status;
  service->WriteCharacteristic(
      0, std::vector<uint8_t>(),
      [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotReady, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, WriteCharNotFound) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);
  SetupCharacteristics(service, std::vector<CharacteristicData>());

  att::Status status;
  service->WriteCharacteristic(
      0, std::vector<uint8_t>(),
      [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotFound, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, WriteCharNotSupported) {
  ServiceData data(1, 3, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  // No "write" property set.
  CharacteristicData chr(0, 2, 3, kTestUuid3);
  SetupCharacteristics(service, {{chr}});

  att::Status status;
  service->WriteCharacteristic(
      0, std::vector<uint8_t>(),
      [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotSupported, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, WriteCharSendsWriteRequest) {
  constexpr att::Handle kValueHandle = 3;
  const std::vector<uint8_t> kValue{{'t', 'e', 's', 't'}};
  constexpr att::Status kStatus(att::ErrorCode::kWriteNotPermitted);

  ServiceData data(1, kValueHandle, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData chr(Property::kWrite, 2, kValueHandle, kTestUuid3);
  SetupCharacteristics(service, {{chr}});

  fake_client()->set_write_request_callback(
      [&](att::Handle handle, const auto& value, auto status_callback) {
        EXPECT_EQ(kValueHandle, handle);
        EXPECT_TRUE(std::equal(kValue.begin(), kValue.end(), value.begin(),
                               value.end()));
        status_callback(kStatus);
      });

  att::Status status;
  service->WriteCharacteristic(
      0, kValue, [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(kStatus, status);
}

TEST_F(GATT_RemoteServiceManagerTest, EnableNotificationsAfterShutDown) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  service->ShutDown();

  att::Status status;
  service->EnableNotifications(
      0, NopValueCallback,
      [&](att::Status cb_status, IdType) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kFailed, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, EnableNotificationsWhileNotReady) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  att::Status status;
  service->EnableNotifications(
      0, NopValueCallback,
      [&](att::Status cb_status, IdType) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotReady, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, EnableNotificationsCharNotFound) {
  ServiceData data(1, 2, kTestServiceUuid1);
  auto service = SetUpFakeService(data);
  SetupCharacteristics(service, std::vector<CharacteristicData>());

  att::Status status;
  service->EnableNotifications(
      0, NopValueCallback,
      [&](att::Status cb_status, IdType) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotFound, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, EnableNotificationsNoProperties) {
  ServiceData data(1, 4, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  // Has neither the "notify" nor "indicate" property but has a CCC descriptor.
  CharacteristicData chr(Property::kRead, 2, 3, kTestUuid3);
  DescriptorData desc(4, types::kClientCharacteristicConfig);
  SetupCharacteristics(service, {{chr}}, {{desc}});

  att::Status status;
  service->EnableNotifications(
      0, NopValueCallback,
      [&](att::Status cb_status, IdType) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotSupported, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, EnableNotificationsNoCCC) {
  ServiceData data(1, 3, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  // Has the "notify" property but no CCC descriptor.
  CharacteristicData chr(Property::kNotify, 2, 3, kTestUuid3);
  SetupCharacteristics(service, {{chr}});

  att::Status status;
  service->EnableNotifications(
      0, NopValueCallback,
      [&](att::Status cb_status, IdType) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotSupported, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, EnableNotificationsSuccess) {
  constexpr att::Handle kCCCHandle = 4;

  ServiceData data(1, kCCCHandle, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData chr(Property::kNotify, 2, 3, kTestUuid3);
  DescriptorData desc(kCCCHandle, types::kClientCharacteristicConfig);
  SetupCharacteristics(service, {{chr}}, {{desc}});

  fake_client()->set_write_request_callback(
      [&](att::Handle handle, const auto& value, auto status_callback) {
        EXPECT_EQ(kCCCHandle, handle);
        EXPECT_TRUE(ContainersEqual(kCCCNotifyValue, value));
        status_callback(att::Status());
      });

  IdType id = kInvalidId;
  att::Status status(HostError::kFailed);
  service->EnableNotifications(0, NopValueCallback,
                               [&](att::Status cb_status, IdType cb_id) {
                                 status = cb_status;
                                 id = cb_id;
                               });

  RunLoopUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_NE(kInvalidId, id);
}

TEST_F(GATT_RemoteServiceManagerTest, EnableIndications) {
  constexpr att::Handle kCCCHandle = 4;

  ServiceData data(1, 4, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData chr(Property::kIndicate, 2, 3, kTestUuid3);
  DescriptorData desc(kCCCHandle, types::kClientCharacteristicConfig);
  SetupCharacteristics(service, {{chr}}, {{desc}});

  fake_client()->set_write_request_callback(
      [&](att::Handle handle, const auto& value, auto status_callback) {
        EXPECT_EQ(kCCCHandle, handle);
        EXPECT_TRUE(ContainersEqual(kCCCIndicateValue, value));
        status_callback(att::Status());
      });

  IdType id = kInvalidId;
  att::Status status(HostError::kFailed);
  service->EnableNotifications(0, NopValueCallback,
                               [&](att::Status cb_status, IdType cb_id) {
                                 status = cb_status;
                                 id = cb_id;
                               });

  RunLoopUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_NE(kInvalidId, id);
}

TEST_F(GATT_RemoteServiceManagerTest, EnableNotificationsError) {
  constexpr att::Handle kCCCHandle = 4;

  ServiceData data(1, 4, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  CharacteristicData chr(Property::kNotify, 2, 3, kTestUuid3);
  DescriptorData desc(kCCCHandle, types::kClientCharacteristicConfig);
  SetupCharacteristics(service, {{chr}}, {{desc}});

  // Should enable notifications
  const auto kExpectedValue = CreateStaticByteBuffer(0x01, 0x00);

  fake_client()->set_write_request_callback(
      [&](att::Handle handle, const auto& value, auto status_callback) {
        EXPECT_EQ(kCCCHandle, handle);
        EXPECT_TRUE(ContainersEqual(kExpectedValue, value));
        status_callback(att::Status(att::ErrorCode::kUnlikelyError));
      });

  IdType id = kInvalidId;
  att::Status status;
  service->EnableNotifications(0, NopValueCallback,
                               [&](att::Status cb_status, IdType cb_id) {
                                 status = cb_status;
                                 id = cb_id;
                               });

  RunLoopUntilIdle();

  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(att::ErrorCode::kUnlikelyError, status.protocol_error());
  EXPECT_EQ(kInvalidId, id);
}

TEST_F(GATT_RemoteServiceManagerTest, EnableNotificationsRequestMany) {
  constexpr att::Handle kCCCHandle1 = 4;
  constexpr att::Handle kCCCHandle2 = 7;

  ServiceData data(1, 7, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  // Set up two characteristics
  CharacteristicData chr1(Property::kNotify, 2, 3, kTestUuid3);
  DescriptorData desc1(kCCCHandle1, types::kClientCharacteristicConfig);

  CharacteristicData chr2(Property::kIndicate, 5, 6, kTestUuid3);
  DescriptorData desc2(kCCCHandle2, types::kClientCharacteristicConfig);

  SetupCharacteristics(service, {{chr1, chr2}}, {{desc1, desc2}});

  int ccc_write_count = 0;
  att::StatusCallback status_callback1, status_callback2;
  fake_client()->set_write_request_callback(
      [&](att::Handle handle, const auto& value, auto status_cb) {
        if (handle == kCCCHandle1) {
          EXPECT_TRUE(ContainersEqual(kCCCNotifyValue, value));
          status_callback1 = std::move(status_cb);
        } else if (handle == kCCCHandle2) {
          EXPECT_TRUE(ContainersEqual(kCCCIndicateValue, value));
          status_callback2 = std::move(status_cb);
        } else {
          ADD_FAILURE() << "Unexpected handle: " << handle;
        }
        ccc_write_count++;
      });

  size_t cb_count = 0u;

  service->EnableNotifications(0, NopValueCallback,
                               [&](att::Status status, IdType id) {
                                 cb_count++;
                                 EXPECT_EQ(1u, id);
                                 EXPECT_TRUE(status);
                               });
  service->EnableNotifications(0, NopValueCallback,
                               [&](att::Status status, IdType id) {
                                 cb_count++;
                                 EXPECT_EQ(2u, id);
                                 EXPECT_TRUE(status);
                               });
  service->EnableNotifications(1, NopValueCallback,
                               [&](att::Status status, IdType id) {
                                 cb_count++;
                                 EXPECT_EQ(1u, id);
                                 EXPECT_TRUE(status);
                               });
  service->EnableNotifications(1, NopValueCallback,
                               [&](att::Status status, IdType id) {
                                 cb_count++;
                                 EXPECT_EQ(2u, id);
                                 EXPECT_TRUE(status);
                               });
  service->EnableNotifications(1, NopValueCallback,
                               [&](att::Status status, IdType id) {
                                 cb_count++;
                                 EXPECT_EQ(3u, id);
                                 EXPECT_TRUE(status);
                               });

  RunLoopUntilIdle();

  // ATT write requests should be sent but none of the notification requests
  // should be resolved.
  EXPECT_EQ(2, ccc_write_count);
  EXPECT_EQ(0u, cb_count);

  // An ATT response should resolve all pending requests for the right
  // characteristic.
  status_callback1(att::Status());
  EXPECT_EQ(2u, cb_count);
  status_callback2(att::Status());
  EXPECT_EQ(5u, cb_count);

  // An extra request should succeed without sending any PDUs.
  service->EnableNotifications(0, NopValueCallback,
                               [&](att::Status status, IdType) {
                                 cb_count++;
                                 EXPECT_TRUE(status);
                               });

  RunLoopUntilIdle();

  EXPECT_EQ(2, ccc_write_count);
  EXPECT_EQ(6u, cb_count);
}

TEST_F(GATT_RemoteServiceManagerTest, EnableNotificationsRequestManyError) {
  constexpr att::Handle kCCCHandle = 4;

  ServiceData data(1, 4, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  // Set up two characteristics
  CharacteristicData chr(Property::kNotify, 2, 3, kTestUuid3);
  DescriptorData desc(kCCCHandle, types::kClientCharacteristicConfig);

  SetupCharacteristics(service, {{chr}}, {{desc}});

  int ccc_write_count = 0;
  att::StatusCallback status_callback;
  fake_client()->set_write_request_callback(
      [&](att::Handle handle, const auto& value, auto status_cb) {
        EXPECT_EQ(kCCCHandle, handle);
        EXPECT_TRUE(ContainersEqual(kCCCNotifyValue, value));

        ccc_write_count++;
        status_callback = std::move(status_cb);
      });

  int cb_count = 0;
  att::Status status;
  auto cb = [&](att::Status cb_status, IdType id) {
    status = cb_status;
    cb_count++;
  };

  service->EnableNotifications(0, NopValueCallback, std::move(cb));
  service->EnableNotifications(0, NopValueCallback, std::move(cb));
  service->EnableNotifications(0, NopValueCallback, std::move(cb));

  RunLoopUntilIdle();

  // Requests should be buffered and only one ATT request should have been sent
  // out.
  EXPECT_EQ(1, ccc_write_count);
  EXPECT_EQ(0, cb_count);

  status_callback(att::Status(HostError::kNotSupported));
  EXPECT_EQ(3, cb_count);
  EXPECT_EQ(HostError::kNotSupported, status.error());

  // A new request should write to the descriptor again.
  service->EnableNotifications(0, NopValueCallback, std::move(cb));

  RunLoopUntilIdle();

  EXPECT_EQ(2, ccc_write_count);
  EXPECT_EQ(3, cb_count);

  status_callback(att::Status());
  EXPECT_EQ(2, ccc_write_count);
  EXPECT_EQ(4, cb_count);
  EXPECT_TRUE(status);
}

TEST_F(GATT_RemoteServiceManagerTest, NotificationCallback) {
  constexpr IdType kId1 = 0;
  constexpr IdType kId2 = 1;

  ServiceData data(1, 7, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  // Set up two characteristics
  CharacteristicData chr1(Property::kNotify, 2, 3, kTestUuid3);
  DescriptorData desc1(4, types::kClientCharacteristicConfig);

  CharacteristicData chr2(Property::kIndicate, 5, 6, kTestUuid3);
  DescriptorData desc2(7, types::kClientCharacteristicConfig);

  SetupCharacteristics(service, {{chr1, chr2}}, {{desc1, desc2}});

  fake_client()->set_write_request_callback(
      [&](att::Handle, const auto&, auto status_callback) {
        status_callback(att::Status());
      });

  IdType handler_id = kInvalidId;
  att::Status status(HostError::kFailed);

  int chr1_count = 0;
  auto chr1_cb = [&](const ByteBuffer& value) {
    chr1_count++;
    EXPECT_EQ("notify", value.AsString());
  };

  int chr2_count = 0;
  auto chr2_cb = [&](const ByteBuffer& value) {
    chr2_count++;
    EXPECT_EQ("indicate", value.AsString());
  };

  // Notify both characteristics which should get dropped.
  fake_client()->SendNotification(
      false, 3, common::CreateStaticByteBuffer('n', 'o', 't', 'i', 'f', 'y'));
  fake_client()->SendNotification(
      true, 6,
      common::CreateStaticByteBuffer('i', 'n', 'd', 'i', 'c', 'a', 't', 'e'));

  EnableNotifications(service, kId1, &status, &handler_id, std::move(chr1_cb));
  ASSERT_TRUE(status);
  EnableNotifications(service, kId2, &status, &handler_id, std::move(chr2_cb));
  ASSERT_TRUE(status);

  // Notify characteristic 1.
  fake_client()->SendNotification(
      false, 3, common::CreateStaticByteBuffer('n', 'o', 't', 'i', 'f', 'y'));
  EXPECT_EQ(1, chr1_count);
  EXPECT_EQ(0, chr2_count);

  // Notify characteristic 2.
  fake_client()->SendNotification(
      true, 6,
      common::CreateStaticByteBuffer('i', 'n', 'd', 'i', 'c', 'a', 't', 'e'));
  EXPECT_EQ(1, chr1_count);
  EXPECT_EQ(1, chr2_count);

  // Disable notifications from characteristic 1.
  status = att::Status(HostError::kFailed);
  service->DisableNotifications(
      kId1, handler_id, [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();
  EXPECT_TRUE(status);

  // Notifications for characteristic 1 should get dropped.
  fake_client()->SendNotification(
      false, 3, common::CreateStaticByteBuffer('n', 'o', 't', 'i', 'f', 'y'));
  fake_client()->SendNotification(
      true, 6,
      common::CreateStaticByteBuffer('i', 'n', 'd', 'i', 'c', 'a', 't', 'e'));
  EXPECT_EQ(1, chr1_count);
  EXPECT_EQ(2, chr2_count);
}

TEST_F(GATT_RemoteServiceManagerTest, DisableNotificationsAfterShutDown) {
  auto service = SetupNotifiableService();

  IdType id = kInvalidId;
  att::Status status(HostError::kFailed);
  EnableNotifications(service, 0, &status, &id);

  EXPECT_TRUE(status);
  EXPECT_NE(kInvalidId, id);

  service->ShutDown();

  service->DisableNotifications(
      0, id, [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kFailed, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, DisableNotificationsWhileNotReady) {
  ServiceData data(1, 4, kTestServiceUuid1);
  auto service = SetUpFakeService(data);

  att::Status status;
  service->DisableNotifications(
      0, 1, [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotReady, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, DisableNotificationsCharNotFound) {
  auto service = SetupNotifiableService();

  IdType id = kInvalidId;
  att::Status status(HostError::kFailed);
  EnableNotifications(service, 0, &status, &id);

  // "1" is an invalid characteristic ID.
  service->DisableNotifications(
      1, id, [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotFound, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, DisableNotificationsIdNotFound) {
  auto service = SetupNotifiableService();

  IdType id = kInvalidId;
  att::Status status(HostError::kFailed);
  EnableNotifications(service, 0, &status, &id);

  // Valid characteristic ID but invalid notification handler ID.
  service->DisableNotifications(
      0, id + 1, [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kNotFound, status.error());
}

TEST_F(GATT_RemoteServiceManagerTest, DisableNotificationsSingleHandler) {
  constexpr att::Handle kCCCHandle = 4;
  auto service = SetupNotifiableService();

  IdType id = kInvalidId;
  att::Status status(HostError::kFailed);
  EnableNotifications(service, 0, &status, &id);

  // Should disable notifications
  const auto kExpectedValue = CreateStaticByteBuffer(0x00, 0x00);

  int ccc_write_count = 0;
  fake_client()->set_write_request_callback(
      [&](att::Handle handle, const auto& value, auto status_callback) {
        EXPECT_EQ(kCCCHandle, handle);
        EXPECT_TRUE(ContainersEqual(kExpectedValue, value));
        ccc_write_count++;
        status_callback(att::Status());
      });

  status = att::Status(HostError::kFailed);
  service->DisableNotifications(
      0, id, [&](att::Status cb_status) { status = cb_status; });

  RunLoopUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_EQ(1, ccc_write_count);
}

TEST_F(GATT_RemoteServiceManagerTest, DisableNotificationsDuringShutDown) {
  constexpr att::Handle kCCCHandle = 4;
  auto service = SetupNotifiableService();

  IdType id = kInvalidId;
  att::Status status(HostError::kFailed);
  EnableNotifications(service, 0, &status, &id);
  ASSERT_TRUE(status);

  // Should disable notifications
  const auto kExpectedValue = CreateStaticByteBuffer(0x00, 0x00);

  int ccc_write_count = 0;
  fake_client()->set_write_request_callback(
      [&](att::Handle handle, const auto& value, auto status_callback) {
        EXPECT_EQ(kCCCHandle, handle);
        EXPECT_TRUE(ContainersEqual(kExpectedValue, value));
        ccc_write_count++;
        status_callback(att::Status());
      });

  // Shutting down the service should clear the CCC.
  service->ShutDown();
  RunLoopUntilIdle();

  EXPECT_EQ(1, ccc_write_count);
}

TEST_F(GATT_RemoteServiceManagerTest, DisableNotificationsManyHandlers) {
  auto service = SetupNotifiableService();

  IdType id = kInvalidId;
  std::vector<IdType> handler_ids;

  for (int i = 0; i < 2; i++) {
    att::Status status(HostError::kFailed);
    EnableNotifications(service, 0, &status, &id);
    ASSERT_TRUE(status);
    handler_ids.push_back(id);
  }

  int ccc_write_count = 0;
  fake_client()->set_write_request_callback(
      [&](att::Handle handle, const auto& value, auto status_callback) {
        ccc_write_count++;
        status_callback(att::Status());
      });

  // Disabling should succeed without an ATT transaction.
  att::Status status(HostError::kFailed);
  service->DisableNotifications(
      0, handler_ids.back(),
      [&](att::Status cb_status) { status = cb_status; });
  handler_ids.pop_back();
  RunLoopUntilIdle();
  EXPECT_TRUE(status);
  EXPECT_EQ(0, ccc_write_count);

  // Enabling should succeed without an ATT transaction.
  status = att::Status(HostError::kFailed);
  EnableNotifications(service, 0, &status, &id);
  EXPECT_TRUE(status);
  EXPECT_EQ(0, ccc_write_count);
  handler_ids.push_back(id);

  // Disabling all should send out an ATT transaction.
  while (!handler_ids.empty()) {
    att::Status status(HostError::kFailed);
    service->DisableNotifications(
        0, handler_ids.back(),
        [&](att::Status cb_status) { status = cb_status; });
    handler_ids.pop_back();
    RunLoopUntilIdle();
    EXPECT_TRUE(status);
  }

  EXPECT_EQ(1, ccc_write_count);
}

}  // namespace
}  // namespace internal
}  // namespace gatt
}  // namespace btlib
