// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gatt/server.h"

#include "garnet/drivers/bluetooth/lib/att/database.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace gatt {
namespace {

constexpr char kTestDeviceId[] = "11223344-1122-1122-1122-112233445566";
constexpr common::UUID kTestType16((uint16_t)0xBEEF);
constexpr common::UUID kTestType128({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                     13, 14, 15});

const auto kTestValue1 = common::CreateStaticByteBuffer('f', 'o', 'o');
const auto kTestValue2 = common::CreateStaticByteBuffer('b', 'a', 'r');
const auto kTestValue3 = common::CreateStaticByteBuffer('b', 'a', 'z');
const auto kTestValue4 = common::CreateStaticByteBuffer('l', 'o', 'l');

const auto kTestValueLong = common::CreateStaticByteBuffer('l', 'o', 'n', 'g');

inline att::AccessRequirements AllowedNoSecurity() {
  return att::AccessRequirements(false, false, false);
}

class GATT_ServerTest : public l2cap::testing::FakeChannelTest {
 public:
  GATT_ServerTest() = default;
  ~GATT_ServerTest() override = default;

 protected:
  void SetUp() override {
    db_ = att::Database::Create();

    ChannelOptions options(l2cap::kATTChannelId);
    auto fake_chan = CreateFakeChannel(options);
    att_ = att::Bearer::Create(std::move(fake_chan));
    server_ = std::make_unique<Server>(kTestDeviceId, db_, att_);
  }

  void TearDown() override {
    server_ = nullptr;
    att_ = nullptr;
    db_ = nullptr;
  }

  Server* server() const { return server_.get(); }

  att::Database* db() const { return db_.get(); }

  // TODO(armansito): Consider introducing a FakeBearer for testing (NET-318).
  att::Bearer* att() const { return att_.get(); }

 private:
  fxl::RefPtr<att::Database> db_;
  fxl::RefPtr<att::Bearer> att_;
  std::unique_ptr<Server> server_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GATT_ServerTest);
};

TEST_F(GATT_ServerTest, ExchangeMTURequestInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x02);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x02,        // request: exchange MTU
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, ExchangeMTURequestValueTooSmall) {
  const uint16_t kServerMTU = l2cap::kDefaultMTU;
  constexpr uint16_t kClientMTU = 1;

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
    0x02,             // opcode: exchange MTU
    kClientMTU, 0x00  // client rx mtu: |kClientMTU|
  );

  const auto kExpected = common::CreateStaticByteBuffer(
    0x03,       // opcode: exchange MTU response
    0xA0, 0x02  // server rx mtu: |kServerMTU|
  );
  // clang-format on

  ASSERT_EQ(kServerMTU, att()->preferred_mtu());

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));

  // Should default to kLEMinMTU since kClientMTU is too small.
  EXPECT_EQ(att::kLEMinMTU, att()->mtu());
}

TEST_F(GATT_ServerTest, ExchangeMTURequest) {
  constexpr uint16_t kServerMTU = l2cap::kDefaultMTU;
  constexpr uint16_t kClientMTU = 0x64;

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
    0x02,             // opcode: exchange MTU
    kClientMTU, 0x00  // client rx mtu: |kClientMTU|
  );

  const auto kExpected = common::CreateStaticByteBuffer(
    0x03,       // opcode: exchange MTU response
    0xA0, 0x02  // server rx mtu: |kServerMTU|
  );
  // clang-format on

  ASSERT_EQ(kServerMTU, att()->preferred_mtu());

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));

  EXPECT_EQ(kClientMTU, att()->mtu());
}

TEST_F(GATT_ServerTest, FindInformationInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x04);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x04,        // request: find information
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, FindInformationInvalidHandle) {
  // Start handle is 0
  // clang-format off
  const auto kInvalidStartHandle = common::CreateStaticByteBuffer(
      0x04,        // opcode: find information
      0x00, 0x00,  // start: 0x0000
      0xFF, 0xFF   // end: 0xFFFF
  );

  const auto kExpected1 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x04,        // request: find information
      0x00, 0x00,  // handle: 0x0000 (start handle in request)
      0x01         // error: Invalid handle
  );

  // End handle is smaller than start handle
  const auto kInvalidEndHandle = common::CreateStaticByteBuffer(
      0x04,        // opcode: find information
      0x02, 0x00,  // start: 0x0002
      0x01, 0x00   // end: 0x0001
  );

  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x04,        // request: find information
      0x02, 0x00,  // handle: 0x0002 (start handle in request)
      0x01         // error: Invalid handle
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidStartHandle, kExpected1));
  EXPECT_TRUE(ReceiveAndExpect(kInvalidEndHandle, kExpected2));
}

