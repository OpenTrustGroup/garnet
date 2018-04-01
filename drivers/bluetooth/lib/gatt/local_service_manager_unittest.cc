// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gatt/local_service_manager.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt_defs.h"

namespace btlib {
namespace gatt {
namespace {

constexpr char kTestDeviceId[] = "11223344-1122-1122-1122-112233445566";
constexpr char kTestDeviceId2[] = "00112233-4411-2211-2211-221122334455";
constexpr common::UUID kTestType16((uint16_t)0xdead);
constexpr common::UUID kTestType32((uint32_t)0xdeadbeef);

// The first characteristic value attribute of the first service has handle
// number 3.
constexpr att::Handle kFirstChrcValueHandle = 0x0003;

// The first descroptor of the first characteristic of the first service has
// handle number 4.
constexpr att::Handle kFirstDescrHandle = 0x0004;

inline att::AccessRequirements AllowedNoSecurity() {
  return att::AccessRequirements(false, false, false);
}

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

// Convenience function that registers |service| with |mgr| using the NOP
// handlers above by default.
IdType RegisterService(LocalServiceManager* mgr,
                       ServicePtr service,
                       ReadHandler read_handler = NopReadHandler,
                       WriteHandler write_handler = NopWriteHandler,
                       ClientConfigCallback ccc_callback = NopCCCallback) {
  return mgr->RegisterService(std::move(service), read_handler, write_handler,
                              ccc_callback);
}

TEST(GATT_LocalServiceManagerTest, EmptyService) {
  LocalServiceManager mgr;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto id1 = RegisterService(&mgr, std::move(service));
  EXPECT_NE(0u, id1);

  service = std::make_unique<Service>(false /* primary */, kTestType32);
  auto id2 = RegisterService(&mgr, std::move(service));
  EXPECT_NE(0u, id2);

  EXPECT_EQ(2u, mgr.database()->groupings().size());

  auto iter = mgr.database()->groupings().begin();

  EXPECT_TRUE(iter->complete());
  EXPECT_EQ(1u, iter->attributes().size());
  EXPECT_TRUE(iter->active());
  EXPECT_EQ(0x0001, iter->start_handle());
  EXPECT_EQ(0x0001, iter->end_handle());
  EXPECT_EQ(types::kPrimaryService, iter->group_type());
  EXPECT_TRUE(common::ContainersEqual(
      common::CreateStaticByteBuffer(0xad, 0xde), iter->decl_value()));

  iter++;

  EXPECT_TRUE(iter->complete());
  EXPECT_EQ(1u, iter->attributes().size());
  EXPECT_TRUE(iter->active());
  EXPECT_EQ(0x0002, iter->start_handle());
  EXPECT_EQ(0x0002, iter->end_handle());
  EXPECT_EQ(types::kSecondaryService, iter->group_type());
  EXPECT_TRUE(common::ContainersEqual(
      common::CreateStaticByteBuffer(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00,
                                     0x80, 0x00, 0x10, 0x00, 0x00, 0xef, 0xbe,
                                     0xad, 0xde),
      iter->decl_value()));
}

TEST(GATT_LocalServiceManagerTest, UnregisterService) {
  LocalServiceManager mgr;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto id1 = RegisterService(&mgr, std::move(service));
  EXPECT_NE(0u, id1);
  EXPECT_EQ(1u, mgr.database()->groupings().size());

  // Unknown id
  EXPECT_FALSE(mgr.UnregisterService(id1 + 1));

  // Success
  EXPECT_TRUE(mgr.UnregisterService(id1));
  EXPECT_TRUE(mgr.database()->groupings().empty());

  // |id1| becomes unknown
  EXPECT_FALSE(mgr.UnregisterService(id1));
}

TEST(GATT_LocalServiceManagerTest, RegisterCharacteristic) {
  LocalServiceManager mgr;

  constexpr IdType kChrcId = 0;
  constexpr uint8_t kChrcProps = Property::kRead;
  constexpr common::UUID kTestChrcType((uint16_t)0xabcd);
  const att::AccessRequirements kReadReqs(true, true, true);
  const att::AccessRequirements kWriteReqs, kUpdateReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(
      std::make_unique<Characteristic>(kChrcId, kTestChrcType, kChrcProps, 0,
                                       kReadReqs, kWriteReqs, kUpdateReqs));
  auto id1 = RegisterService(&mgr, std::move(service));
  EXPECT_NE(0u, id1);

  ASSERT_EQ(1u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  EXPECT_TRUE(grouping.complete());

  const auto& attrs = grouping.attributes();
  ASSERT_EQ(3u, attrs.size());

  att::Handle srvc_handle = attrs[0].handle();
  EXPECT_EQ(att::kHandleMin, srvc_handle);

  // Characteristic declaration
  EXPECT_EQ(srvc_handle + 1, attrs[1].handle());
  EXPECT_EQ(types::kCharacteristicDeclaration, attrs[1].type());
  EXPECT_EQ(AllowedNoSecurity(), attrs[1].read_reqs());
  EXPECT_EQ(att::AccessRequirements(), attrs[1].write_reqs());
  EXPECT_TRUE(attrs[1].value());

  // clang-format off
  const auto kDeclValue = common::CreateStaticByteBuffer(
      0x02,        // properties
      0x03, 0x00,  // value handle
      0xcd, 0xab   // UUID
  );
  // clang-format on
  EXPECT_TRUE(common::ContainersEqual(kDeclValue, *attrs[1].value()));

  // Characteristic value
  EXPECT_EQ(srvc_handle + 2, attrs[2].handle());
  EXPECT_EQ(kTestChrcType, attrs[2].type());
  EXPECT_EQ(kReadReqs, attrs[2].read_reqs());
  EXPECT_EQ(kWriteReqs, attrs[2].write_reqs());

  // This value is dynamic.
  EXPECT_FALSE(attrs[2].value());
}

TEST(GATT_LocalServiceManagerTest, RegisterCharacteristic32) {
  LocalServiceManager mgr;

  constexpr IdType kChrcId = 0;
  constexpr uint8_t kChrcProps = Property::kRead;
  constexpr common::UUID kTestChrcType((uint32_t)0xdeadbeef);
  const att::AccessRequirements kReadReqs(true, true, true);
  const att::AccessRequirements kWriteReqs, kUpdateReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(
      std::make_unique<Characteristic>(kChrcId, kTestChrcType, kChrcProps, 0,
                                       kReadReqs, kWriteReqs, kUpdateReqs));
  auto id1 = RegisterService(&mgr, std::move(service));
  EXPECT_NE(0u, id1);

  ASSERT_EQ(1u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  EXPECT_TRUE(grouping.complete());

  const auto& attrs = grouping.attributes();
  ASSERT_EQ(3u, attrs.size());

  att::Handle srvc_handle = attrs[0].handle();
  EXPECT_EQ(att::kHandleMin, srvc_handle);

  // Characteristic declaration
  EXPECT_EQ(srvc_handle + 1, attrs[1].handle());
  EXPECT_EQ(types::kCharacteristicDeclaration, attrs[1].type());
  EXPECT_EQ(AllowedNoSecurity(), attrs[1].read_reqs());
  EXPECT_EQ(att::AccessRequirements(), attrs[1].write_reqs());
  EXPECT_TRUE(attrs[1].value());

  const auto kDeclValue = common::CreateStaticByteBuffer(
      0x02,        // properties
      0x03, 0x00,  // value handle

      // The 32-bit UUID will be stored as 128-bit
      0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
      0xef, 0xbe, 0xad, 0xde);
  EXPECT_TRUE(common::ContainersEqual(kDeclValue, *attrs[1].value()));

  // Characteristic value
  EXPECT_EQ(srvc_handle + 2, attrs[2].handle());
  EXPECT_EQ(kTestChrcType, attrs[2].type());
  EXPECT_EQ(kReadReqs, attrs[2].read_reqs());
  EXPECT_EQ(kWriteReqs, attrs[2].write_reqs());

  // This value is dynamic.
  EXPECT_FALSE(attrs[2].value());
}

TEST(GATT_LocalServiceManagerTest, RegisterCharacteristic128) {
  LocalServiceManager mgr;

  constexpr IdType kChrcId = 0;
  constexpr uint8_t kChrcProps = Property::kRead;
  common::UUID kTestChrcType;
  EXPECT_TRUE(common::StringToUuid("00112233-4455-6677-8899-AABBCCDDEEFF",
                                   &kTestChrcType));
  const att::AccessRequirements kReadReqs(true, true, true);
  const att::AccessRequirements kWriteReqs, kUpdateReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(
      std::make_unique<Characteristic>(kChrcId, kTestChrcType, kChrcProps, 0,
                                       kReadReqs, kWriteReqs, kUpdateReqs));
  auto id1 = RegisterService(&mgr, std::move(service));
  EXPECT_NE(0u, id1);

  ASSERT_EQ(1u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  EXPECT_TRUE(grouping.complete());

  const auto& attrs = grouping.attributes();
  ASSERT_EQ(3u, attrs.size());

  att::Handle srvc_handle = attrs[0].handle();
  EXPECT_EQ(att::kHandleMin, srvc_handle);

  // Characteristic declaration
  EXPECT_EQ(srvc_handle + 1, attrs[1].handle());
  EXPECT_EQ(types::kCharacteristicDeclaration, attrs[1].type());
  EXPECT_EQ(AllowedNoSecurity(), attrs[1].read_reqs());
  EXPECT_EQ(att::AccessRequirements(), attrs[1].write_reqs());
  EXPECT_TRUE(attrs[1].value());

  const auto kDeclValue = common::CreateStaticByteBuffer(
      0x02,        // properties
      0x03, 0x00,  // value handle

      // 128-bit UUID
      0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44,
      0x33, 0x22, 0x11, 0x00);
  EXPECT_TRUE(common::ContainersEqual(kDeclValue, *attrs[1].value()));

  // Characteristic value
  EXPECT_EQ(srvc_handle + 2, attrs[2].handle());
  EXPECT_EQ(kTestChrcType, attrs[2].type());
  EXPECT_EQ(kReadReqs, attrs[2].read_reqs());
  EXPECT_EQ(kWriteReqs, attrs[2].write_reqs());

  // This value is dynamic.
  EXPECT_FALSE(attrs[2].value());
}

TEST(GATT_LocalServiceManagerTest, ExtPropSetSuccess) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);

  constexpr uint8_t kChrcProps = Property::kRead;
  constexpr uint8_t kExtChrcProps = ExtendedProperty::kReliableWrite;
  auto chrc = std::make_unique<Characteristic>(kChrcId, kChrcType16, kChrcProps,
                                               kExtChrcProps, kReadReqs,
                                               kWriteReqs, kUpdateReqs);
  service->AddCharacteristic(std::move(chrc));

  ASSERT_TRUE(RegisterService(&mgr, std::move(service)));
  ASSERT_NE(0u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  const auto& attrs = grouping.attributes();
  ASSERT_EQ(4u, attrs.size());
  EXPECT_EQ(types::kCharacteristicExtProperties, attrs[3].type());
  EXPECT_TRUE(common::ContainersEqual(  // Reliable Write property
      common::CreateStaticByteBuffer(0x01, 0x00), *attrs[3].value()));
}

// tests that the extended properties descriptor cannot be set externally
TEST(GATT_LocalServiceManagerTest, ExtPropSetFailure) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;

  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x2900);  // UUID for Ext Prop

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(0, kChrcType16, 0, 0, kReadReqs,
                                               kWriteReqs, kUpdateReqs);
  chrc->AddDescriptor(
      std::make_unique<Descriptor>(1, kDescType16, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  EXPECT_EQ(false, RegisterService(&mgr, std::move(service)));
}

TEST(GATT_LocalServiceManagerTest, RegisterCharacteristicSorted) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;

  constexpr common::UUID kType16((uint16_t)0xbeef);
  constexpr common::UUID kType128((uint32_t)0xdeadbeef);

  constexpr IdType kChrcId0 = 0;
  constexpr uint8_t kChrcProps0 = 0;
  constexpr IdType kChrcId1 = 1;
  constexpr uint8_t kChrcProps1 = 1;
  constexpr IdType kChrcId2 = 2;
  constexpr uint8_t kChrcProps2 = 2;
  constexpr IdType kChrcId3 = 3;
  constexpr uint8_t kChrcProps3 = 3;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId0, kType128, kChrcProps0, 0, kReadReqs, kWriteReqs, kUpdateReqs));
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId1, kType16, kChrcProps1, 0, kReadReqs, kWriteReqs, kUpdateReqs));
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId2, kType128, kChrcProps2, 0, kReadReqs, kWriteReqs, kUpdateReqs));
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId3, kType16, kChrcProps3, 0, kReadReqs, kWriteReqs, kUpdateReqs));
  auto id1 = RegisterService(&mgr, std::move(service));
  EXPECT_NE(0u, id1);

  ASSERT_EQ(1u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  EXPECT_TRUE(grouping.complete());

  const auto& attrs = grouping.attributes();
  ASSERT_EQ(9u, attrs.size());

  // The declaration attributes should be sorted by service type (16-bit UUIDs
  // first).
  ASSERT_TRUE(attrs[1].value());
  EXPECT_EQ(kChrcProps1, (*attrs[1].value())[0]);
  ASSERT_TRUE(attrs[3].value());
  EXPECT_EQ(kChrcProps3, (*attrs[3].value())[0]);
  ASSERT_TRUE(attrs[5].value());
  EXPECT_EQ(kChrcProps0, (*attrs[5].value())[0]);
  ASSERT_TRUE(attrs[7].value());
  EXPECT_EQ(kChrcProps2, (*attrs[7].value())[0]);
}

