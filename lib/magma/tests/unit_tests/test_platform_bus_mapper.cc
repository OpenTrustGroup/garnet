// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "platform_bus_mapper.h"
#include "gtest/gtest.h"
#include <vector>

class TestPlatformBusMapper {
public:
    static void Basic(magma::PlatformBusMapper* mapper, uint32_t page_count)
    {
        std::unique_ptr<magma::PlatformBuffer> buffer =
            magma::PlatformBuffer::Create(page_count * PAGE_SIZE, "test");

        std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping;

        bus_mapping = mapper->MapPageRangeBus(buffer.get(), 0, 0);
        EXPECT_FALSE(bus_mapping);

        bus_mapping = mapper->MapPageRangeBus(buffer.get(), 0, page_count + 1);
        EXPECT_FALSE(bus_mapping);

        // Map each page individually, and release.
        for (uint32_t i = 0; i < page_count; i++) {
            bus_mapping = mapper->MapPageRangeBus(buffer.get(), i, 1);
            ASSERT_TRUE(bus_mapping);
            EXPECT_EQ(1u, bus_mapping->page_count());
            EXPECT_EQ(i, bus_mapping->page_offset());
        }

        // Map the full range.
        bus_mapping = mapper->MapPageRangeBus(buffer.get(), 0, page_count);
        ASSERT_TRUE(bus_mapping);
        EXPECT_EQ(page_count, bus_mapping->page_count());
        EXPECT_EQ(0u, bus_mapping->page_offset());
    }

    static void Overlapped(magma::PlatformBusMapper* mapper, uint32_t page_count)
    {
        std::unique_ptr<magma::PlatformBuffer> buffer =
            magma::PlatformBuffer::Create(page_count * PAGE_SIZE, "test");

        std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> mappings;

        for (uint32_t i = 0; i < 3; i++) { // A few times
            auto bus_mapping = mapper->MapPageRangeBus(buffer.get(), 0, 1);
            ASSERT_TRUE(bus_mapping);
            EXPECT_EQ(0u, bus_mapping->page_offset());
            EXPECT_EQ(1u, bus_mapping->page_count());
            mappings.push_back(std::move(bus_mapping));

            bus_mapping = mapper->MapPageRangeBus(buffer.get(), 0, 1);
            ASSERT_TRUE(bus_mapping);
            EXPECT_EQ(0u, bus_mapping->page_offset());
            EXPECT_EQ(1u, bus_mapping->page_count());
            mappings.push_back(std::move(bus_mapping));

            bus_mapping = mapper->MapPageRangeBus(buffer.get(), 0, page_count / 2);
            ASSERT_TRUE(bus_mapping);
            EXPECT_EQ(0u, bus_mapping->page_offset());
            EXPECT_EQ(page_count / 2, bus_mapping->page_count());
            mappings.push_back(std::move(bus_mapping));

            bus_mapping = mapper->MapPageRangeBus(buffer.get(), 0, page_count);
            ASSERT_TRUE(bus_mapping);
            EXPECT_EQ(0u, bus_mapping->page_offset());
            EXPECT_EQ(page_count, bus_mapping->page_count());
            mappings.push_back(std::move(bus_mapping));

            bus_mapping = mapper->MapPageRangeBus(buffer.get(), 1, page_count - 1);
            ASSERT_TRUE(bus_mapping);
            EXPECT_EQ(1u, bus_mapping->page_offset());
            EXPECT_EQ(page_count - 1, bus_mapping->page_count());
            mappings.push_back(std::move(bus_mapping));

            mappings.clear();
        }
    }
};

TEST(PlatformPciDevice, BusMapperBasic)
{
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_TRUE(platform_device);
    auto mapper = magma::PlatformBusMapper::Create(platform_device->GetBusTransactionInitiator());
    ASSERT_TRUE(mapper);

    TestPlatformBusMapper::Basic(mapper.get(), 1);
    TestPlatformBusMapper::Basic(mapper.get(), 2);
    TestPlatformBusMapper::Basic(mapper.get(), 10);
}

TEST(PlatformPciDevice, BusMapperOverlapped)
{
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_TRUE(platform_device);
    auto mapper = magma::PlatformBusMapper::Create(platform_device->GetBusTransactionInitiator());
    ASSERT_TRUE(mapper);

    TestPlatformBusMapper::Overlapped(mapper.get(), 12);
}

TEST(PlatformDevice, BusMapperBasic)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_TRUE(platform_device);
    auto mapper = magma::PlatformBusMapper::Create(platform_device->GetBusTransactionInitiator());
    ASSERT_TRUE(mapper);

    TestPlatformBusMapper::Basic(mapper.get(), 1);
    TestPlatformBusMapper::Basic(mapper.get(), 2);
    TestPlatformBusMapper::Basic(mapper.get(), 10);
}

TEST(PlatformDevice, BusMapperOverlapped)
{
    magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
    ASSERT_TRUE(platform_device);
    auto mapper = magma::PlatformBusMapper::Create(platform_device->GetBusTransactionInitiator());
    ASSERT_TRUE(mapper);

    TestPlatformBusMapper::Overlapped(mapper.get(), 12);
}
