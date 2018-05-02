// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "generic_attribute_service.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/gatt/gatt_defs.h"

namespace btlib {
namespace gatt {
namespace {

void NopReadHandler(IdType, IdType, uint16_t, const ReadResponder&) {}
void NopWriteHandler(IdType,
                     IdType,
                     uint16_t,
                     const common::ByteBuffer&,
                     const WriteResponder&) {}
void NopCCCallback(IdType service_id,
                   IdType chrc_id,
                   const std::string& peer_id,
                   bool notify,
                   bool indicate) {}
static void NopSendIndication(const std::string&,
                              att::Handle,
                              const common::ByteBuffer&) {}

// Handles for the third attribute (Service Changed characteristic) and fourth
// attribute (corresponding client config).
static constexpr att::Handle kChrcHandle = 0x0003;
static constexpr att::Handle kCCCHandle = 0x0004;

static constexpr char kTestDeviceId[] = "11223344-1122-1122-1122-112233445566";

static constexpr uint16_t kEnableInd = 0x0002;

class GATT_GenericAttributeServiceTest : public ::testing::Test {
 protected:
  bool WriteServiceChangedCCC(const std::string& device_id,
                              uint16_t ccc_value,
                              att::ErrorCode* out_ecode) {
    FXL_DCHECK(out_ecode);

    auto* attr = mgr.database()->FindAttribute(kCCCHandle);
    FXL_DCHECK(attr);
    auto result_cb = [&out_ecode](auto cb_code) { *out_ecode = cb_code; };
    uint16_t value = htole16(ccc_value);
    return attr->WriteAsync(
        device_id, 0u, common::BufferView(&value, sizeof(value)), result_cb);
  }

  LocalServiceManager mgr;
};

// Test registration and unregistration of the local GATT service itself.
TEST_F(GATT_GenericAttributeServiceTest, RegisterUnregister) {
  {
    GenericAttributeService gatt_service(&mgr, NopSendIndication);

    // Check that the local attribute database has a grouping for the GATT GATT
    // service with four attributes.
    auto iter = mgr.database()->groupings().begin();
    EXPECT_TRUE(iter->complete());
    EXPECT_EQ(4u, iter->attributes().size());
    EXPECT_TRUE(iter->active());
    EXPECT_EQ(0x0001, iter->start_handle());
    EXPECT_EQ(0x0004, iter->end_handle());
    EXPECT_EQ(types::kPrimaryService, iter->group_type());

    auto const* ccc_attr = mgr.database()->FindAttribute(kCCCHandle);
    ASSERT_TRUE(ccc_attr != nullptr);
    EXPECT_EQ(types::kClientCharacteristicConfig, ccc_attr->type());
  }

  // The service should now be unregistered, so no characeteristic attributes
  // should be active.
  auto const* chrc_attr = mgr.database()->FindAttribute(kChrcHandle);
  ASSERT_TRUE(chrc_attr == nullptr);
}

// Tests that registering the GATT service, enabling indication on its Service
// Changed characteristic, then registering a different service invokes the
// callback to send an indication to the "client."
TEST_F(GATT_GenericAttributeServiceTest, IndicateOnRegister) {
  int callback_count = 0;
  auto send_indication = [&](const std::string& peer_id,
                             att::Handle handle,
                             const common::ByteBuffer& value) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(kChrcHandle, handle);
    ASSERT_EQ(4u, value.size());

    // The second service following the four-attribute GATT service should span
    // the subsequent three handles.
    EXPECT_EQ(0x05, value[0]);
    EXPECT_EQ(0x00, value[1]);
    EXPECT_EQ(0x07, value[2]);
    EXPECT_EQ(0x00, value[3]);
    callback_count++;
  };

  // Register the GATT service.
  GenericAttributeService gatt_service(&mgr, std::move(send_indication));

  // Enable Service Changed indications for the test client.
  att::ErrorCode ecode;
  WriteServiceChangedCCC(kTestDeviceId, kEnableInd, &ecode);
  EXPECT_EQ(0, callback_count);

  constexpr common::UUID kTestSvcType((uint32_t)0xdeadbeef);
  constexpr IdType kChrcId = 0;
  constexpr uint8_t kChrcProps = Property::kRead;
  constexpr common::UUID kTestChrcType((uint32_t)0xdeadbeef);
  const att::AccessRequirements kReadReqs(true, true, true);
  const att::AccessRequirements kWriteReqs, kUpdateReqs;
  auto service = std::make_unique<Service>(false /* primary */, kTestSvcType);
  service->AddCharacteristic(
      std::make_unique<Characteristic>(kChrcId, kTestChrcType, kChrcProps, 0,
                                       kReadReqs, kWriteReqs, kUpdateReqs));
  auto service_id = mgr.RegisterService(std::move(service), NopReadHandler,
                                        NopWriteHandler, NopCCCallback);
  EXPECT_NE(0u, service_id);
  EXPECT_EQ(1, callback_count);
}

// Same test as above, but the indication is enabled just prior unregistering
// the latter test service.
TEST_F(GATT_GenericAttributeServiceTest, IndicateOnUnregister) {
  int callback_count = 0;
  auto send_indication = [&](const std::string& peer_id,
                             att::Handle handle,
                             const common::ByteBuffer& value) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(kChrcHandle, handle);
    ASSERT_EQ(4u, value.size());

    // The second service following the four-attribute GATT service should span
    // the subsequent four handles (update enabled).
    EXPECT_EQ(0x05, value[0]);
    EXPECT_EQ(0x00, value[1]);
    EXPECT_EQ(0x08, value[2]);
    EXPECT_EQ(0x00, value[3]);
    callback_count++;
  };

  // Register the GATT service.
  GenericAttributeService gatt_service(&mgr, std::move(send_indication));

  constexpr common::UUID kTestSvcType((uint32_t)0xdeadbeef);
  constexpr IdType kChrcId = 0;
  constexpr uint8_t kChrcProps = Property::kNotify;
  constexpr common::UUID kTestChrcType((uint32_t)0xdeadbeef);
  const att::AccessRequirements kReadReqs, kWriteReqs;
  const att::AccessRequirements kUpdateReqs(true, true, true);
  auto service = std::make_unique<Service>(false /* primary */, kTestSvcType);
  service->AddCharacteristic(
      std::make_unique<Characteristic>(kChrcId, kTestChrcType, kChrcProps, 0,
                                       kReadReqs, kWriteReqs, kUpdateReqs));
  auto service_id = mgr.RegisterService(std::move(service), NopReadHandler,
                                        NopWriteHandler, NopCCCallback);
  EXPECT_NE(0u, service_id);

  // Enable Service Changed indications for the test client.
  att::ErrorCode ecode;
  WriteServiceChangedCCC(kTestDeviceId, kEnableInd, &ecode);
  EXPECT_EQ(0, callback_count);

  mgr.UnregisterService(service_id);
  EXPECT_EQ(1, callback_count);
}

}  // namespace
}  // namespace gatt
}  // namespace btlib