TEST_F(GATT_ServerTest, FindInformationAttributeNotFound) {
  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x04,        // opcode: find information request
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF   // end: 0xFFFF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x04,        // request: find information
      0x01, 0x00,  // handle: 0x0001 (start handle in request)
      0x0A         // error: Attribute not found
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindInformation16) {
  auto* grp = db()->NewGrouping(types::kPrimaryService, 2, kTestValue1);
  grp->AddAttribute(kTestType16);
  grp->AddAttribute(kTestType16);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x04,        // opcode: find information request
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF   // end: 0xFFFF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x05,        // opcode: find information response
      0x01,        // format: 16-bit
      0x01, 0x00,  // handle: 0x0001
      0x00, 0x28,  // uuid: primary service group type
      0x02, 0x00,  // handle: 0x0002
      0xEF, 0xBE,  // uuid: 0xBEEF
      0x03, 0x00,  // handle: 0x0003
      0xEF, 0xBE   // uuid: 0xBEEF
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindInformation128) {
  auto* grp = db()->NewGrouping(kTestType128, 0, kTestValue1);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x04,        // opcode: find information request
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF   // end: 0xFFFF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x05,        // opcode: find information response
      0x02,        // format: 128-bit
      0x01, 0x00,  // handle: 0x0001

      // uuid: 0F0E0D0C-0B0A-0908-0706-050403020100
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F);
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindByTypeValueSuccess) {
  // handle: 1 (active)
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // handle: 2 (active)
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue2)->set_active(true);

  // handle: 3 (active)
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x06,          // opcode: find by type value request
      0x01, 0x00,    // start: 0x0001
      0xFF, 0xFF,    // end: 0xFFFF
      0x00, 0x28,    // uuid: primary service group type
      'f', 'o', 'o'  // value: foo
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x07,        // opcode: find by type value response
      0x01, 0x00,  // handle: 0x0001
      0x01, 0x00,  // group handle: 0x0001
      0x03, 0x00,  // handle: 0x0003
      0x03, 0x00   // group handle: 0x0003
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindByTypeValueFail) {
  // handle: 1 (active)
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x06,          // opcode: find by type value request
      0x01, 0x00,    // start: 0x0001
      0xFF, 0xFF,    // end: 0xFFFF
      0x00, 0x28,    // uuid: primary service group type
      'n', 'o'       // value: no
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,          // Error
      0x06,          // opcode: find by type value
      0x00, 0x00,    // group handle: 0x0000
      0x0a           // Attribute Not Found
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindByTypeValueEmptyDB) {
  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x06,          // opcode: find by type value request
      0x01, 0x00,    // start: 0x0001
      0xFF, 0xFF,    // end: 0xFFFF
      0x00, 0x28,    // uuid: primary service group type
      'n', 'o'       // value: no
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,          // Error
      0x06,          // opcode: find by type value
      0x00, 0x00,    // group handle: 0x0000
      0x0a           // Attribute Not Found
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindByTypeValueInvalidHandle) {
  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x06,          // opcode: find by type value request
      0x02, 0x00,    // start: 0x0002
      0x01, 0x00,    // end: 0x0001
      0x00, 0x28,    // uuid: primary service group type
      'n', 'o'       // value: no
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,          // Error
      0x06,          // opcode: find by type value
      0x00, 0x00,    // group handle: 0x0000
      0x01           // Invalid Handle
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindByTypeValueInvalidPDUError) {
  // handle: 1 (active)
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x06);

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,          // Error
      0x06,          // opcode: find by type value
      0x00, 0x00,    // group handle: 0x0000
      0x04           // Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, FindByTypeValueZeroLengthValueError) {
  // handle: 1 (active)
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x06,          // opcode: find by type value request
      0x01, 0x00,    // start: 0x0001
      0xFF, 0xFF,    // end: 0xFFFF
      0x00, 0x28     // uuid: primary service group type
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,          // Error
      0x06,          // opcode: find by type value
      0x00, 0x00,    // group handle: 0x0000
      0x0a           // Attribute Not Found
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindByTypeValueOutsideRangeError) {
  // handle: 1 (active)
  auto* grp = db()->NewGrouping(kTestType16, 2, kTestValue2);

  // handle: 2 - value: "long"
  grp->AddAttribute(kTestType16, AllowedNoSecurity())->SetValue(kTestValue2);

  // handle: 3 - value: "foo"
  grp->AddAttribute(kTestType16, AllowedNoSecurity())->SetValue(kTestValue1);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x06,          // opcode: find by type value request
      0x01, 0x00,    // start: 0x0001
      0x02, 0x00,    // end: 0xFFFF
      0x00, 0x28,    // uuid: primary service group type
      'f', 'o', 'o'  // value: foo
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,          // Error
      0x06,          // opcode: find by type value
      0x00, 0x00,    // group handle: 0x0000
      0x0a           // Attribute Not Found
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindInfomationInactive) {
  // handle: 1 (active)
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // handle: 2, 3, 4 (inactive)
  auto* grp = db()->NewGrouping(types::kPrimaryService, 2, kTestValue1);
  grp->AddAttribute(kTestType16);
  grp->AddAttribute(kTestType16);

  // handle: 5 (active)
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x04,        // opcode: find information request
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF   // end: 0xFFFF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x05,        // opcode: find information response
      0x01,        // format: 16-bit
      0x01, 0x00,  // handle: 0x0001
      0x00, 0x28,  // uuid: primary service group type
      0x05, 0x00,  // handle: 0x0005
      0x00, 0x28  // uuid: primary service group type
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindInfomationRange) {
  auto* grp = db()->NewGrouping(types::kPrimaryService, 2, kTestValue1);
  grp->AddAttribute(kTestType16);
  grp->AddAttribute(kTestType16);
  grp->set_active(true);

  // handle: 5 (active)
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x04,        // opcode: find information request
      0x02, 0x00,  // start: 0x0002
      0x02, 0x00   // end: 0x0002
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x05,        // opcode: find information response
      0x01,        // format: 16-bit
      0x02, 0x00,  // handle: 0x0001
      0xEF, 0xBE   // uuid: 0xBEEF
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x10);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeUnsupportedGroupType) {
  // 16-bit UUID
  // clang-format off
  const auto kUsing16BitType = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x01, 0x00   // group type: 1 (unsupported)
  );

  // 128-bit UUID
  const auto kUsing128BitType = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF

      // group type: 00112233-4455-6677-8899-AABBCCDDEEFF (unsupported)
      0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
      0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00);

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x01, 0x00,  // handle: 0x0001 (start handle in request)
      0x10         // error: Unsupported Group Type
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kUsing16BitType, kExpected));
  EXPECT_TRUE(ReceiveAndExpect(kUsing128BitType, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeInvalidHandle) {
  // Start handle is 0
  // clang-format off
  const auto kInvalidStartHandle = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x00, 0x00,  // start: 0x0000
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected1 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x00, 0x00,  // handle: 0x0000 (start handle in request)
      0x01         // error: Invalid handle
  );

  // End handle is smaller than start handle
  const auto kInvalidEndHandle = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x02, 0x00,  // start: 0x0002
      0x01, 0x00,  // end: 0x0001
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x02, 0x00,  // handle: 0x0002 (start handle in request)
      0x01         // error: Invalid handle
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidStartHandle, kExpected1));
  EXPECT_TRUE(ReceiveAndExpect(kInvalidEndHandle, kExpected2));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeAttributeNotFound) {
  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x01, 0x00,  // handle: 0x0001 (start handle in request)
      0x0A         // error: Attribute not found
  );
  // clang-format on

  // Database is empty.
  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));

  // Group type does not match.
  db()->NewGrouping(types::kSecondaryService, 0, kTestValue1)->set_active(true);
  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeSingle) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  // Start: 1, end: 2
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  grp->AddAttribute(common::UUID(), att::AccessRequirements(),
                    att::AccessRequirements());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x11,               // opcode: read by group type response
      0x08,               // length: 8 (strlen("test") + 4)
      0x01, 0x00,         // start: 0x0001
      0x02, 0x00,         // end: 0x0002
      't', 'e', 's', 't'  // value: "test"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeSingle128) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  // Start: 1, end: 2
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  grp->AddAttribute(common::UUID(), att::AccessRequirements(),
                    att::AccessRequirements());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF

      // group type: 00002800-0000-1000-8000-00805F9B34FB (primary service)
      0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
      0x00, 0x10, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00);

  const auto kExpected = common::CreateStaticByteBuffer(
      0x11,               // opcode: read by group type response
      0x08,               // length: 8 (strlen("test") + 4)
      0x01, 0x00,         // start: 0x0001
      0x02, 0x00,         // end: 0x0002
      't', 'e', 's', 't'  // value: "test"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeSingleTruncated) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  // Start: 1, end: 1
  auto* grp = db()->NewGrouping(types::kPrimaryService, 0, kTestValue);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x11,        // opcode: read by group type response
      0x06,        // length: 6 (strlen("te") + 4)
      0x01, 0x00,  // start: 0x0001
      0x01, 0x00,  // end: 0x0001
      't', 'e'     // value: "te"
  );
  // clang-format on

  // Force the MTU to exactly fit |kExpected| which partially contains
  // |kTestValue|.
  att()->set_mtu(kExpected.size());

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeMultipleSameValueSize) {
  // Start: 1, end: 1
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // Start: 2, end: 2
  auto* grp2 = db()->NewGrouping(types::kPrimaryService, 0, kTestValue2);
  grp2->set_active(true);

  // Start: 3, end: 3
  db()->NewGrouping(types::kSecondaryService, 0, kTestValue3)->set_active(true);

  // Start: 4, end: 4
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue3)->set_active(true);

  // Start: 5, end: 5
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue4)->set_active(true);

  // clang-format off
  const auto kRequest1 = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected1 = common::CreateStaticByteBuffer(
      0x11,           // opcode: read by group type response
      0x07,           // length: 7 (strlen("foo") + 4)
      0x01, 0x00,     // start: 0x0001
      0x01, 0x00,     // end: 0x0001
      'f', 'o', 'o',  // value: "foo"
      0x02, 0x00,     // start: 0x0002
      0x02, 0x00,     // end: 0x0002
      'b', 'a', 'r',  // value: "bar"
      0x04, 0x00,     // start: 0x0004
      0x04, 0x00,     // end: 0x0004
      'b', 'a', 'z'   // value: "baz"
  );
  // clang-format on

  // Set the MTU to be one byte too short to include the 5th attribute group.
  // The 3rd group is omitted as its group type does not match.
  att()->set_mtu(kExpected1.size() + 6);

  EXPECT_TRUE(ReceiveAndExpect(kRequest1, kExpected1));

  // Search a narrower range. Only two groups should be returned even with room
  // in MTU.
  // clang-format off
  const auto kRequest2 = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x02, 0x00,  // start: 0x0002
      0x04, 0x00,  // end: 0x0004
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x11,           // opcode: read by group type response
      0x07,           // length: 7 (strlen("foo") + 4)
      0x02, 0x00,     // start: 0x0002
      0x02, 0x00,     // end: 0x0002
      'b', 'a', 'r',  // value: "bar"
      0x04, 0x00,     // start: 0x0004
      0x04, 0x00,     // end: 0x0004
      'b', 'a', 'z'   // value: "baz"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest2, kExpected2));

  // Make the second group inactive. It should get omitted.
  // clang-format off
  const auto kExpected3 = common::CreateStaticByteBuffer(
      0x11,           // opcode: read by group type response
      0x07,           // length: 7 (strlen("foo") + 4)
      0x04, 0x00,     // start: 0x0004
      0x04, 0x00,     // end: 0x0004
      'b', 'a', 'z'   // value: "baz"
  );
  // clang-format on

  grp2->set_active(false);
  EXPECT_TRUE(ReceiveAndExpect(kRequest2, kExpected3));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeMultipleVaryingLengths) {
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // Matching type but value of different size. The results will stop here.
  db()->NewGrouping(types::kPrimaryService, 0, kTestValueLong)
      ->set_active(true);

  // Matching type and matching value length. This won't be included as the
  // request will terminate at the second attribute.
  db()->NewGrouping(types::kPrimaryService, 0, kTestValue1)->set_active(true);

  // clang-format off
  const auto kRequest2 = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x11,               // opcode: read by group type response
      0x08,               // length: 8 (strlen("long") + 4)
      0x01, 0x00,         // start: 0x0001
      0x01, 0x00,         // end: 0x0001
      'l', 'o', 'n', 'g'  // value: "bar"
  );
  // clang-format on
}