TEST(GATT_LocalServiceManagerTest, RegisterDescriptor) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;

  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(0, kChrcType16, 0, 0, kReadReqs,
                                               kWriteReqs, kUpdateReqs);
  chrc->AddDescriptor(
      std::make_unique<Descriptor>(1, kDescType16, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  EXPECT_NE(0u, RegisterService(&mgr, std::move(service)));
  ASSERT_EQ(1u, mgr.database()->groupings().size());
  const auto& grouping = mgr.database()->groupings().front();
  EXPECT_TRUE(grouping.complete());

  const auto& attrs = grouping.attributes();
  ASSERT_EQ(4u, attrs.size());
  EXPECT_EQ(types::kCharacteristicDeclaration, attrs[1].type());
  EXPECT_EQ(kChrcType16, attrs[2].type());
  EXPECT_EQ(kDescType16, attrs[3].type());
  EXPECT_FALSE(attrs[3].value());
}

TEST(GATT_LocalServiceManagerTest, DuplicateChrcIds) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;

  constexpr common::UUID kChrcType16((uint16_t)0x1234);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);

  // Use same characteristic ID twice.
  service->AddCharacteristic(std::make_unique<Characteristic>(
      0, kChrcType16, 0, 0, kReadReqs, kWriteReqs, kUpdateReqs));
  service->AddCharacteristic(std::make_unique<Characteristic>(
      0, kChrcType16, 0, 0, kReadReqs, kWriteReqs, kUpdateReqs));

  EXPECT_EQ(0u, RegisterService(&mgr, std::move(service)));
}

