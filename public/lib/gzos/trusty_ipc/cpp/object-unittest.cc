// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "lib/gzos/trusty_ipc/cpp/object.h"
#include "lib/gzos/trusty_ipc/cpp/object_manager.h"

namespace trusty_ipc {

class TipcObjectFake : public TipcObject {
 public:
  // It doesn't matter, just required to instantiate fake TipcObject
  virtual ObjectType get_type() override { return ObjectType(0); }
};

class TipcObjectTest : public ::testing::Test {
 protected:
  TipcObjectTest() : obj_mgr_(TipcObjectManager::Instance()) {}

  void SetUp() {
    object1_ = AdoptRef(new TipcObjectFake());
    ASSERT_TRUE(object1_ != nullptr);
    ASSERT_EQ(obj_mgr_->InstallObject(object1_), ZX_OK);

    object2_ = AdoptRef(new TipcObjectFake());
    ASSERT_TRUE(object2_ != nullptr);
    ASSERT_EQ(obj_mgr_->InstallObject(object2_), ZX_OK);
  }

  void TearDown() {
    obj_mgr_->RemoveObject(object1_->handle_id());
    obj_mgr_->RemoveObject(object2_->handle_id());
  }

  TipcObjectManager* obj_mgr_;
  fbl::RefPtr<TipcObject> object1_;
  fbl::RefPtr<TipcObject> object2_;
};

#define VerifyWaitOne(object, expected_event)                                 \
  {                                                                           \
    WaitResult result;                                                        \
    EXPECT_EQ(object->Wait(&result, zx::deadline_after(zx::msec(1))), ZX_OK); \
    EXPECT_EQ(result.handle_id, object->handle_id());                         \
    EXPECT_EQ(result.event, expected_event);                                  \
  }

#define VerifyWaitObjectSet(object, expected_id, expected_event)              \
  {                                                                           \
    WaitResult result;                                                        \
    EXPECT_EQ(object->Wait(&result, zx::deadline_after(zx::msec(1))), ZX_OK); \
    EXPECT_EQ(result.handle_id, expected_id);                                 \
    EXPECT_EQ(result.event, expected_event);                                  \
  }

#define VerifyWaitAny(expected_id, expected_event)                      \
  {                                                                     \
    WaitResult result;                                                  \
    EXPECT_EQ(obj_mgr_->Wait(&result, zx::deadline_after(zx::msec(1))), \
              ZX_OK);                                                   \
    EXPECT_EQ(result.handle_id, expected_id);                           \
    EXPECT_EQ(result.event, expected_event);                            \
  }

TEST_F(TipcObjectTest, WaitObject) {
  object1_->SignalEvent(TipcEvent::READY);

  VerifyWaitOne(object1_, TipcEvent::READY);

  // Test again should get the same result
  VerifyWaitOne(object1_, TipcEvent::READY);

  object1_->ClearEvent(TipcEvent::READY);

  // Should timeout now since event is cleared
  WaitResult result;
  EXPECT_EQ(object1_->Wait(&result, zx::deadline_after(zx::msec(1))),
            ZX_ERR_TIMED_OUT);
}

TEST_F(TipcObjectTest, WaitObjectWithMultipleEvent) {
  object1_->SignalEvent(TipcEvent::READY);
  object1_->SignalEvent(TipcEvent::MSG);

  VerifyWaitOne(object1_, TipcEvent::READY | TipcEvent::MSG);
}

TEST_F(TipcObjectTest, ClearEvent) {
  object1_->SignalEvent(TipcEvent::READY | TipcEvent::MSG);

  VerifyWaitOne(object1_, TipcEvent::READY | TipcEvent::MSG);

  object1_->ClearEvent(TipcEvent::MSG);

  VerifyWaitOne(object1_, TipcEvent::READY);

  object1_->ClearEvent(TipcEvent::READY);

  WaitResult result;
  EXPECT_EQ(object1_->Wait(&result, zx::deadline_after(zx::msec(1))),
            ZX_ERR_TIMED_OUT);
}

TEST_F(TipcObjectTest, WaitAny) {
  object1_->SignalEvent(TipcEvent::READY);
  object2_->SignalEvent(TipcEvent::MSG);

  VerifyWaitAny(object1_->handle_id(), TipcEvent::READY);
  VerifyWaitAny(object2_->handle_id(), TipcEvent::MSG);

  // Should wrap around
  VerifyWaitAny(object1_->handle_id(), TipcEvent::READY);

  object1_->ClearEvent(TipcEvent::READY);

  // Should always get object2 now since object1 event is cleared
  VerifyWaitAny(object2_->handle_id(), TipcEvent::MSG);
  VerifyWaitAny(object2_->handle_id(), TipcEvent::MSG);

  object2_->ClearEvent(TipcEvent::MSG);

  // Should timeout now since all events are cleared
  WaitResult result;
  EXPECT_EQ(obj_mgr_->Wait(&result, zx::deadline_after(zx::msec(1))),
            ZX_ERR_TIMED_OUT);
}

TEST_F(TipcObjectTest, WaitAnyWithMultipleEvent) {
  object1_->SignalEvent(TipcEvent::READY);
  object1_->SignalEvent(TipcEvent::MSG);

  VerifyWaitAny(object1_->handle_id(), TipcEvent::READY | TipcEvent::MSG);
}

class TipcObjectSetTest : public TipcObjectTest {
 protected:
  void SetUp() {
    TipcObjectTest::SetUp();

    object_set1_ = fbl::MakeRefCounted<TipcObjectSet>();
    ASSERT_TRUE(object_set1_ != nullptr);
    ASSERT_EQ(obj_mgr_->InstallObject(object_set1_), ZX_OK);

    object_set2_ = fbl::MakeRefCounted<TipcObjectSet>();
    ASSERT_TRUE(object_set2_ != nullptr);
    ASSERT_EQ(obj_mgr_->InstallObject(object_set2_), ZX_OK);
  }

