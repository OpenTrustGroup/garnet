// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_BUS_MAPPER_H
#define ZIRCON_PLATFORM_BUS_MAPPER_H

#include "platform_bus_mapper.h"
#include "zircon_platform_buffer.h"
#include <vector>

namespace magma {

class ZirconPlatformBusMapper : public PlatformBusMapper {
public:
    ZirconPlatformBusMapper(std::shared_ptr<ZirconPlatformHandle> bus_transaction_initiator)
        : bus_transaction_initiator_(std::move(bus_transaction_initiator))
    {
    }

    std::unique_ptr<BusMapping> MapPageRangeBus(PlatformBuffer* buffer, uint32_t start_page_index,
                                                uint32_t page_count) override;

    class BusMapping : public PlatformBusMapper::BusMapping {
    public:
        BusMapping(uint64_t page_offset, uint64_t page_count,
                   std::weak_ptr<ZirconPlatformHandle> bus_transaction_initiator)
            : page_offset_(page_offset), page_addr_(page_count),
              bus_transaction_initiator_(bus_transaction_initiator)
        {
        }
        ~BusMapping();

        uint64_t page_offset() override { return page_offset_; }
        uint64_t page_count() override { return page_addr_.size(); }

        std::vector<uint64_t>& Get() override { return page_addr_; }

    private:
        uint64_t page_offset_;
        std::vector<uint64_t> page_addr_;
        std::weak_ptr<ZirconPlatformHandle> bus_transaction_initiator_;
    };

private:
    std::shared_ptr<ZirconPlatformHandle> bus_transaction_initiator_;
};

} // namespace magma

#endif // ZIRCON_PLATFORM_BUS_MAPPER_H