TEST_F(GATT_ServerTest, ReadByTypeInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x08);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeInvalidHandle) {
  // Start handle is 0
  // clang-format off
  const auto kInvalidStartHandle = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x00, 0x00,  // start: 0x0000
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected1 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x00, 0x00,  // handle: 0x0000 (start handle in request)
      0x01         // error: Invalid handle
  );

  // End handle is smaller than start handle
  const auto kInvalidEndHandle = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x02, 0x00,  // start: 0x0002
      0x01, 0x00,  // end: 0x0001
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x02, 0x00,  // handle: 0x0002 (start handle in request)
      0x01         // error: Invalid handle
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidStartHandle, kExpected1));
  EXPECT_TRUE(ReceiveAndExpect(kInvalidEndHandle, kExpected2));
}

TEST_F(GATT_ServerTest, ReadByTypeAttributeNotFound) {
  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x01, 0x00,  // handle: 0x0001 (start handle in request)
      0x0A         // error: Attribute not found
  );
  // clang-format on

  // Database is empty.
  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));

  // Attribute type does not match.
  db()->NewGrouping(types::kSecondaryService, 0, kTestValue1)->set_active(true);
  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeDynamicValueNoHandler) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                    att::AccessRequirements());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x02, 0x00,  // handle: 0x0002 (the attribute causing the error)
      0x02         // error: Read not permitted
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeDynamicValue) {
  auto* grp = db()->NewGrouping(types::kPrimaryService, 2, kTestValue1);
  auto* attr = grp->AddAttribute(kTestType16, AllowedNoSecurity());
  attr->set_read_handler([attr](const auto& peer_id, auto handle,
                                uint16_t offset, const auto& result_cb) {
    EXPECT_EQ(attr->handle(), handle);
    EXPECT_EQ(0u, offset);
    result_cb(att::ErrorCode::kNoError,
              common::CreateStaticByteBuffer('f', 'o', 'r', 'k'));
  });

  // Add a second dynamic attribute, which should be omitted.
  attr = grp->AddAttribute(kTestType16, AllowedNoSecurity());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,               // opcode: read by type response
      0x06,               // length: 6 (strlen("fork") + 2)
      0x02, 0x00,         // handle: 0x0002
      'f', 'o', 'r', 'k'  // value: "fork"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));

  // Assign a static value to the second attribute. It should still be omitted
  // as the first attribute is dynamic.
  attr->SetValue(kTestValue1);
  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeDynamicValueError) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                                 att::AccessRequirements());
  attr->set_read_handler([](const auto& peer_id, auto handle, uint16_t offset,
                            const auto& result_cb) {
    result_cb(att::ErrorCode::kUnlikelyError, common::BufferView());
  });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x02, 0x00,  // handle: 0x0002 (the attribute causing the error)
      0x0E         // error: Unlikely error
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeSingle) {
  const auto kTestValue1 = common::CreateStaticByteBuffer('f', 'o', 'o');
  const auto kTestValue2 = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue1);
  grp->AddAttribute(kTestType16, AllowedNoSecurity(), att::AccessRequirements())
      ->SetValue(kTestValue2);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,               // opcode: read by type response
      0x06,               // length: 6 (strlen("test") + 2)
      0x02, 0x00,         // handle: 0x0002
      't', 'e', 's', 't'  // value: "test"
  );

  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeSingle128) {
  const auto kTestValue1 = common::CreateStaticByteBuffer('f', 'o', 'o');
  const auto kTestValue2 = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue1);
  grp->AddAttribute(kTestType128, AllowedNoSecurity(),
                    att::AccessRequirements())
      ->SetValue(kTestValue2);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF

      // type: 0F0E0D0C-0B0A-0908-0706-050403020100
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F);

  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,               // opcode: read by type response
      0x06,               // length: 6 (strlen("test") + 2)
      0x02, 0x00,         // handle: 0x0002
      't', 'e', 's', 't'  // value: "test"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeSingleTruncated) {
  const auto kVeryLongValue = common::CreateStaticByteBuffer(
      't', 'e', 's', 't', 'i', 'n', 'g', ' ', 'i', 's', ' ', 'f', 'u', 'n');

  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue1);
  grp->AddAttribute(kTestType16, AllowedNoSecurity(), att::AccessRequirements())
      ->SetValue(kVeryLongValue);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,          // opcode: read by type response
      0x05,          // length: 5 (strlen("tes") + 2)
      0x02, 0x00,    // handle: 0x0002
      't', 'e', 's'  // value: "tes"
  );
  // clang-format on

  // Force the MTU to exactly fit |kExpected| which partially contains
  // |kTestValue2| (the packet is crafted so that both |kRequest| and
  // |kExpected| fit within the MTU).
  att()->set_mtu(kExpected.size());

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeErrorSecurity) {
  auto* grp = db()->NewGrouping(types::kPrimaryService, 4, kTestValue1);

  // Reads not allowed (handle: 2)
  grp->AddAttribute(kTestType16)->SetValue(kTestValue2);

  // Requires encryption (handle: 3)
  grp->AddAttribute(kTestType16, att::AccessRequirements(true, false, false))
      ->SetValue(kTestValue2);

  // Requires authentication (handle: 4)
  grp->AddAttribute(kTestType16, att::AccessRequirements(false, true, false))
      ->SetValue(kTestValue2);

  // Requires authorization (handle: 5)
  grp->AddAttribute(kTestType16, att::AccessRequirements(false, false, true))
      ->SetValue(kTestValue2);

  grp->set_active(true);

  // clang-format off
  const auto kRequest1 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x02, 0x00,  // start: 0x0002
      0x02, 0x00,  // end: 0x0002
      0xEF, 0xBE   // type: 0xBEEF
  );
  const auto kRequest2 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x03, 0x00,  // start: 0x0003
      0x03, 0x00,  // end: 0x0003
      0xEF, 0xBE   // type: 0xBEEF
  );
  const auto kRequest3 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x04, 0x00,  // start: 0x0004
      0x04, 0x00,  // end: 0x0004
      0xEF, 0xBE   // type: 0xBEEF
  );
  const auto kRequest4 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x05, 0x00,  // start: 0x0005
      0x05, 0x00,  // end: 0x0005
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected1 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x02, 0x00,  // handle: 0x0002
      0x02         // error: read not permitted
  );
  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x03, 0x00,  // handle: 0x0003
      0x0F         // error: insuff. encryption
  );
  const auto kExpected3 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x04, 0x00,  // handle: 0x0004
      0x05         // error: insuff. authentication
  );
  const auto kExpected4 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x05, 0x00,  // handle: 0x0005
      0x08         // error: insuff. authorization
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest1, kExpected1));
  EXPECT_TRUE(ReceiveAndExpect(kRequest2, kExpected2));
  EXPECT_TRUE(ReceiveAndExpect(kRequest3, kExpected3));
  EXPECT_TRUE(ReceiveAndExpect(kRequest4, kExpected4));
}