TEST(GATT_LocalServiceManagerTest, DuplicateDescIds) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;

  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);

  // Use same descriptor ID twice.
  auto chrc = std::make_unique<Characteristic>(0, kChrcType16, 0, 0, kReadReqs,
                                               kWriteReqs, kUpdateReqs);
  chrc->AddDescriptor(
      std::make_unique<Descriptor>(1, kDescType16, kReadReqs, kWriteReqs));
  chrc->AddDescriptor(
      std::make_unique<Descriptor>(1, kDescType16, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  EXPECT_EQ(0u, RegisterService(&mgr, std::move(service)));
}

TEST(GATT_LocalServiceManagerTest, DuplicateChrcAndDescIds) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;

  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);

  // Use same descriptor ID twice.
  auto chrc = std::make_unique<Characteristic>(0, kChrcType16, 0, 0, kReadReqs,
                                               kWriteReqs, kUpdateReqs);
  chrc->AddDescriptor(
      std::make_unique<Descriptor>(0, kDescType16, kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  EXPECT_EQ(0u, RegisterService(&mgr, std::move(service)));
}

TEST(GATT_LocalServiceManagerTest, ReadCharacteristicNoReadPermission) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(
      std::make_unique<Characteristic>(kChrcId, kChrcType16, Property::kRead, 0,
                                       kReadReqs, kWriteReqs, kUpdateReqs));

  bool called = false;
  auto read_cb = [&called](auto, auto, auto, auto&) { called = true; };

  EXPECT_NE(0u, RegisterService(&mgr, std::move(service), read_cb));

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  bool result_called = false;
  auto result_cb = [&result_called](auto, const auto&) {
    result_called = true;
  };

  EXPECT_FALSE(attr->ReadAsync(kTestDeviceId, 0, result_cb));
  EXPECT_FALSE(called);
  EXPECT_FALSE(result_called);
}

