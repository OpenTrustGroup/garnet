// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_MAPPING_H
#define GPU_MAPPING_H

#include "magma_util/macros.h"
#include "platform_buffer.h"
#include "types.h"
#include <memory>

class AddressSpace;
class MsdIntelBuffer;

class GpuMapping {
public:
    GpuMapping(std::shared_ptr<AddressSpace> address_space, std::shared_ptr<MsdIntelBuffer> buffer,
               uint64_t offset, uint64_t length, gpu_addr_t gpu_addr,
               std::unique_ptr<magma::PlatformBuffer::BusMapping> bus_mapping);

    ~GpuMapping();

    MsdIntelBuffer* buffer() { return buffer_.get(); }

    gpu_addr_t gpu_addr()
    {
        DASSERT(!address_space_.expired());
        return gpu_addr_;
    }

    std::weak_ptr<AddressSpace> address_space() { return address_space_; }

    uint64_t offset() { return offset_; }

    uint64_t length() { return length_; }

private:
    std::weak_ptr<AddressSpace> address_space_;
    std::shared_ptr<MsdIntelBuffer> buffer_;
    uint64_t offset_;
    uint64_t length_;
    gpu_addr_t gpu_addr_;
    std::unique_ptr<magma::PlatformBuffer::BusMapping> bus_mapping_;
};

#endif // GPU_MAPPING_H
