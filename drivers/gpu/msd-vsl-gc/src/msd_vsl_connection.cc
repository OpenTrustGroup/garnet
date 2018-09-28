// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsl_connection.h"

#include "msd_vsl_buffer.h"
#include "msd_vsl_context.h"

void msd_connection_close(msd_connection_t* connection)
{
    delete MsdVslAbiConnection::cast(connection);
}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection)
{
    return new MsdVslAbiContext(std::make_shared<MsdVslContext>(
        MsdVslAbiConnection::cast(abi_connection)->ptr()->address_space()));
}

magma_status_t msd_connection_map_buffer_gpu(msd_connection_t* abi_connection,
                                             msd_buffer_t* abi_buffer, uint64_t gpu_va,
                                             uint64_t page_offset, uint64_t page_count,
                                             uint64_t flags)
{
    auto connection = MsdVslAbiConnection::cast(abi_connection)->ptr();
    auto buffer = MsdVslAbiBuffer::cast(abi_buffer)->ptr();

    auto bus_mapping = connection->bus_mapper()->MapPageRangeBus(buffer->platform_buffer(),
                                                                 page_offset, page_count);
    if (!bus_mapping)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to map page range to bus");

    if (!connection->address_space()->AddMapping(std::make_unique<GpuMapping>(
            connection->address_space(), buffer, std::move(bus_mapping), gpu_va)))
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to add mapping");

    return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_unmap_buffer_gpu(msd_connection_t* abi_connection,
                                               msd_buffer_t* abi_buffer, uint64_t gpu_va)
{
    if (!MsdVslAbiConnection::cast(abi_connection)
             ->ptr()
             ->address_space()
             ->RemoveMapping(MsdVslAbiBuffer::cast(abi_buffer)->ptr()->platform_buffer(), gpu_va))
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to remove mapping");

    return MAGMA_STATUS_OK;
}

void msd_connection_release_buffer(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer)
{
    uint32_t released_count;
    MsdVslAbiConnection::cast(abi_connection)
        ->ptr()
        ->address_space()
        ->ReleaseBuffer(MsdVslAbiBuffer::cast(abi_buffer)->ptr()->platform_buffer(),
                        &released_count);
}

magma_status_t msd_connection_commit_buffer(msd_connection_t* abi_connection,
                                            msd_buffer_t* abi_buffer, uint64_t page_offset,
                                            uint64_t page_count)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

void msd_connection_set_notification_callback(struct msd_connection_t* connection,
                                              msd_connection_notification_callback_t callback,
                                              void* token)
{
}