TEST(GATT_LocalServiceManagerTest, ReadCharacteristicNoReadProperty) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;

  // Characteristic is readable but doesn't have the "read" property.
  auto kReadReqs = AllowedNoSecurity();
  const att::AccessRequirements kWriteReqs, kUpdateReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kChrcType16, 0, 0, kReadReqs, kWriteReqs, kUpdateReqs));

  bool called = false;
  auto read_cb = [&called](auto, auto, auto, auto&) { called = true; };

  EXPECT_NE(0u, RegisterService(&mgr, std::move(service), read_cb));

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kNoError;
  auto result_cb = [&ecode](auto code, const auto&) { ecode = code; };

  EXPECT_TRUE(attr->ReadAsync(kTestDeviceId, 0, result_cb));

  // The error should be handled internally and not reach |read_cb|.
  EXPECT_FALSE(called);
  EXPECT_EQ(att::ErrorCode::kReadNotPermitted, ecode);
}

TEST(GATT_LocalServiceManagerTest, ReadCharacteristic) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;
  constexpr uint16_t kOffset = 10;

  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');

  auto kReadReqs = AllowedNoSecurity();
  const att::AccessRequirements kWriteReqs, kUpdateReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(
      std::make_unique<Characteristic>(kChrcId, kChrcType16, Property::kRead, 0,
                                       kReadReqs, kWriteReqs, kUpdateReqs));

  bool called = false;
  IdType svc_id;
  auto read_cb = [&](auto cb_svc_id, auto id, auto offset,
                     const auto& responder) {
    called = true;
    EXPECT_EQ(svc_id, cb_svc_id);
    EXPECT_EQ(kChrcId, id);
    EXPECT_EQ(kOffset, offset);
    responder(att::ErrorCode::kNoError, kTestValue);
  };

  svc_id = RegisterService(&mgr, std::move(service), read_cb);
  ASSERT_NE(0u, svc_id);

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kUnlikelyError;
  auto result_cb = [&ecode, &kTestValue](auto code, const auto& value) {
    ecode = code;
    EXPECT_TRUE(common::ContainersEqual(kTestValue, value));
  };

  EXPECT_TRUE(attr->ReadAsync(kTestDeviceId, kOffset, result_cb));

  EXPECT_TRUE(called);
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
}

TEST(GATT_LocalServiceManagerTest, WriteCharacteristicNoWritePermission) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;
  const common::BufferView kTestValue;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(
      std::make_unique<Characteristic>(kChrcId, kChrcType16, Property::kWrite,
                                       0, kReadReqs, kWriteReqs, kUpdateReqs));

  bool called = false;
  auto write_cb = [&called](auto, auto, auto, auto&, auto&) { called = true; };

  EXPECT_NE(
      0u, RegisterService(&mgr, std::move(service), NopReadHandler, write_cb));

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  bool result_called = false;
  auto result_cb = [&result_called](auto) { result_called = true; };

  EXPECT_FALSE(attr->WriteAsync(kTestDeviceId, 0, kTestValue, result_cb));
  EXPECT_FALSE(called);
  EXPECT_FALSE(result_called);
}

