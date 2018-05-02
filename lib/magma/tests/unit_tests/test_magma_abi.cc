// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <poll.h>

#include "magma.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"
#include "gtest/gtest.h"
#include <thread>

extern "C" {
#include "test_magma_abi.h"
}

class TestConnection {
public:
    TestConnection() : TestConnection(MAGMA_CAPABILITY_RENDERING) {}

    TestConnection(uint32_t capability)
    {
        if (capability == MAGMA_CAPABILITY_RENDERING)
            fd_ = open("/dev/class/gpu/000", O_RDONLY);
        else if (capability == MAGMA_CAPABILITY_DISPLAY)
            fd_ = open("/dev/class/display/000", O_RDONLY);

        DASSERT(fd_ >= 0);
        connection_ = magma_create_connection(fd(), capability);
    }

    ~TestConnection()
    {
        if (connection_)
            magma_release_connection(connection_);

        close(fd_);
    }

    int fd() { return fd_; }

    void GetDeviceId()
    {
        uint64_t device_id = 0;
        EXPECT_EQ(MAGMA_STATUS_OK, magma_query(fd_, MAGMA_QUERY_DEVICE_ID, &device_id));
        EXPECT_NE(0u, device_id);
    }

    magma_connection_t* connection() { return connection_; }

    void Connection() { ASSERT_NE(connection_, nullptr); }

    void Context()
    {
        ASSERT_NE(connection_, nullptr);

        uint32_t context_id[2];

        magma_create_context(connection_, &context_id[0]);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_create_context(connection_, &context_id[1]);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_release_context(connection_, context_id[0]);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_release_context(connection_, context_id[1]);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_release_context(connection_, context_id[1]);
        EXPECT_NE(magma_get_error(connection_), 0);
    }

    void NotificationChannel()
    {
        int fd1 = magma_get_notification_channel_fd(connection_);
        EXPECT_LE(0, fd1);

        int fd2 = magma_get_notification_channel_fd(connection_);
        EXPECT_LE(0, fd1);
        EXPECT_NE(fd1, fd2);

        struct pollfd fds[2] = {{fd1, POLLIN, 0}, {fd2, POLLOUT, 0}};
        int result = poll(fds, 2, 0);

        // No events should exist.
        EXPECT_EQ(0, result);
        EXPECT_EQ(0, fds[0].revents);
        EXPECT_EQ(0, fds[1].revents);
        close(fd1);
        close(fd2);
    }

    void Buffer()
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size = PAGE_SIZE;
        uint64_t actual_size;
        uint64_t id;

        EXPECT_EQ(magma_create_buffer(connection_, size, &actual_size, &id), 0);
        EXPECT_GE(size, actual_size);
        EXPECT_NE(id, 0u);