// When there are more than one matching attributes, the list should end at the
// first attribute that causes an error.
TEST_F(GATT_ServerTest, ReadByTypeMultipleExcludeFirstError) {
  // handle 1: readable
  auto* grp = db()->NewGrouping(kTestType16, 1, kTestValue1);

  // handle 2: not readable.
  grp->AddAttribute(kTestType16)->SetValue(kTestValue1);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,          // opcode: read by type response
      0x05,          // length: 5 (strlen("foo") + 2)
      0x01, 0x00,    // handle: 0x0001
      'f', 'o', 'o'  // value: "foo"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeMultipleSameValueSize) {
  // handle: 1, value: foo
  auto* grp = db()->NewGrouping(types::kPrimaryService, 2, kTestValue1);

  // handle: 2, value: foo
  grp->AddAttribute(kTestType16, AllowedNoSecurity(), att::AccessRequirements())
      ->SetValue(kTestValue1);

  // handle: 3, value: bar
  grp->AddAttribute(kTestType16, AllowedNoSecurity(), att::AccessRequirements())
      ->SetValue(kTestValue2);
  grp->set_active(true);

  // handle: 4, value: foo (new grouping)
  grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue1);

  // handle: 5, value: baz
  grp->AddAttribute(kTestType16, AllowedNoSecurity(), att::AccessRequirements())
      ->SetValue(kTestValue3);
  grp->set_active(true);

  // clang-format off
  const auto kRequest1 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected1 = common::CreateStaticByteBuffer(
      0x09,           // opcode: read by type response
      0x05,           // length: 5 (strlen("foo") + 2)
      0x02, 0x00,     // handle: 0x0002
      'f', 'o', 'o',  // value: "foo"
      0x03, 0x00,     // handle: 0x0003
      'b', 'a', 'r',  // value: "bar"
      0x05, 0x00,     // handle: 0x0005
      'b', 'a', 'z'   // value: "baz"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest1, kExpected1));

  // Set the MTU 1 byte too short for |kExpected1|.
  att()->set_mtu(kExpected1.size() - 1);

  // clang-format off
  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x09,           // opcode: read by type response
      0x05,           // length: 5 (strlen("foo") + 2)
      0x02, 0x00,     // handle: 0x0002
      'f', 'o', 'o',  // value: "foo"
      0x03, 0x00,     // handle: 0x0003
      'b', 'a', 'r'   // value: "bar"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest1, kExpected2));

  // Try a different range.
  // clang-format off
  const auto kRequest2 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x03, 0x00,  // start: 0x0003
      0x05, 0x00,  // end: 0x0005
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected3 = common::CreateStaticByteBuffer(
      0x09,           // opcode: read by type response
      0x05,           // length: 5 (strlen("bar") + 2)
      0x03, 0x00,     // handle: 0x0003
      'b', 'a', 'r',  // value: "bar"
      0x05, 0x00,     // handle: 0x0005
      'b', 'a', 'z'   // value: "baz"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest2, kExpected3));

  // Make the second group inactive.
  grp->set_active(false);

  // clang-format off
  const auto kExpected4 = common::CreateStaticByteBuffer(
      0x09,           // opcode: read by type response
      0x05,           // length: 5 (strlen("bar") + 2)
      0x03, 0x00,     // handle: 0x0003
      'b', 'a', 'r'   // value: "bar"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest2, kExpected4));
}

// A response packet should only include consecutive attributes with the same
// value size.
TEST_F(GATT_ServerTest, ReadByTypeMultipleVaryingLengths) {
  // handle: 1 - value: "foo"
  auto* grp = db()->NewGrouping(kTestType16, 2, kTestValue1);

  // handle: 2 - value: "long"
  grp->AddAttribute(kTestType16, AllowedNoSecurity())->SetValue(kTestValueLong);

  // handle: 3 - value: "foo"
  grp->AddAttribute(kTestType16, AllowedNoSecurity())->SetValue(kTestValue1);
  grp->set_active(true);

  // Even though we have 3 attributes with a matching type, the requests below
  // will always return one attribute at a time as their values have different
  // sizes.

  // clang-format off
  const auto kRequest1 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );
  const auto kExpected1 = common::CreateStaticByteBuffer(
      0x09,          // opcode: read by type response
      0x05,          // length: 5 (strlen("foo") + 2)
      0x01, 0x00,    // handle: 0x0001
      'f', 'o', 'o'  // value: "foo"
  );
  const auto kRequest2 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x02, 0x00,  // start: 0x0002
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );
  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x09,               // opcode: read by type response
      0x06,               // length: 6 (strlen("long") + 2)
      0x02, 0x00,         // handle: 0x0002
      'l', 'o', 'n', 'g'  // value: "long"
  );
  const auto kRequest3 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x03, 0x00,  // start: 0x0003
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );
  const auto kExpected3 = common::CreateStaticByteBuffer(
      0x09,          // opcode: read by type response
      0x05,          // length: 5 (strlen("foo") + 2)
      0x03, 0x00,    // handle: 0x0003
      'f', 'o', 'o'  // value: "foo"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest1, kExpected1));
  EXPECT_TRUE(ReceiveAndExpect(kRequest2, kExpected2));
  EXPECT_TRUE(ReceiveAndExpect(kRequest3, kExpected3));
}

