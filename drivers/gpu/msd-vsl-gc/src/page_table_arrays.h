// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PAGE_TABLE_ARRAYS_H_
#define PAGE_TABLE_ARRAYS_H_

#include "address_space.h"
#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "platform_buffer.h"
#include "platform_bus_mapper.h"

class PageTableArrays {
public:
    static std::unique_ptr<PageTableArrays> Create(magma::PlatformBusMapper* bus_mapper);

    PageTableArrays() = default;

    uint64_t bus_addr()
    {
        DASSERT(bus_mapping_);
        return bus_mapping_->Get()[0];
    }

    void HardwareInit(magma::RegisterIo* register_io);

private:
    static constexpr uint32_t kPageTableArraySizeInPages = 1;
    static constexpr uint32_t kPageTableArrayEntries =
        kPageTableArraySizeInPages * PAGE_SIZE / sizeof(uint64_t);

    bool Init(magma::PlatformBusMapper* bus_mapper);
    void AssignAddressSpace(uint32_t index, AddressSpace* address_space);
    void Enable(magma::RegisterIo* register_io, bool enable);

    std::unique_ptr<magma::PlatformBuffer> page_table_array_;
    std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping_;
    uint64_t* master_tlb_bus_addr_ = nullptr;

    std::unique_ptr<magma::PlatformBuffer> security_safe_page_;
    std::unique_ptr<magma::PlatformBusMapper::BusMapping> security_safe_page_bus_mapping_;

    std::unique_ptr<magma::PlatformBuffer> non_security_safe_page_;
    std::unique_ptr<magma::PlatformBusMapper::BusMapping> non_security_safe_page_bus_mapping_;

    DISALLOW_COPY_AND_ASSIGN(PageTableArrays);

    friend class TestMsdVslDevice;
};

#endif // PAGE_TABLE_ARRAYS_H_
