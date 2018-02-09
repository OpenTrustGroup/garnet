// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "mock/mock_msd.h"
#include "sys_driver/magma_system_connection.h"
#include "sys_driver/magma_system_device.h"
#include "gtest/gtest.h"

class MsdMockDevice_GetDeviceId : public MsdMockDevice {
public:
    MsdMockDevice_GetDeviceId(uint32_t device_id) : device_id_(device_id) {}

    uint32_t GetDeviceId() override { return device_id_; }

private:
    uint32_t device_id_;
};

TEST(MagmaSystemConnection, GetDeviceId)
{
    uint32_t test_id = 0xdeadbeef;

    auto msd_dev = new MsdMockDevice_GetDeviceId(test_id);
    auto device = MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev));

    uint32_t device_id = device->GetDeviceId();
    // For now device_id is invalid
    EXPECT_EQ(device_id, test_id);
}

class MsdMockConnection_ContextManagement : public MsdMockConnection {
public:
    MsdMockConnection_ContextManagement() {}

    MsdMockContext* CreateContext() override
    {
        active_context_count_++;
        return MsdMockConnection::CreateContext();
    }

    void DestroyContext(MsdMockContext* ctx) override
    {
        active_context_count_--;
        MsdMockConnection::DestroyContext(ctx);
    }

    uint32_t NumActiveContexts() { return active_context_count_; }

private:
    uint32_t active_context_count_ = 0;
};

TEST(MagmaSystemConnection, ContextManagement)
{
    auto msd_connection = new MsdMockConnection_ContextManagement();

    auto msd_dev = new MsdMockDevice();
    auto dev =
        std::shared_ptr<MagmaSystemDevice>(MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev)));
    MagmaSystemConnection connection(dev, MsdConnectionUniquePtr(msd_connection),
                                     MAGMA_CAPABILITY_RENDERING);

    EXPECT_EQ(msd_connection->NumActiveContexts(), 0u);

    uint32_t context_id_0 = 0;
    uint32_t context_id_1 = 1;

    EXPECT_TRUE(connection.CreateContext(context_id_0));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 1u);

    EXPECT_TRUE(connection.CreateContext(context_id_1));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 2u);

    EXPECT_TRUE(connection.DestroyContext(context_id_0));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 1u);
    EXPECT_FALSE(connection.DestroyContext(context_id_0));

    EXPECT_TRUE(connection.DestroyContext(context_id_1));
    EXPECT_EQ(msd_connection->NumActiveContexts(), 0u);
    EXPECT_FALSE(connection.DestroyContext(context_id_1));
}

TEST(MagmaSystemConnection, BufferManagement)
{
    auto msd_drv = msd_driver_create();
    ASSERT_NE(msd_drv, nullptr);
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    ASSERT_NE(msd_dev, nullptr);
    auto dev =
        std::shared_ptr<MagmaSystemDevice>(MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev)));
    auto msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    MagmaSystemConnection connection(dev, MsdConnectionUniquePtr(msd_connection),
                                     MAGMA_CAPABILITY_RENDERING);

    uint64_t test_size = 4096;

    auto buf = magma::PlatformBuffer::Create(test_size, "test");

    // assert because if this fails the rest of this is gonna be bogus anyway
    ASSERT_NE(buf, nullptr);
    EXPECT_GE(buf->size(), test_size);

    uint32_t duplicate_handle;
    ASSERT_TRUE(buf->duplicate_handle(&duplicate_handle));

    uint64_t id;
    EXPECT_TRUE(connection.ImportBuffer(duplicate_handle, &id));

    // should be able to get the buffer by handle
    auto get_buf = connection.LookupBuffer(id);
    EXPECT_NE(get_buf, nullptr);
    EXPECT_EQ(get_buf->id(), id); // they are shared ptrs after all

    EXPECT_TRUE(connection.ImportBuffer(duplicate_handle, &id));

    // freeing the allocated buffer should cause refcount to drop to 1
    EXPECT_TRUE(connection.ReleaseBuffer(id));
    EXPECT_NE(connection.LookupBuffer(id), nullptr);

    // freeing the allocated buffer should work
    EXPECT_TRUE(connection.ReleaseBuffer(id));

    // should no longer be able to get it from the map
    EXPECT_EQ(connection.LookupBuffer(id), nullptr);

    // should not be able to double free it
    EXPECT_FALSE(connection.ReleaseBuffer(id));
}

TEST(MagmaSystemConnection, Semaphores)
{
    auto msd_drv = msd_driver_create();
    ASSERT_NE(msd_drv, nullptr);
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    ASSERT_NE(msd_dev, nullptr);
    auto dev =
        std::shared_ptr<MagmaSystemDevice>(MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev)));
    auto msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    MagmaSystemConnection connection(dev, MsdConnectionUniquePtr(msd_connection),
                                     MAGMA_CAPABILITY_RENDERING);

    auto semaphore = magma::PlatformSemaphore::Create();

    // assert because if this fails the rest of this is gonna be bogus anyway
    ASSERT_NE(semaphore, nullptr);

    uint32_t duplicate_handle;
    ASSERT_TRUE(semaphore->duplicate_handle(&duplicate_handle));

    EXPECT_TRUE(connection.ImportObject(duplicate_handle, magma::PlatformObject::SEMAPHORE));

    auto system_semaphore = connection.LookupSemaphore(semaphore->id());
    EXPECT_NE(system_semaphore, nullptr);
    EXPECT_EQ(system_semaphore->platform_semaphore()->id(), semaphore->id());

    EXPECT_TRUE(connection.ImportObject(duplicate_handle, magma::PlatformObject::SEMAPHORE));

    // freeing the allocated semaphore should decrease refcount to 1
    EXPECT_TRUE(connection.ReleaseObject(semaphore->id(), magma::PlatformObject::SEMAPHORE));
    EXPECT_NE(connection.LookupSemaphore(semaphore->id()), nullptr);

    // freeing the allocated buffer should work
    EXPECT_TRUE(connection.ReleaseObject(semaphore->id(), magma::PlatformObject::SEMAPHORE));

    // should no longer be able to get it from the map
    EXPECT_EQ(connection.LookupSemaphore(semaphore->id()), nullptr);

    // should not be able to double free it
    EXPECT_FALSE(connection.ReleaseObject(semaphore->id(), magma::PlatformObject::SEMAPHORE));
}