        magma_release_buffer(connection_, id);
    }

    void WaitRendering()
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size = PAGE_SIZE;
        uint64_t id;

        EXPECT_EQ(magma_create_buffer(connection_, size, &size, &id), 0);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_wait_rendering(connection_, id);
        EXPECT_EQ(magma_get_error(connection_), 0);

        magma_release_buffer(connection_, id);
        EXPECT_EQ(magma_get_error(connection_), 0);
    }

    void BufferMap()
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size = PAGE_SIZE;
        uint64_t actual_size;
        uint64_t id;

        EXPECT_EQ(magma_create_buffer(connection_, size, &actual_size, &id), 0);
        EXPECT_NE(id, 0u);

        magma_map_buffer_gpu(connection_, id, 1024, 0, size / PAGE_SIZE, MAGMA_GPU_MAP_FLAG_READ);
        magma_unmap_buffer_gpu(connection_, id, 2048);
        magma_commit_buffer(connection_, id, 100, 100);

        magma_release_buffer(connection_, id);
    }

    void BufferExport(uint32_t* handle_out, uint64_t* id_out)
    {
        ASSERT_NE(connection_, nullptr);

        uint64_t size = PAGE_SIZE;
        magma_buffer_t buffer;

        EXPECT_EQ(magma_create_buffer(connection_, size, &size, &buffer), 0);

        *id_out = magma_get_buffer_id(buffer);

        EXPECT_EQ(magma_export(connection_, buffer, handle_out), 0);
    }

    void BufferImport(uint32_t handle, uint64_t id)
    {
        ASSERT_NE(connection_, nullptr);

        magma_buffer_t buffer;
        EXPECT_EQ(magma_import(connection_, handle, &buffer), 0);
        EXPECT_EQ(magma_get_buffer_id(buffer), id);
    }

    static void BufferImportExport(TestConnection* test1, TestConnection* test2)
    {
        uint32_t handle;
        uint64_t id;
        test1->BufferExport(&handle, &id);
        test2->BufferImport(handle, id);
    }

    void Semaphore(uint32_t count)
    {
        ASSERT_NE(connection_, nullptr);

        std::vector<magma_semaphore_t> semaphore(count);
        for (uint32_t i = 0; i < count; i++) {
            EXPECT_EQ(MAGMA_STATUS_OK, magma_create_semaphore(connection_, &semaphore[i]));
            EXPECT_NE(0u, magma_get_semaphore_id(semaphore[i]));
        }

        auto thread = std::thread([semaphore, wait_all = true] {
            EXPECT_EQ(MAGMA_STATUS_OK, magma_wait_semaphores(semaphore.data(), semaphore.size(),
                                                             UINT64_MAX, wait_all));
            for (uint32_t i = 0; i < semaphore.size(); i++) {
                magma_reset_semaphore(semaphore[i]);
            }
            EXPECT_EQ(MAGMA_STATUS_TIMED_OUT,
                      magma_wait_semaphores(semaphore.data(), semaphore.size(), 100, wait_all));
        });

        for (uint32_t i = 0; i < semaphore.size(); i++) {
            usleep(10 * 1000);
            magma_signal_semaphore(semaphore[i]);
        }
        thread.join();

        thread = std::thread([semaphore, wait_all = false] {
            EXPECT_EQ(MAGMA_STATUS_OK, magma_wait_semaphores(semaphore.data(), semaphore.size(),
                                                             UINT64_MAX, wait_all));
            for (uint32_t i = 0; i < semaphore.size(); i++) {
                magma_reset_semaphore(semaphore[i]);
            }
            EXPECT_EQ(MAGMA_STATUS_TIMED_OUT,
                      magma_wait_semaphores(semaphore.data(), semaphore.size(), 100, wait_all));
        });

        usleep(10 * 1000);
        magma_signal_semaphore(semaphore[semaphore.size() - 1]);
        thread.join();

        for (uint32_t i = 0; i < semaphore.size(); i++) {
            magma_release_semaphore(connection_, semaphore[i]);
        }
    }

    void SemaphoreExport(uint32_t* handle_out, uint64_t* id_out)
    {
        ASSERT_NE(connection_, nullptr);
        magma_semaphore_t semaphore;

        EXPECT_EQ(magma_create_semaphore(connection_, &semaphore), MAGMA_STATUS_OK);
        *id_out = magma_get_semaphore_id(semaphore);
        EXPECT_EQ(magma_export_semaphore(connection_, semaphore, handle_out), MAGMA_STATUS_OK);
    }

    void SemaphoreImport(uint32_t handle, uint64_t id)
    {
        ASSERT_NE(connection_, nullptr);
        magma_semaphore_t semaphore;

        EXPECT_EQ(magma_import_semaphore(connection_, handle, &semaphore), MAGMA_STATUS_OK);
        EXPECT_EQ(magma_get_semaphore_id(semaphore), id);
    }

    static void SemaphoreImportExport(TestConnection* test1, TestConnection* test2)
    {
        uint32_t handle;
        uint64_t id;
        test1->SemaphoreExport(&handle, &id);
        test2->SemaphoreImport(handle, id);
    }

private:
    int fd_ = -1;
    magma_connection_t* connection_;
};

class TestDisplayConnection : public TestConnection {
public:
    TestDisplayConnection() : TestConnection(MAGMA_CAPABILITY_DISPLAY) {}

