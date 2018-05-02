// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtt.h"
#include "mock/mock_bus_mapper.h"
#include "mock/mock_mmio.h"
#include "platform_mmio.h"
#include "registers.h"
#include "gtest/gtest.h"

namespace {

class MockPlatformPciDevice : public magma::PlatformPciDevice {
public:
    MockPlatformPciDevice(uint64_t bar0_size) : bar0_size_(bar0_size) {}

    void* GetDeviceHandle() override { return nullptr; }

    std::unique_ptr<magma::PlatformHandle> GetBusTransactionInitiator() override { return nullptr; }

    std::unique_ptr<magma::PlatformMmio>
    CpuMapPciMmio(unsigned int pci_bar, magma::PlatformMmio::CachePolicy cache_policy) override
    {
        DASSERT(!mmio_);

        if (pci_bar != 0)
            return DRETP(nullptr, "");

        std::unique_ptr<MockMmio> mmio = MockMmio::Create(bar0_size_);
        mmio_ = mmio.get();

        return mmio;
    }

    MockMmio* mmio() { return mmio_; }

private:
    uint64_t bar0_size_;
    MockMmio* mmio_{};
};

void check_pte_entries_clear(magma::PlatformMmio* mmio, uint64_t gpu_addr, uint64_t size)
{
    ASSERT_NE(mmio, nullptr);

    uint64_t* pte_array =
        reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(mmio->addr()) + mmio->size() / 2);

    uint32_t page_count = size >> PAGE_SHIFT;

    // Note: <= is intentional here to accout for ofer-fetch protection page
    for (unsigned int i = 0; i <= page_count; i++) {
        uint64_t pte = pte_array[(gpu_addr >> PAGE_SHIFT) + i];
        EXPECT_FALSE(pte & 0x1); // page should not be present
        EXPECT_TRUE(pte & 0x3);  // rw
    }
}

void check_pte_entries(magma::PlatformMmio* mmio, magma::PlatformBusMapper::BusMapping* bus_mapping,
                       uint64_t gpu_addr, CachingType caching_type)
{
    ASSERT_NE(mmio, nullptr);

    uint64_t* pte_array =
        reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(mmio->addr()) + mmio->size() / 2);

    auto& bus_addr_array = bus_mapping->Get();

    for (unsigned int i = 0; i < bus_addr_array.size(); i++) {
        uint64_t pte = pte_array[(gpu_addr >> PAGE_SHIFT) + i];
        EXPECT_EQ(pte & ~(PAGE_SIZE - 1), bus_addr_array[i]);
        EXPECT_TRUE(pte & 0x1); // page present
        EXPECT_TRUE(pte & 0x3); // rw
    }

    uint64_t pte = pte_array[(gpu_addr >> PAGE_SHIFT) + bus_addr_array.size()];
    EXPECT_NE(pte & ~(PAGE_SIZE - 1), 0u);
    EXPECT_TRUE(pte & 0x1); // page present
    EXPECT_TRUE(pte & 0x3); // rw
}

class TestDevice : public Gtt::Owner {
public:
    class MockBusMapping : public magma::PlatformBusMapper::BusMapping {
    public:
        MockBusMapping(uint64_t page_offset, uint64_t page_count)
            : page_offset_(page_offset), phys_addr_(page_count)
        {
        }

        uint64_t page_offset() override { return page_offset_; }
        uint64_t page_count() override { return phys_addr_.size(); }
        std::vector<uint64_t>& Get() override { return phys_addr_; }

    private:
        uint64_t page_offset_;
        std::vector<uint64_t> phys_addr_;
    };

    TestDevice() { bus_mapper_ = std::make_unique<MockBusMapper>(); }

    magma::PlatformPciDevice* platform_device() override { return platform_device_.get(); }

    magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_.get(); }

    // size_bits: 1 (2MB), 2 (4MB), 3 (8MB)
    void Init(unsigned int size_bits)
    {
        ASSERT_EQ(true, size_bits == 1 || size_bits == 2 || size_bits == 3);
        uint64_t gtt_size = (1 << size_bits) * 1024 * 1024;
        uint64_t reg_size = gtt_size;

        platform_device_ =
            std::unique_ptr<MockPlatformPciDevice>(new MockPlatformPciDevice(reg_size + gtt_size));
        auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(reg_size));

        auto gtt = Gtt::CreateCore(this);

        EXPECT_TRUE(gtt->Init(gtt_size));

        auto mmio = platform_device_->mmio();
        ASSERT_NE(mmio, nullptr);

        check_pte_entries_clear(mmio, 0, mmio->size());
    }

    void Insert()
    {
        uint64_t gtt_size = 8ULL * 1024 * 1024;
        uint64_t bar0_size = gtt_size * 2;

        platform_device_ =
            std::shared_ptr<MockPlatformPciDevice>(new MockPlatformPciDevice(bar0_size));
        auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(bar0_size));
        auto gtt = Gtt::CreateCore(this);

        EXPECT_TRUE(gtt->Init(gtt_size));

        // create some buffers
        std::vector<uint64_t> addr(2);
        std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);
        std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> bus_mapping(2);

        buffer[0] = magma::PlatformBuffer::Create(1000, "test");
        EXPECT_TRUE(gtt->Alloc(buffer[0]->size(), 0, &addr[0]));

        buffer[1] = magma::PlatformBuffer::Create(10000, "test");
        EXPECT_TRUE(gtt->Alloc(buffer[1]->size(), 0, &addr[1]));

        bus_mapping[0] = std::make_unique<MockBusMapping>(0, buffer[0]->size() / PAGE_SIZE);
        uint64_t phys_addr_base = 0xabcd0000;
        for (auto& phys_addr : bus_mapping[0]->Get()) {
            phys_addr = phys_addr_base += PAGE_SIZE;
        }

        bus_mapping[1] = std::make_unique<MockBusMapping>(0, buffer[1]->size() / PAGE_SIZE);
        for (auto& phys_addr : bus_mapping[1]->Get()) {
            phys_addr = phys_addr_base += PAGE_SIZE;
        }

        // Mismatch addr and buffer
        EXPECT_FALSE(gtt->Insert(addr[1], bus_mapping[0].get(), 0, buffer[0]->size() / PAGE_SIZE,
                                 CACHING_NONE));

        // Totally bogus addr
        EXPECT_FALSE(gtt->Insert(0xdead1000, bus_mapping[0].get(), 0, buffer[0]->size() / PAGE_SIZE,
                                 CACHING_NONE));

        EXPECT_TRUE(gtt->Insert(addr[0], bus_mapping[0].get(), 0, buffer[0]->size() / PAGE_SIZE,
                                CACHING_NONE));

        check_pte_entries(platform_device_->mmio(), bus_mapping[0].get(), addr[0], CACHING_NONE);

        EXPECT_TRUE(gtt->Insert(addr[1], bus_mapping[1].get(), 0, buffer[1]->size() / PAGE_SIZE,
                                CACHING_NONE));

        check_pte_entries(platform_device_->mmio(), bus_mapping[1].get(), addr[1], CACHING_NONE);

        // Bogus addr
        EXPECT_FALSE(gtt->Clear(0xdead1000));

        // Cool
        EXPECT_TRUE(gtt->Clear(addr[1]));

        check_pte_entries_clear(platform_device_->mmio(), addr[1], buffer[1]->size());

        EXPECT_TRUE(gtt->Clear(addr[0]));

        check_pte_entries_clear(platform_device_->mmio(), addr[0], buffer[0]->size());

        // Bogus addr
        EXPECT_FALSE(gtt->Free(0xdead1000));

        // Cool
        EXPECT_TRUE(gtt->Free(addr[0]));
        EXPECT_TRUE(gtt->Free(addr[1]));
    }

    std::shared_ptr<MockPlatformPciDevice> platform_device_;
    std::unique_ptr<MockBusMapper> bus_mapper_;
};

TEST(Gtt, Init3) { TestDevice().Init(3); }

TEST(Gtt, Init2) { TestDevice().Init(2); }

TEST(Gtt, Init1) { TestDevice().Init(1); }

TEST(Gtt, Insert) { TestDevice().Insert(); }

} // namespace
