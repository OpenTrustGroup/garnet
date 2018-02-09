// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_connection.h"
#include "magma_util/dlog.h"
#include "msd_intel_semaphore.h"
#include "ppgtt.h"

void msd_connection_close(msd_connection_t* connection)
{
    delete MsdIntelAbiConnection::cast(connection);
}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection)
{
    auto connection = MsdIntelAbiConnection::cast(abi_connection)->ptr();

    // Backing store creation deferred until context is used.
    return new MsdIntelAbiContext(
        std::make_unique<ClientContext>(connection, connection->per_process_gtt()));
}

magma_status_t msd_connection_wait_rendering(msd_connection_t* abi_connection, msd_buffer_t* buffer)
{
    auto connection = MsdIntelAbiConnection::cast(abi_connection)->ptr();

    if (connection->context_killed())
        return DRET(MAGMA_STATUS_CONTEXT_KILLED);

    MsdIntelAbiBuffer::cast(buffer)->ptr()->WaitRendering();

    if (connection->context_killed())
        return DRET(MAGMA_STATUS_CONTEXT_KILLED);

    return MAGMA_STATUS_OK;
}

void msd_connection_set_notification_channel(msd_connection_t* connection,
                                             msd_channel_send_callback_t callback,
                                             msd_channel_t channel)
{
    // The channel isn't used for anything.
}

void msd_connection_map_buffer_gpu(msd_connection_t* connection, msd_buffer_t* buffer,
                                   uint64_t gpu_va, uint64_t page_offset, uint64_t page_count,
                                   uint64_t flags)
{
}

void msd_connection_unmap_buffer_gpu(msd_connection_t* connection, msd_buffer_t* buffer,
                                     uint64_t gpu_va)
{
}

void msd_connection_commit_buffer(msd_connection_t* connection, msd_buffer_t* buffer,
                                  uint64_t page_offset, uint64_t page_count)
{
}

void msd_connection_release_buffer(msd_connection_t* connection, msd_buffer_t* buffer)
{
}

std::unique_ptr<MsdIntelConnection> MsdIntelConnection::Create(Owner* owner,
                                                               msd_client_id_t client_id)
{
    std::unique_ptr<GpuMappingCache> cache;
#if MSD_INTEL_ENABLE_MAPPING_CACHE
    cache = GpuMappingCache::Create();
#endif
    return std::unique_ptr<MsdIntelConnection>(
        new MsdIntelConnection(owner, PerProcessGtt::Create(std::move(cache)), client_id));
}