// When there are more than one matching attributes, the list should end at the
// first attribute with a dynamic value.
TEST_F(GATT_ServerTest, ReadByTypeMultipleExcludeFirstDynamic) {
  // handle: 1 - value: "foo"
  auto* grp = db()->NewGrouping(kTestType16, 1, kTestValue1);

  // handle: 2 - value: dynamic
  grp->AddAttribute(kTestType16, AllowedNoSecurity());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,          // opcode: read by type response
      0x05,          // length: 5 (strlen("foo") + 2)
      0x01, 0x00,    // handle: 0x0001
      'f', 'o', 'o'  // value: "foo"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, WriteRequestInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x12);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x12,        // request: write request
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, WriteRequestInvalidHandle) {
  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x12,        // opcode: write request
      0x01, 0x00,  // handle: 0x0001

      // value: "test"
      't', 'e', 's', 't');

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x12,        // request: write request
      0x01, 0x00,  // handle: 0x0001
      0x01         // error: invalid handle
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, WriteRequestSecurity) {
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);

  // Requires encryption
  grp->AddAttribute(kTestType16, att::AccessRequirements(),
                    att::AccessRequirements(true, false, false));
  grp->set_active(true);

  // We send two write requests:
  //   1. 0x0001: not writable
  //   2. 0x0002: writable but requires encryption
  //
  // clang-format off
  const auto kRequest1 = common::CreateStaticByteBuffer(
      0x12,        // opcode: write request
      0x01, 0x00,  // handle: 0x0001

      // value: "test"
      't', 'e', 's', 't');

  const auto kExpected1 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x12,        // request: write request
      0x01, 0x00,  // handle: 0x0001
      0x03         // error: write not permitted
  );
  const auto kRequest2 = common::CreateStaticByteBuffer(
      0x12,        // opcode: write request
      0x02, 0x00,  // handle: 0x0002

      // value: "test"
      't', 'e', 's', 't');

  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x12,        // request: write request
      0x02, 0x00,  // handle: 0x0002
      0x03         // error: write not permitted
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest1, kExpected1));
  EXPECT_TRUE(ReceiveAndExpect(kRequest2, kExpected2));
}