TEST(MagmaSystemConnection, BufferSharing)
{
    auto msd_drv = msd_driver_create();
    ASSERT_NE(msd_drv, nullptr);
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    ASSERT_NE(msd_dev, nullptr);
    auto dev =
        std::shared_ptr<MagmaSystemDevice>(MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev)));

    auto msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    MagmaSystemConnection connection_0(dev, MsdConnectionUniquePtr(msd_connection),
                                       MAGMA_CAPABILITY_RENDERING);

    msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    MagmaSystemConnection connection_1(dev, MsdConnectionUniquePtr(msd_connection),
                                       MAGMA_CAPABILITY_RENDERING);

    auto platform_buf = magma::PlatformBuffer::Create(4096, "test");

    uint64_t buf_id_0 = 0;
    uint64_t buf_id_1 = 1;

    uint32_t duplicate_handle;
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));
    EXPECT_TRUE(connection_0.ImportBuffer(duplicate_handle, &buf_id_0));
    ASSERT_TRUE(platform_buf->duplicate_handle(&duplicate_handle));
    EXPECT_TRUE(connection_1.ImportBuffer(duplicate_handle, &buf_id_1));

    // should be the same underlying memory object
    EXPECT_EQ(buf_id_0, buf_id_1);

    auto buf_0 = connection_0.LookupBuffer(buf_id_0);
    auto buf_1 = connection_1.LookupBuffer(buf_id_1);

    // should also be shared pointers to the same MagmaSystemBuffer
    EXPECT_EQ(buf_0, buf_1);
}

TEST(MagmaSystemConnection, PageFlip)
{
    auto msd_drv = msd_driver_create();
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    auto device =
        std::shared_ptr<MagmaSystemDevice>(MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev)));

    device->SetPresentBufferCallback(
        [](std::shared_ptr<MagmaSystemBuffer> buf, magma_system_image_descriptor* image_desc,
           uint32_t wait_semaphore_count, uint32_t signal_semaphore_count,
           std::vector<std::shared_ptr<MagmaSystemSemaphore>> semaphores,
           std::unique_ptr<magma::PlatformSemaphore> buffer_presented_semaphore) {
            static std::vector<std::shared_ptr<MagmaSystemSemaphore>> last_semaphores;

            for (uint32_t i = 0; i < last_semaphores.size(); i++) {
                last_semaphores[i]->platform_semaphore()->Signal();
            }

            last_semaphores.clear();

            if (buffer_presented_semaphore)
                buffer_presented_semaphore->Signal();

            for (uint32_t i = wait_semaphore_count;
                 i < wait_semaphore_count + signal_semaphore_count; i++) {
                last_semaphores.push_back(semaphores[i]);
            }
        });

    auto msd_connection = msd_device_open(msd_dev, 0);
    ASSERT_NE(msd_connection, nullptr);
    MagmaSystemConnection connection(device, MsdConnectionUniquePtr(msd_connection),
                                     MAGMA_CAPABILITY_DISPLAY);

    auto buf = magma::PlatformBuffer::Create(PAGE_SIZE, "test");

    uint64_t imported_id;
    uint32_t handle;
    ASSERT_TRUE(buf->duplicate_handle(&handle));
    ASSERT_TRUE(connection.ImportBuffer(handle, &imported_id));
    ASSERT_EQ(buf->id(), imported_id);

    // should still be unable to page flip buffer because it hasnt been exported to display
    auto semaphore = std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());

    ASSERT_TRUE(semaphore->duplicate_handle(&handle));
    ASSERT_TRUE(connection.ImportObject(handle, magma::PlatformObject::SEMAPHORE));

    std::vector<uint64_t> semaphore_ids{semaphore->id()};
    std::vector<uint64_t> bogus_semaphore_ids{UINT64_MAX};

    auto buffer_presented_semaphore = magma::PlatformSemaphore::Create();

    // scanout the buffer
    EXPECT_TRUE(connection.PageFlip(buf->id(), 0, 1, semaphore_ids.data(),
                                    buffer_presented_semaphore->Clone()));
    EXPECT_TRUE(buffer_presented_semaphore->Wait(100));

    // should be unable to pageflip totally bogus handle
    EXPECT_FALSE(connection.PageFlip(0, 0, 0, nullptr, buffer_presented_semaphore->Clone()));

    // should be unable to pageflip unknown semaphore
    EXPECT_FALSE(connection.PageFlip(buf->id(), 0, 1, bogus_semaphore_ids.data(),
                                     buffer_presented_semaphore->Clone()));

    // should be ok to page flip now
    EXPECT_TRUE(connection.PageFlip(buf->id(), 0, 1, semaphore_ids.data(),
                                    buffer_presented_semaphore->Clone()));
    EXPECT_TRUE(buffer_presented_semaphore->Wait(100));
    EXPECT_TRUE(semaphore->Wait(100));

    msd_driver_destroy(msd_drv);
}
