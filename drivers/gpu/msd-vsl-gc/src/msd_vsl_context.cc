// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsl_context.h"

void msd_context_destroy(msd_context_t* abi_context) { delete MsdVslAbiContext::cast(abi_context); }

magma_status_t msd_context_execute_immediate_commands(msd_context_t* ctx, uint64_t commands_size,
                                                      void* commands, uint64_t semaphore_count,
                                                      msd_semaphore_t** msd_semaphores)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

magma_status_t msd_context_execute_command_buffer(msd_context_t* ctx, msd_buffer_t* cmd_buf,
                                                  msd_buffer_t** exec_resources,
                                                  msd_semaphore_t** wait_semaphores,
                                                  msd_semaphore_t** signal_semaphores)
{
    return MAGMA_STATUS_UNIMPLEMENTED;
}

void msd_context_release_buffer(msd_context_t* context, msd_buffer_t* buffer) {}