TEST_F(GATT_ServerTest, WriteRequestNoHandler) {
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);

  grp->AddAttribute(kTestType16, att::AccessRequirements(),
                    AllowedNoSecurity());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x12,        // opcode: write request
      0x02, 0x00,  // handle: 0x0002

      // value: "test"
      't', 'e', 's', 't');

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x12,        // request: write request
      0x02, 0x00,  // handle: 0x0002
      0x03         // error: write not permitted
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, WriteRequestError) {
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, att::AccessRequirements(),
                                 AllowedNoSecurity());

  attr->set_write_handler([&](const auto& peer_id, att::Handle handle,
                              uint16_t offset, const auto& value,
                              const auto& result_cb) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(attr->handle(), handle);
    EXPECT_EQ(0u, offset);
    EXPECT_TRUE(common::ContainersEqual(
        common::CreateStaticByteBuffer('t', 'e', 's', 't'), value));

    result_cb(att::ErrorCode::kUnlikelyError);
  });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x12,        // opcode: write request
      0x02, 0x00,  // handle: 0x0002

      // value: "test"
      't', 'e', 's', 't');

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x12,        // request: write request
      0x02, 0x00,  // handle: 0x0002
      0x0E         // error: unlikely error
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, WriteRequestSuccess) {
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, att::AccessRequirements(),
                                 AllowedNoSecurity());

  attr->set_write_handler([&](const auto& peer_id, att::Handle handle,
                              uint16_t offset, const auto& value,
                              const auto& result_cb) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(attr->handle(), handle);
    EXPECT_EQ(0u, offset);
    EXPECT_TRUE(common::ContainersEqual(
        common::CreateStaticByteBuffer('t', 'e', 's', 't'), value));

    result_cb(att::ErrorCode::kNoError);
  });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x12,        // opcode: write request
      0x02, 0x00,  // handle: 0x0002

      // value: "test"
      't', 'e', 's', 't');
  // clang-format on

  // opcode: write response
  const auto kExpected = common::CreateStaticByteBuffer(0x13);

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

// TODO(bwb): Add test cases for the error conditions involved in a Write
// Command (NET-387)

TEST_F(GATT_ServerTest, WriteCommandSuccess) {
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, att::AccessRequirements(),
                                 AllowedNoSecurity());

  attr->set_write_handler([&](const auto& peer_id, att::Handle handle,
                              uint16_t offset, const auto& value,
                              const auto& result_cb) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(attr->handle(), handle);
    EXPECT_EQ(0u, offset);
    EXPECT_TRUE(common::ContainersEqual(
        common::CreateStaticByteBuffer('t', 'e', 's', 't'), value));
    // validated side effect, exit loop
    fsl::MessageLoop::GetCurrent()->QuitNow();
  });
  grp->set_active(true);

  // clang-format off
  const auto kCmd = common::CreateStaticByteBuffer(
      0x52,        // opcode: write command
      0x02, 0x00,  // handle: 0x0002
      't', 'e', 's', 't');
  // clang-format on

  fake_chan()->Receive(kCmd);
  RunMessageLoop();
}

TEST_F(GATT_ServerTest, ReadRequestInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x0A);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x0A,        // request: read request
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, ReadRequestInvalidHandle) {
  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0A,       // opcode: read request
      0x01, 0x00  // handle: 0x0001
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x0A,        // request: read request
      0x01, 0x00,  // handle: 0x0001
      0x01         // error: invalid handle
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadRequestSecurity) {
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);

  // Requires encryption
  grp->AddAttribute(kTestType16, att::AccessRequirements(true, false, false),
                    att::AccessRequirements());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0A,       // opcode: read request
      0x02, 0x00  // handle: 0x0002
  );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x0A,        // request: read request
      0x02, 0x00,  // handle: 0x0002
      0x02         // error: read not permitted
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadRequestCached) {
  const auto kDeclValue = common::CreateStaticByteBuffer('d', 'e', 'c', 'l');
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kDeclValue);
  auto* attr = grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                                 att::AccessRequirements());
  attr->SetValue(kTestValue);
  grp->set_active(true);

  // clang-format off
  const auto kRequest1 = common::CreateStaticByteBuffer(
      0x0A,       // opcode: read request
      0x01, 0x00  // handle: 0x0001
  );
  const auto kExpected1 = common::CreateStaticByteBuffer(
      0x0B,               // opcode: read response
      'd', 'e', 'c', 'l'  // value: kDeclValue
  );
  const auto kRequest2 = common::CreateStaticByteBuffer(
      0x0A,       // opcode: read request
      0x02, 0x00  // handle: 0x0002
  );
  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x0B,          // opcode: read response
      'f', 'o', 'o'  // value: kTestValue
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest1, kExpected1));
  EXPECT_TRUE(ReceiveAndExpect(kRequest2, kExpected2));
}