    bool Display(uint32_t num_buffers, uint32_t num_frames)
    {
        magma_status_t status;
        magma_display_size display_size;

        status = magma_display_get_size(fd(), &display_size);
        if (status != MAGMA_STATUS_OK)
            return DRETF(false, "magma_display_get_size returned %d", status);

        std::vector<magma_buffer_t> buffers;
        std::vector<magma_semaphore_t> wait_semaphores;
        std::vector<magma_semaphore_t> signal_semaphores;
        std::vector<magma_semaphore_t> buffer_presented_semaphores;

        for (uint32_t i = 0; i < num_buffers; i++) {
            magma_buffer_t buffer;
            uint64_t actual_size;

            status = magma_create_buffer(connection(), display_size.width * 4 * display_size.height,
                                         &actual_size, &buffer);
            if (status != MAGMA_STATUS_OK)
                return DRETF(false, "magma_alloc returned %d", status);

            buffers.push_back(buffer);

            magma_semaphore_t semaphore;
            status = magma_create_semaphore(connection(), &semaphore);
            if (status != MAGMA_STATUS_OK)
                return DRETF(false, "magma_create_semaphore returned %d", status);

            wait_semaphores.push_back(semaphore);

            status = magma_create_semaphore(connection(), &semaphore);
            if (status != MAGMA_STATUS_OK)
                return DRETF(false, "magma_create_semaphore returned %d", status);

            signal_semaphores.push_back(semaphore);

            status = magma_create_semaphore(connection(), &semaphore);
            if (status != MAGMA_STATUS_OK)
                return DRETF(false, "magma_create_semaphore returned %d", status);

            buffer_presented_semaphores.push_back(semaphore);
        }

        for (uint32_t frame = 0; frame < num_frames; frame++) {
            uint32_t index = frame % buffers.size();

            status = magma_display_page_flip(connection(), buffers[index], 1,
                                             &wait_semaphores[index], 1, &signal_semaphores[index],
                                             buffer_presented_semaphores[index]);

            magma_signal_semaphore(wait_semaphores[index]);

            if (frame > 0) {
                uint32_t last_index = (frame - 1) % buffers.size();
                status =
                    magma_wait_semaphores(&buffer_presented_semaphores[last_index], 1, 1000, true);
                if (status != MAGMA_STATUS_OK)
                    return DRETF(false, "wait on signal semaphore failed");
                DLOG("buffer presented");

                status = magma_wait_semaphores(&signal_semaphores[last_index], 1, 1000, true);
                if (status != MAGMA_STATUS_OK)
                    return DRETF(false, "wait on signal semaphore failed");
            }
        }

        return true;
    }
};

TEST(MagmaAbi, DeviceId)
{
    TestConnection test;
    test.GetDeviceId();
}

TEST(MagmaAbi, Buffer)
{
    TestConnection test;
    test.Buffer();
}

TEST(MagmaAbi, Connection)
{
    TestConnection test;
    test.Connection();
}

TEST(MagmaAbi, Context)
{
    TestConnection test;
    test.Context();
}

TEST(MagmaAbi, NotificationChannel)
{
    TestConnection test;
    test.NotificationChannel();
}

TEST(MagmaAbi, WaitRendering)
{
    TestConnection test;
    test.WaitRendering();
}

TEST(MagmaAbi, BufferMap)
{
    TestConnection test;
    test.BufferMap();
}

TEST(MagmaAbi, BufferImportExport)
{
    TestConnection test1;
    TestConnection test2;
    TestConnection::BufferImportExport(&test1, &test2);
}

TEST(MagmaAbi, Semaphore)
{
    TestConnection test;
    test.Semaphore(1);
    test.Semaphore(2);
    test.Semaphore(3);
}

TEST(MagmaAbi, SemaphoreImportExport)
{
    TestConnection test1;
    TestConnection test2;
    TestConnection::SemaphoreImportExport(&test1, &test2);
}

TEST(MagmaAbi, FromC) { EXPECT_TRUE(test_magma_abi_from_c()); }

TEST(MagmaAbi, DisplayDoubleBuffered)
{
    TestDisplayConnection test;
    EXPECT_TRUE(test.Display(2, 60));
}

TEST(MagmaAbi, DisplayTripleBuffered)
{
    TestDisplayConnection test;
    EXPECT_TRUE(test.Display(3, 60));
}
