// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_device_fake.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "gtest/gtest.h"

#define QUEUE_SIZE 16
#define VIRTIO_TEST_ID 30

namespace machina {
namespace {

TEST(VirtioQueueTest, HandleOverflow) {
  VirtioDeviceFake device;
  ASSERT_EQ(device.Init(), ZX_OK);
  VirtioQueue* queue = device.queue();
  VirtioQueueFake& queue_fake = device.queue_fake();

  // Setup queue pointers so that the next descriptor will wrap avail->idx
  // to 0.
  queue->UpdateRing<void>([](virtio_queue_t* ring) {
    const_cast<uint16_t&>(ring->avail->idx) = UINT16_MAX;
    ring->index = UINT16_MAX;
  });

  uint16_t expected_desc;
  uint32_t data = 0x12345678;
  ASSERT_EQ(queue_fake.BuildDescriptor()
                .AppendReadable(&data, sizeof(data))
                .Build(&expected_desc),
            ZX_OK);

  uint16_t desc;
  ASSERT_EQ(queue->NextAvail(&desc), ZX_OK);
  ASSERT_EQ(desc, expected_desc);
  ASSERT_EQ(queue->ring()->avail->idx, 0);
  ASSERT_EQ(queue->ring()->index, 0);
}

TEST(VirtioQueueTest, ReadDescRejectsIndexOutsideQueueSize) {
  VirtioDeviceFake device;
  ASSERT_EQ(device.Init(), ZX_OK);
  VirtioQueue* queue = device.queue();

  virtio_desc_t desc = {};
  ASSERT_EQ(queue->ReadDesc(QUEUE_SIZE, &desc), ZX_ERR_OUT_OF_RANGE);
}

TEST(VirtioQueueTest, ReadDescRejectsGuestPhysicalAddressBelowBase) {
  VirtioDeviceFake device;
  ASSERT_EQ(device.Init(), ZX_OK);
  VirtioQueue* queue = device.queue();
  VirtioQueueFake& queue_fake = device.queue_fake();

  uint16_t desc_index;
  ASSERT_EQ(queue_fake.WriteDescriptor(reinterpret_cast<void*>(UINTPTR_MAX),
                                       1, 0, &desc_index),
            ZX_OK);

  virtio_desc_t desc = {};
  ASSERT_EQ(queue->ReadDesc(desc_index, &desc), ZX_ERR_OUT_OF_RANGE);
}

}  // namespace
}  // namespace machina