TEST_F(GATT_ServerTest, ReadRequestNoHandler) {
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);

  grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                    att::AccessRequirements());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0A,       // opcode: read request
      0x02, 0x00  // handle: 0x0002
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x0A,        // request: read request
      0x02, 0x00,  // handle: 0x0002
      0x02         // error: read not permitted
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadRequestError) {
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                                 att::AccessRequirements());
  attr->set_read_handler([&](const auto& peer_id, att::Handle handle,
                             uint16_t offset, const auto& result_cb) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(attr->handle(), handle);
    EXPECT_EQ(0u, offset);

    result_cb(att::ErrorCode::kUnlikelyError, common::BufferView());
  });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0A,       // opcode: read request
      0x02, 0x00  // handle: 0x0002
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x0A,        // request: read request
      0x02, 0x00,  // handle: 0x0002
      0x0E         // error: unlikely error
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadBlobRequestInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x0C);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x0C,        // request: read blob request
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, ReadBlobRequestDynamicSuccess) {
  const auto kDeclValue = common::CreateStaticByteBuffer('d', 'e', 'c', 'l');
  const auto kTestValue = common::CreateStaticByteBuffer(
      'A', ' ', 'V', 'e', 'r', 'y', ' ', 'L', 'o', 'n', 'g', ' ', 'D', 'e', 'v',
      'i', 'c', 'e', ' ', 'N', 'a', 'm', 'e', ' ', 'U', 's', 'i', 'n', 'g', ' ',
      'A', ' ', 'L', 'o', 'n', 'g', ' ', 'A', 't', 't', 'r', 'i', 'b', 'u', 't',
      'e');

  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                                 att::AccessRequirements());

  attr->set_read_handler([&](const auto& peer_id, att::Handle handle,
                             uint16_t offset, const auto& result_cb) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(attr->handle(), handle);
    EXPECT_EQ(22u, offset);
    result_cb(att::ErrorCode::kNoError,
              common::CreateStaticByteBuffer(
                  'e', ' ', 'U', 's', 'i', 'n', 'g', ' ', 'A', ' ', 'L', 'o',
                  'n', 'g', ' ', 'A', 't', 't', 'r', 'i', 'b', 'u'));
  });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0C,       // opcode: read blob request
      0x02, 0x00, // handle: 0x0002
      0x16, 0x00  // offset: 0x0016
  );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x0D,          // opcode: read blob response
      // Read Request response
      'e', ' ', 'U', 's', 'i', 'n', 'g', ' ', 'A', ' ', 'L',
      'o', 'n', 'g', ' ', 'A', 't', 't', 'r', 'i', 'b', 'u'
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadBlobDynamicRequestError) {
  const auto kTestValue = common::CreateStaticByteBuffer(
      'A', ' ', 'V', 'e', 'r', 'y', ' ', 'L', 'o', 'n', 'g', ' ', 'D', 'e', 'v',
      'i', 'c', 'e', ' ', 'N', 'a', 'm', 'e', ' ', 'U', 's', 'i', 'n', 'g', ' ',
      'A', ' ', 'L', 'o', 'n', 'g', ' ', 'A', 't', 't', 'r', 'i', 'b', 'u', 't',
      'e');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                                 att::AccessRequirements());
  attr->set_read_handler([&](const auto& peer_id, att::Handle handle,
                             uint16_t offset, const auto& result_cb) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(attr->handle(), handle);

    result_cb(att::ErrorCode::kUnlikelyError, common::BufferView());
  });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0C,       // opcode: read blob request
      0x02, 0x00, // handle: 0x0002
      0x16, 0x00  // offset: 0x0016
      );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x0C,        // request: read by type
      0x02, 0x00,  // handle: 0x0002 (the attribute causing the error)
      0x0E         // error: Unlikely error
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadBlobRequestStaticSuccess) {
  const auto kTestValue = common::CreateStaticByteBuffer(
      'A', ' ', 'V', 'e', 'r', 'y', ' ', 'L', 'o', 'n', 'g', ' ', 'D', 'e', 'v',
      'i', 'c', 'e', ' ', 'N', 'a', 'm', 'e', ' ', 'U', 's', 'i', 'n', 'g', ' ',
      'A', ' ', 'L', 'o', 'n', 'g', ' ', 'A', 't', 't', 'r', 'i', 'b', 'u', 't',
      'e');

  auto* grp = db()->NewGrouping(types::kPrimaryService, 0, kTestValue);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0C,       // opcode: read blob request
      0x01, 0x00, // handle: 0x0002
      0x16, 0x00  // offset: 0x0016
  );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x0D,          // opcode: read blob response
      // Read Request response
      'e', ' ', 'U', 's', 'i', 'n', 'g', ' ', 'A', ' ', 'L',
      'o', 'n', 'g', ' ', 'A', 't', 't', 'r', 'i', 'b', 'u'
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadBlobRequestStaticOverflowError) {
  const auto kTestValue = common::CreateStaticByteBuffer(
      's', 'h', 'o', 'r', 't', 'e', 'r');

  auto* grp = db()->NewGrouping(types::kPrimaryService, 0, kTestValue);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0C,       // opcode: read blob request
      0x01, 0x00, // handle: 0x0001
      0x16, 0x10  // offset: 0x1016
  );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,       // Error
      0x0C,       // opcode
      0x01, 0x00, // handle: 0x0001
      0x07        // InvalidOffset
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadBlobRequestInvalidHandleError) {
  const auto kTestValue = common::CreateStaticByteBuffer(
      'A', ' ', 'V', 'e', 'r', 'y', ' ', 'L', 'o', 'n', 'g', ' ', 'D', 'e', 'v',
      'i', 'c', 'e', ' ', 'N', 'a', 'm', 'e', ' ', 'U', 's', 'i', 'n', 'g', ' ',
      'A', ' ', 'L', 'o', 'n', 'g', ' ', 'A', 't', 't', 'r', 'i', 'b', 'u', 't',
      'e');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 0, kTestValue);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0C,       // opcode: read blob request
      0x02, 0x30, // handle: 0x0002
      0x16, 0x00  // offset: 0x0016
      );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x0C,        // request: read blob request
      0x02, 0x30,  // handle: 0x0001
      0x01         // error: invalid handle
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadBlobRequestNotPermitedError) {
  const auto kTestValue = common::CreateStaticByteBuffer(
      'A', ' ', 'V', 'e', 'r', 'y', ' ', 'L', 'o', 'n', 'g', ' ', 'D', 'e', 'v',
      'i', 'c', 'e', ' ', 'N', 'a', 'm', 'e', ' ', 'U', 's', 'i', 'n', 'g', ' ',
      'A', ' ', 'L', 'o', 'n', 'g', ' ', 'A', 't', 't', 'r', 'i', 'b', 'u', 't',
      'e');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, att::AccessRequirements(),
                    att::AccessRequirements(true, false, false));
  attr->set_read_handler([&](const auto& peer_id, att::Handle handle,
                             uint16_t offset, const auto& result_cb) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(attr->handle(), handle);

    result_cb(att::ErrorCode::kUnlikelyError, common::BufferView());
  });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0C,       // opcode: read blob request
      0x02, 0x00, // handle: 0x0002
      0x16, 0x00  // offset: 0x0016
      );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x0C,        // request: read by type
      0x02, 0x00,  // handle: 0x0002 (the attribute causing the error)
      0x02         // error: Not Permitted
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadBlobRequestInvalidOffsetError) {
  const auto kTestValue = common::CreateStaticByteBuffer(
      'A', ' ', 'V', 'e', 'r', 'y', ' ', 'L', 'o', 'n', 'g', ' ', 'D', 'e', 'v',
      'i', 'c', 'e', ' ', 'N', 'a', 'm', 'e', ' ', 'U', 's', 'i', 'n', 'g', ' ',
      'A', ' ', 'L', 'o', 'n', 'g', ' ', 'A', 't', 't', 'r', 'i', 'b', 'u', 't',
      'e');

  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                                 att::AccessRequirements());
  attr->set_read_handler([&](const auto& peer_id, att::Handle handle,
                             uint16_t offset, const auto& result_cb) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(attr->handle(), handle);

    result_cb(att::ErrorCode::kInvalidOffset, common::BufferView());
  });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0C,       // opcode: read blob request
      0x02, 0x00, // handle: 0x0002
      0x16, 0x40  // offset: 0x4016
      );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x0C,        // request: read by type
      0x02, 0x00,  // handle: 0x0002 (the attribute causing the error)
      0x07         // error: Invalid Offset Error
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadRequestSuccess) {
  const auto kDeclValue = common::CreateStaticByteBuffer('d', 'e', 'c', 'l');
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');
  auto* grp = db()->NewGrouping(types::kPrimaryService, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                                 att::AccessRequirements());
  attr->set_read_handler([&](const auto& peer_id, att::Handle handle,
                             uint16_t offset, const auto& result_cb) {
    EXPECT_EQ(kTestDeviceId, peer_id);
    EXPECT_EQ(attr->handle(), handle);
    EXPECT_EQ(0u, offset);

    result_cb(att::ErrorCode::kNoError, kTestValue);
  });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x0A,       // opcode: read request
      0x02, 0x00  // handle: 0x0002
  );
  const auto kExpected = common::CreateStaticByteBuffer(
      0x0B,          // opcode: read response
      'f', 'o', 'o'  // value: kTestValue
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, SendNotificationEmpty) {
  constexpr att::Handle kHandle = 0x1234;
  const common::BufferView kTestValue;

  // clang-format off
  const auto kExpected = common::CreateStaticByteBuffer(
    0x1B,         // opcode: notification
    0x34, 0x12    // handle: |kHandle|
  );
  // clang-format on

  message_loop()->task_runner()->PostTask(
      [=] { server()->SendNotification(kHandle, kTestValue, false); });

  EXPECT_TRUE(Expect(kExpected));
}