TEST(GATT_LocalServiceManagerTest, WriteCharacteristicNoWriteProperty) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;
  const common::BufferView kTestValue;

  const att::AccessRequirements kReadReqs, kUpdateReqs;
  auto kWriteReqs = AllowedNoSecurity();

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(std::make_unique<Characteristic>(
      kChrcId, kChrcType16, 0, 0, kReadReqs, kWriteReqs, kUpdateReqs));

  bool called = false;
  auto write_cb = [&called](auto, auto, auto, auto&, auto&) { called = true; };

  EXPECT_NE(
      0u, RegisterService(&mgr, std::move(service), NopReadHandler, write_cb));

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kNoError;
  auto result_cb = [&ecode](auto code) { ecode = code; };

  EXPECT_TRUE(attr->WriteAsync(kTestDeviceId, 0, kTestValue, result_cb));

  // The error should be handled internally and not reach |write_cb|.
  EXPECT_FALSE(called);
  EXPECT_EQ(att::ErrorCode::kWriteNotPermitted, ecode);
}

TEST(GATT_LocalServiceManagerTest, WriteCharacteristic) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr IdType kChrcId = 5;
  constexpr uint16_t kOffset = 10;

  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');

  const att::AccessRequirements kReadReqs, kUpdateReqs;
  auto kWriteReqs = AllowedNoSecurity();

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  service->AddCharacteristic(
      std::make_unique<Characteristic>(kChrcId, kChrcType16, Property::kWrite,
                                       0, kReadReqs, kWriteReqs, kUpdateReqs));

  bool called = false;
  IdType svc_id;
  auto write_cb = [&](auto cb_svc_id, auto id, auto offset, const auto& value,
                      const auto& responder) {
    called = true;
    EXPECT_EQ(svc_id, cb_svc_id);
    EXPECT_EQ(kChrcId, id);
    EXPECT_EQ(kOffset, offset);
    EXPECT_TRUE(common::ContainersEqual(kTestValue, value));
    responder(att::ErrorCode::kNoError);
  };

  svc_id = RegisterService(&mgr, std::move(service), NopReadHandler, write_cb);
  ASSERT_NE(0u, svc_id);

  auto* attr = mgr.database()->FindAttribute(kFirstChrcValueHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kChrcType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kUnlikelyError;
  auto result_cb = [&ecode](auto code) { ecode = code; };

  EXPECT_TRUE(attr->WriteAsync(kTestDeviceId, kOffset, kTestValue, result_cb));

  EXPECT_TRUE(called);
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
}

TEST(GATT_LocalServiceManagerTest, ReadDescriptorNoReadPermission) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);
  constexpr IdType kChrcId = 0;
  constexpr IdType kDescId = 1;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(
      kChrcId, kChrcType16, 0, 0, kReadReqs, kWriteReqs, kUpdateReqs);
  chrc->AddDescriptor(std::make_unique<Descriptor>(kDescId, kDescType16,
                                                   kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  bool called = false;
  auto read_cb = [&called](auto, auto, auto, auto&) { called = true; };

  EXPECT_NE(0u, RegisterService(&mgr, std::move(service), read_cb));

  auto* attr = mgr.database()->FindAttribute(kFirstDescrHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kDescType16, attr->type());

  bool result_called = false;
  auto result_cb = [&result_called](auto, const auto&) {
    result_called = true;
  };

  EXPECT_FALSE(attr->ReadAsync(kTestDeviceId, 0, result_cb));
  EXPECT_FALSE(called);
  EXPECT_FALSE(result_called);
}

TEST(GATT_LocalServiceManagerTest, ReadDescriptor) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);
  constexpr IdType kChrcId = 0;
  constexpr IdType kDescId = 1;
  constexpr uint16_t kOffset = 10;

  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');

  auto kReadReqs = AllowedNoSecurity();
  const att::AccessRequirements kWriteReqs, kUpdateReqs;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(
      kChrcId, kChrcType16, 0, 0, kReadReqs, kWriteReqs, kUpdateReqs);
  chrc->AddDescriptor(
      std::make_unique<Descriptor>(kDescId, kDescType16, kReadReqs, kReadReqs));
  service->AddCharacteristic(std::move(chrc));

  bool called = false;
  IdType svc_id;
  auto read_cb = [&](auto cb_svc_id, auto id, auto offset,
                     const auto& responder) {
    called = true;
    EXPECT_EQ(svc_id, cb_svc_id);
    EXPECT_EQ(kDescId, id);
    EXPECT_EQ(kOffset, offset);
    responder(att::ErrorCode::kNoError, kTestValue);
  };

  svc_id = RegisterService(&mgr, std::move(service), read_cb);
  ASSERT_NE(0u, svc_id);

  auto* attr = mgr.database()->FindAttribute(kFirstDescrHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kDescType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kUnlikelyError;
  auto result_cb = [&ecode, &kTestValue](auto code, const auto& value) {
    ecode = code;
    EXPECT_TRUE(common::ContainersEqual(kTestValue, value));
  };

  EXPECT_TRUE(attr->ReadAsync(kTestDeviceId, kOffset, result_cb));

  EXPECT_TRUE(called);
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
}