  void TearDown() {
    TipcObjectTest::TearDown();

    obj_mgr_->RemoveObject(object_set1_->handle_id());
    obj_mgr_->RemoveObject(object_set2_->handle_id());
  }

  fbl::RefPtr<TipcObjectSet> object_set1_;
  fbl::RefPtr<TipcObjectSet> object_set2_;
};

TEST_F(TipcObjectSetTest, AddSelf) {
  ASSERT_EQ(object_set1_->AddObject(object_set1_), ZX_ERR_INVALID_ARGS);
}

TEST_F(TipcObjectSetTest, AddDuplicatedObject) {
  ASSERT_EQ(object_set1_->AddObject(object1_), ZX_OK);
  ASSERT_EQ(object_set1_->AddObject(object1_), ZX_ERR_ALREADY_EXISTS);
}

TEST_F(TipcObjectSetTest, WaitEmptyObjectSet) {
  WaitResult result;
  EXPECT_EQ(object_set1_->Wait(&result, zx::deadline_after(zx::msec(1))),
            ZX_ERR_NOT_FOUND);
}

TEST_F(TipcObjectSetTest, AddRemoveObject) {
  ASSERT_EQ(object_set1_->AddObject(object1_), ZX_OK);
  ASSERT_EQ(object_set1_->AddObject(object2_), ZX_OK);

  object1_->SignalEvent(TipcEvent::READY);

  VerifyWaitObjectSet(object_set1_, object1_->handle_id(), TipcEvent::READY);

  object_set1_->RemoveObject(object1_);

  WaitResult result;
  EXPECT_EQ(object_set1_->Wait(&result, zx::deadline_after(zx::msec(1))),
            ZX_ERR_TIMED_OUT);
}

TEST_F(TipcObjectSetTest, NestedAddRemoveObject) {
  ASSERT_EQ(object_set1_->AddObject(object1_), ZX_OK);
  ASSERT_EQ(object_set2_->AddObject(object_set1_), ZX_OK);

  object1_->SignalEvent(TipcEvent::READY);

  VerifyWaitObjectSet(object_set2_, object_set1_->handle_id(),
                      TipcEvent::READY);

  object_set1_->RemoveObject(object1_);

  WaitResult result;
  EXPECT_EQ(object_set2_->Wait(&result, zx::deadline_after(zx::msec(1))),
            ZX_ERR_TIMED_OUT);
}

TEST_F(TipcObjectSetTest, AssertBeforeAdd) {
  object1_->SignalEvent(TipcEvent::READY);

  ASSERT_EQ(object_set1_->AddObject(object1_), ZX_OK);

  VerifyWaitObjectSet(object_set1_, object1_->handle_id(), TipcEvent::READY);
}

TEST_F(TipcObjectSetTest, AssertBeforeNestedAdd) {
  object1_->SignalEvent(TipcEvent::READY);

  ASSERT_EQ(object_set1_->AddObject(object1_), ZX_OK);
  ASSERT_EQ(object_set2_->AddObject(object_set1_), ZX_OK);

  VerifyWaitObjectSet(object_set2_, object_set1_->handle_id(),
                      TipcEvent::READY);
  VerifyWaitObjectSet(object_set1_, object1_->handle_id(), TipcEvent::READY);
}

TEST_F(TipcObjectSetTest, CircularReference) {
  auto object_set3 = fbl::MakeRefCounted<TipcObjectSet>();
  ASSERT_TRUE(object_set3 != nullptr);

  ASSERT_EQ(object_set2_->AddObject(object_set1_), ZX_OK);
  ASSERT_EQ(object_set3->AddObject(object_set2_), ZX_OK);

  EXPECT_EQ(object_set1_->AddObject(object_set3), ZX_ERR_INVALID_ARGS);
}

TEST_F(TipcObjectSetTest, StackedObjectSet) {
  ASSERT_EQ(object_set1_->AddObject(object1_), ZX_OK);
  ASSERT_EQ(object_set1_->AddObject(object2_), ZX_OK);

  ASSERT_EQ(object_set2_->AddObject(object1_), ZX_OK);
  ASSERT_EQ(object_set2_->AddObject(object2_), ZX_OK);
  ASSERT_EQ(object_set2_->AddObject(object_set1_), ZX_OK);

  object1_->SignalEvent(TipcEvent::READY);
  object2_->SignalEvent(TipcEvent::MSG);

  // Wait from object_set1 and verify result
  VerifyWaitObjectSet(object_set1_, object1_->handle_id(), TipcEvent::READY);
  VerifyWaitObjectSet(object_set1_, object2_->handle_id(), TipcEvent::MSG);

  // Should wrap around
  VerifyWaitObjectSet(object_set1_, object1_->handle_id(), TipcEvent::READY);

  // Wait from object_set2 and verify result
  VerifyWaitObjectSet(object_set2_, object_set1_->handle_id(),
                      TipcEvent::READY);
  VerifyWaitObjectSet(object_set2_, object1_->handle_id(), TipcEvent::READY);
  VerifyWaitObjectSet(object_set2_, object2_->handle_id(), TipcEvent::MSG);

  // Should wrap around
  VerifyWaitObjectSet(object_set2_, object_set1_->handle_id(),
                      TipcEvent::READY);

  object1_->ClearEvent(TipcEvent::READY);

  // Wait from object_set1 should always get object2
  VerifyWaitObjectSet(object_set1_, object2_->handle_id(), TipcEvent::MSG);
  VerifyWaitObjectSet(object_set1_, object2_->handle_id(), TipcEvent::MSG);

  // Wait from object_set2 should get object2 and then object_set1
  VerifyWaitObjectSet(object_set2_, object2_->handle_id(), TipcEvent::MSG);
  VerifyWaitObjectSet(object_set2_, object_set1_->handle_id(),
                      TipcEvent::READY);

  object2_->ClearEvent(TipcEvent::MSG);

  // Both object_set should time out
  WaitResult result;
  EXPECT_EQ(object_set1_->Wait(&result, zx::deadline_after(zx::msec(1))),
            ZX_ERR_TIMED_OUT);
  EXPECT_EQ(object_set2_->Wait(&result, zx::deadline_after(zx::msec(1))),
            ZX_ERR_TIMED_OUT);
}

}  // namespace trusty_ipc