TEST_F(GATT_ServerTest, SendNotification) {
  constexpr att::Handle kHandle = 0x1234;
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');

  // clang-format off
  const auto kExpected = common::CreateStaticByteBuffer(
    0x1B,          // opcode: notification
    0x34, 0x12,    // handle: |kHandle|
    'f', 'o', 'o'  // value: |kTestValue|
  );
  // clang-format on

  message_loop()->task_runner()->PostTask(
      [=] { server()->SendNotification(kHandle, kTestValue, false); });

  EXPECT_TRUE(Expect(kExpected));
}

TEST_F(GATT_ServerTest, SendIndicationEmpty) {
  constexpr att::Handle kHandle = 0x1234;
  const common::BufferView kTestValue;

  // clang-format off
  const auto kExpected = common::CreateStaticByteBuffer(
    0x1D,         // opcode: indication
    0x34, 0x12    // handle: |kHandle|
  );
  // clang-format on

  message_loop()->task_runner()->PostTask(
      [=] { server()->SendNotification(kHandle, kTestValue, true); });

  EXPECT_TRUE(Expect(kExpected));
}

TEST_F(GATT_ServerTest, SendIndication) {
  constexpr att::Handle kHandle = 0x1234;
  const auto kTestValue = common::CreateStaticByteBuffer('f', 'o', 'o');

  // clang-format off
  const auto kExpected = common::CreateStaticByteBuffer(
    0x1D,          // opcode: indication
    0x34, 0x12,    // handle: |kHandle|
    'f', 'o', 'o'  // value: |kTestValue|
  );
  // clang-format on

  message_loop()->task_runner()->PostTask(
      [=] { server()->SendNotification(kHandle, kTestValue, true); });

  EXPECT_TRUE(Expect(kExpected));
}

}  // namespace
}  // namespace gatt
}  // namespace btlib