TEST(GATT_LocalServiceManagerTest, WriteDescriptorNoWritePermission) {
  LocalServiceManager mgr;
  const att::AccessRequirements kReadReqs, kWriteReqs, kUpdateReqs;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);
  constexpr IdType kChrcId = 0;
  constexpr IdType kDescId = 1;
  const common::BufferView kTestValue;

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(
      kChrcId, kChrcType16, 0, 0, kReadReqs, kWriteReqs, kUpdateReqs);
  chrc->AddDescriptor(std::make_unique<Descriptor>(kDescId, kDescType16,
                                                   kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  bool called = false;
  auto write_cb = [&called](auto, auto, auto, auto&, auto&) { called = true; };

  EXPECT_NE(
      0u, RegisterService(&mgr, std::move(service), NopReadHandler, write_cb));

  auto* attr = mgr.database()->FindAttribute(kFirstDescrHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kDescType16, attr->type());

  bool result_called = false;
  auto result_cb = [&result_called](auto) { result_called = true; };

  EXPECT_FALSE(attr->WriteAsync(kTestDeviceId, 0, kTestValue, result_cb));
  EXPECT_FALSE(called);
  EXPECT_FALSE(result_called);
}

TEST(GATT_LocalServiceManagerTest, WriteDescriptor) {
  LocalServiceManager mgr;
  constexpr common::UUID kChrcType16((uint16_t)0x1234);
  constexpr common::UUID kDescType16((uint16_t)0x5678);
  constexpr IdType kChrcId = 0;
  constexpr IdType kDescId = 1;
  constexpr uint16_t kOffset = 10;

  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');

  const att::AccessRequirements kReadReqs, kUpdateReqs;
  auto kWriteReqs = AllowedNoSecurity();

  auto service = std::make_unique<Service>(true /* primary */, kTestType16);
  auto chrc = std::make_unique<Characteristic>(
      kChrcId, kChrcType16, 0, 0, kReadReqs, kWriteReqs, kUpdateReqs);
  chrc->AddDescriptor(std::make_unique<Descriptor>(kDescId, kDescType16,
                                                   kReadReqs, kWriteReqs));
  service->AddCharacteristic(std::move(chrc));

  bool called = false;
  IdType svc_id;
  auto write_cb = [&](auto cb_svc_id, auto id, auto offset, const auto& value,
                      const auto& responder) {
    called = true;
    EXPECT_EQ(svc_id, cb_svc_id);
    EXPECT_EQ(kDescId, id);
    EXPECT_EQ(kOffset, offset);
    EXPECT_TRUE(common::ContainersEqual(kTestValue, value));
    responder(att::ErrorCode::kNoError);
  };

  svc_id = RegisterService(&mgr, std::move(service), NopReadHandler, write_cb);
  ASSERT_NE(0u, svc_id);

  auto* attr = mgr.database()->FindAttribute(kFirstDescrHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(kDescType16, attr->type());

  att::ErrorCode ecode = att::ErrorCode::kUnlikelyError;
  auto result_cb = [&ecode](auto code) { ecode = code; };

  EXPECT_TRUE(attr->WriteAsync(kTestDeviceId, kOffset, kTestValue, result_cb));

  EXPECT_TRUE(called);
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
}

// TODO(armansito): Some functional groupings of tests above (such as
// ReadCharacteristic, WriteCharacteristic, etc) should each use a common test
// harness to reduce code duplication.
class GATT_LocalClientCharacteristicConfigurationTest : public ::testing::Test {
 protected:
  LocalServiceManager mgr;

  // The CCC descriptor is set up as the first descriptor of the first
  // characteristic of the first service:
  //   0x0001: service decl.
  //   0x0002: characteristic decl.
  //   0x0003: characteristic value
  //   0x0004: CCC descriptor
  const att::Handle kChrcHandle = 0x0003;
  const att::Handle kCCCHandle = 0x0004;

  const IdType kChrcId = 0;

  const uint16_t kEnableNot = 0x0001;
  const uint16_t kEnableInd = 0x0002;

  int ccc_callback_count = 0;
  IdType last_service_id = 0u;
  std::string last_peer_id;
  bool last_notify = false;
  bool last_indicate = false;

  void BuildService(uint8_t props, const att::AccessRequirements& update_reqs) {
    const att::AccessRequirements kReqs;
    auto service =
        std::make_unique<Service>(true /* is_primary */, kTestType16);
    service->AddCharacteristic(std::make_unique<Characteristic>(
        kChrcId, kTestType32, props, 0, kReqs, kReqs, update_reqs));
    auto ccc_callback = [this](auto cb_svc_id, auto id, const auto& peer_id,
                               bool notify, bool indicate) {
      ccc_callback_count++;
      EXPECT_EQ(last_service_id, cb_svc_id);
      EXPECT_EQ(kChrcId, id);
      last_peer_id = peer_id;
      last_notify = notify;
      last_indicate = indicate;
    };

    last_service_id = mgr.RegisterService(std::move(service), NopReadHandler,
                                          NopWriteHandler, ccc_callback);
    EXPECT_NE(0u, last_service_id);
    EXPECT_EQ(1u, mgr.database()->groupings().size());
  }

  bool ReadCCC(const att::Attribute* attr,
               const std::string& device_id,
               att::ErrorCode* out_ecode,
               uint16_t* out_value) {
    FXL_DCHECK(attr);
    FXL_DCHECK(out_ecode);
    FXL_DCHECK(out_value);

    auto result_cb = [&out_ecode, &out_value](auto cb_code, const auto& value) {
      *out_ecode = cb_code;
      EXPECT_EQ(2u, value.size());

      if (value.size() == 2u) {
        *out_value = le16toh(value.template As<uint16_t>());
      }
    };

    return attr->ReadAsync(device_id, 0u, result_cb);
  }

  bool WriteCCC(const att::Attribute* attr,
                const std::string& device_id,
                uint16_t ccc_value,
                att::ErrorCode* out_ecode) {
    FXL_DCHECK(attr);
    FXL_DCHECK(out_ecode);

    auto result_cb = [&out_ecode](auto cb_code) { *out_ecode = cb_code; };
    uint16_t value = htole16(ccc_value);
    return attr->WriteAsync(
        device_id, 0u, common::BufferView(&value, sizeof(value)), result_cb);
  }
};

TEST_F(GATT_LocalClientCharacteristicConfigurationTest, UpdatePermissions) {
  // Require authentication. This should have no bearing on reads but it should
  // prevent writes.
  const att::AccessRequirements kUpdateReqs(false, true, false);
  constexpr uint8_t kProps = Property::kNotify;
  BuildService(kProps, kUpdateReqs);

  auto* attr = mgr.database()->FindAttribute(kCCCHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(types::kClientCharacteristicConfig, attr->type());

  EXPECT_FALSE(attr->read_reqs().authentication_required());
  EXPECT_TRUE(attr->write_reqs().authentication_required());
}

TEST_F(GATT_LocalClientCharacteristicConfigurationTest,
       IndicationNotSupported) {
  // No security required to enable notifications.
  auto kUpdateReqs = AllowedNoSecurity();
  constexpr uint8_t kProps = Property::kNotify;
  BuildService(kProps, kUpdateReqs);

  auto* attr = mgr.database()->FindAttribute(kCCCHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(types::kClientCharacteristicConfig, attr->type());

  // Enabling indications should fail as the characteristic only supports
  // notifications.
  att::ErrorCode ecode;
  EXPECT_TRUE(WriteCCC(attr, kTestDeviceId, kEnableInd, &ecode));
  EXPECT_EQ(att::ErrorCode::kWriteNotPermitted, ecode);

  uint16_t ccc_value;

  // Notifications and indications for this device should remain disabled.
  EXPECT_TRUE(ReadCCC(attr, kTestDeviceId, &ecode, &ccc_value));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(0x0000, ccc_value);
}

TEST_F(GATT_LocalClientCharacteristicConfigurationTest,
       NotificationNotSupported) {
  // No security.
  auto kUpdateReqs = AllowedNoSecurity();
  constexpr uint8_t kProps = Property::kIndicate;
  BuildService(kProps, kUpdateReqs);

  auto* attr = mgr.database()->FindAttribute(kCCCHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(types::kClientCharacteristicConfig, attr->type());

  // Enabling notifications should fail as the characteristic only supports
  // indications.
  att::ErrorCode ecode;
  EXPECT_TRUE(WriteCCC(attr, kTestDeviceId, kEnableNot, &ecode));
  EXPECT_EQ(att::ErrorCode::kWriteNotPermitted, ecode);

  uint16_t ccc_value;

  // Notifications and indications for this device should remain disabled.
  EXPECT_TRUE(ReadCCC(attr, kTestDeviceId, &ecode, &ccc_value));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(0x0000, ccc_value);
}

TEST_F(GATT_LocalClientCharacteristicConfigurationTest, EnableNotify) {
  // No security.
  auto kUpdateReqs = AllowedNoSecurity();
  constexpr uint8_t kProps = Property::kNotify;
  BuildService(kProps, kUpdateReqs);

  auto* attr = mgr.database()->FindAttribute(kCCCHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(types::kClientCharacteristicConfig, attr->type());

  LocalServiceManager::ClientCharacteristicConfig config;
  EXPECT_FALSE(mgr.GetCharacteristicConfig(last_service_id, kChrcId,
                                           kTestDeviceId, &config));

  att::ErrorCode ecode;
  EXPECT_TRUE(WriteCCC(attr, kTestDeviceId, kEnableNot, &ecode));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);

  uint16_t ccc_value;

  // Notifications should be enabled for kTestDeviceId.
  EXPECT_TRUE(ReadCCC(attr, kTestDeviceId, &ecode, &ccc_value));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(kEnableNot, ccc_value);

  EXPECT_TRUE(mgr.GetCharacteristicConfig(last_service_id, kChrcId,
                                          kTestDeviceId, &config));
  EXPECT_EQ(kChrcHandle, config.handle);
  EXPECT_TRUE(config.notify);
  EXPECT_FALSE(config.indicate);

  // ..but not for kTestDeviceId2.
  EXPECT_TRUE(ReadCCC(attr, kTestDeviceId2, &ecode, &ccc_value));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(0x0000, ccc_value);

  // A set configurations now exists for |kChrcId| but kTestDeviceId2 should
  // appear as unsubscribed.
  EXPECT_TRUE(mgr.GetCharacteristicConfig(last_service_id, kChrcId,
                                          kTestDeviceId2, &config));
  EXPECT_EQ(kChrcHandle, config.handle);
  EXPECT_FALSE(config.notify);
  EXPECT_FALSE(config.indicate);

  // The callback should have been notified.
  EXPECT_EQ(1, ccc_callback_count);
  EXPECT_EQ(kTestDeviceId, last_peer_id);
  EXPECT_TRUE(last_notify);
  EXPECT_FALSE(last_indicate);

  // Enable notifications again. The write should succeed but the callback
  // should not get called as the value will remain unchanged.
  EXPECT_TRUE(WriteCCC(attr, kTestDeviceId, kEnableNot, &ecode));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(1, ccc_callback_count);
}

TEST_F(GATT_LocalClientCharacteristicConfigurationTest, EnableIndicate) {
  // No security.
  auto kUpdateReqs = AllowedNoSecurity();
  constexpr uint8_t kProps = Property::kIndicate;
  BuildService(kProps, kUpdateReqs);

  auto* attr = mgr.database()->FindAttribute(kCCCHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(types::kClientCharacteristicConfig, attr->type());

  att::ErrorCode ecode;
  EXPECT_TRUE(WriteCCC(attr, kTestDeviceId, kEnableInd, &ecode));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);

  uint16_t ccc_value;

  // Indications should be enabled for kTestDeviceId.
  EXPECT_TRUE(ReadCCC(attr, kTestDeviceId, &ecode, &ccc_value));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(kEnableInd, ccc_value);

  LocalServiceManager::ClientCharacteristicConfig config;
  EXPECT_TRUE(mgr.GetCharacteristicConfig(last_service_id, kChrcId,
                                          kTestDeviceId, &config));
  EXPECT_EQ(kChrcHandle, config.handle);
  EXPECT_FALSE(config.notify);
  EXPECT_TRUE(config.indicate);

  // ..but not for kTestDeviceId2.
  EXPECT_TRUE(ReadCCC(attr, kTestDeviceId2, &ecode, &ccc_value));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(0x0000, ccc_value);

  // A set configurations now exists for |kChrcId| but kTestDeviceId2 should
  // appear as unsubscribed.
  EXPECT_TRUE(mgr.GetCharacteristicConfig(last_service_id, kChrcId,
                                          kTestDeviceId2, &config));
  EXPECT_EQ(kChrcHandle, config.handle);
  EXPECT_FALSE(config.notify);
  EXPECT_FALSE(config.indicate);

  // The callback should have been notified.
  EXPECT_EQ(1, ccc_callback_count);
  EXPECT_EQ(kTestDeviceId, last_peer_id);
  EXPECT_FALSE(last_notify);
  EXPECT_TRUE(last_indicate);

  // Enable indications again. The write should succeed but the callback
  // should not get called as the value will remain unchanged.
  EXPECT_TRUE(WriteCCC(attr, kTestDeviceId, kEnableInd, &ecode));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(1, ccc_callback_count);
}

TEST_F(GATT_LocalClientCharacteristicConfigurationTest, DisconnectCleanup) {
  // No security.
  auto kUpdateReqs = AllowedNoSecurity();
  constexpr uint8_t kProps = Property::kNotify;
  BuildService(kProps, kUpdateReqs);

  auto* attr = mgr.database()->FindAttribute(kCCCHandle);
  ASSERT_TRUE(attr);
  EXPECT_EQ(types::kClientCharacteristicConfig, attr->type());

  LocalServiceManager::ClientCharacteristicConfig config;
  EXPECT_FALSE(mgr.GetCharacteristicConfig(last_service_id, kChrcId,
                                           kTestDeviceId, &config));

  att::ErrorCode ecode;
  EXPECT_TRUE(WriteCCC(attr, kTestDeviceId, kEnableNot, &ecode));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);

  // The callback should have been notified.
  EXPECT_EQ(1, ccc_callback_count);
  EXPECT_EQ(kTestDeviceId, last_peer_id);
  EXPECT_TRUE(last_notify);
  EXPECT_FALSE(last_indicate);

  mgr.DisconnectClient(kTestDeviceId);

  uint16_t ccc_value;
  // Reads should succeed but notifications should be disabled.
  EXPECT_TRUE(ReadCCC(attr, kTestDeviceId, &ecode, &ccc_value));
  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(0x0000, ccc_value);

  // The callback should have been called again to disable notifications.
  EXPECT_EQ(2, ccc_callback_count);
  EXPECT_EQ(kTestDeviceId, last_peer_id);
  EXPECT_FALSE(last_notify);
  EXPECT_FALSE(last_indicate);

  mgr.DisconnectClient(kTestDeviceId2);

  // The callback should not be called if a device isn't registered for
  // notifications.
  EXPECT_EQ(2, ccc_callback_count);
  EXPECT_EQ(kTestDeviceId, last_peer_id);
}

}  // namespace
}  // namespace gatt
}  // namespace btlib
