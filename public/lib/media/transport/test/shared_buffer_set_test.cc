// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/transport/shared_buffer_set.h"

#include <limits>

#include "gtest/gtest.h"

namespace media {
namespace {

uint32_t CreateNewBuffer(SharedBufferSet* under_test, uint64_t size) {
  uint32_t buffer_id;
  zx::vmo vmo;
  zx_status_t status = under_test->CreateNewBuffer(
      size, &buffer_id, ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_MAP, &vmo);
  EXPECT_EQ(ZX_OK, status);
  return buffer_id;
}

void AddBuffer(SharedBufferSet* under_test, uint64_t size, uint32_t buffer_id) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  EXPECT_EQ(ZX_OK, status);
  status = under_test->AddBuffer(buffer_id, std::move(vmo));
  EXPECT_EQ(ZX_OK, status);
}

void VerifyBuffer(const SharedBufferSet& under_test, uint32_t buffer_id,
                  uint64_t size) {
  uint8_t* base = reinterpret_cast<uint8_t*>(
      under_test.PtrFromLocator(SharedBufferSet::Locator(buffer_id, 0)));
  EXPECT_NE(nullptr, base);

  for (uint64_t offset = 0; offset < size; ++offset) {
    EXPECT_EQ(SharedBufferSet::Locator(buffer_id, offset),
              under_test.LocatorFromPtr(base + offset));
    EXPECT_EQ(base + offset, under_test.PtrFromLocator(
                                 SharedBufferSet::Locator(buffer_id, offset)));
  }
}

// Tests SharedBufferSet::CreateNewBuffer.
TEST(SharedBufferSetTest, CreateNewBuffer) {
  SharedBufferSet under_test(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  uint32_t buffer_id = CreateNewBuffer(&under_test, 1000);
  VerifyBuffer(under_test, buffer_id, 1000);
}

// Tests SharedBufferSet::AddBuffer.
TEST(SharedBufferSetTest, AddBuffer) {
  SharedBufferSet under_test(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  AddBuffer(&under_test, 1000, 0);
  VerifyBuffer(under_test, 0, 1000);
}

// Tests offset/ptr conversion with multiple buffers.
TEST(SharedBufferSetTest, ManyBuffers) {
  SharedBufferSet under_test(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  AddBuffer(&under_test, 1000, 0);
  AddBuffer(&under_test, 2000, 1);
  AddBuffer(&under_test, 3000, 2);
  AddBuffer(&under_test, 4000, 3);
  VerifyBuffer(under_test, 0, 1000);
  VerifyBuffer(under_test, 1, 2000);
  VerifyBuffer(under_test, 2, 3000);
  VerifyBuffer(under_test, 3, 4000);
}

// Tests offset/ptr conversion with removed buffers.
TEST(SharedBufferSetTest, RemovedBuffers) {
  SharedBufferSet under_test(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  AddBuffer(&under_test, 1000, 0);
  AddBuffer(&under_test, 2000, 1);
  AddBuffer(&under_test, 3000, 2);
  AddBuffer(&under_test, 4000, 3);
  under_test.RemoveBuffer(1);
  under_test.RemoveBuffer(3);
  VerifyBuffer(under_test, 0, 1000);
  VerifyBuffer(under_test, 2, 3000);
  AddBuffer(&under_test, 2000, 1);
  AddBuffer(&under_test, 4000, 3);
  under_test.RemoveBuffer(0);
  under_test.RemoveBuffer(2);
  VerifyBuffer(under_test, 1, 2000);
  VerifyBuffer(under_test, 3, 4000);
}

// Tests SharedBufferSet::Validate.
TEST(SharedBufferSetTest, Validate) {
  SharedBufferSet under_test(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  AddBuffer(&under_test, 1000, 0);
  VerifyBuffer(under_test, 0, 1000);
  EXPECT_FALSE(under_test.Validate(SharedBufferSet::Locator::Null(), 1));
  EXPECT_FALSE(under_test.Validate(SharedBufferSet::Locator(1, 0), 1));
  EXPECT_FALSE(under_test.Validate(SharedBufferSet::Locator(2, 0), 1));
  EXPECT_FALSE(under_test.Validate(SharedBufferSet::Locator(3, 0), 1));
  for (uint64_t offset = 0; offset < 1000; ++offset) {
    EXPECT_TRUE(under_test.Validate(SharedBufferSet::Locator(0, offset), 1));
  }
}

}  // namespace
}  // namespace media
