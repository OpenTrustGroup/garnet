// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_atom.h"

#include "magma_util/macros.h"
#include "platform_trace.h"

MsdArmAtom::MsdArmAtom(std::weak_ptr<MsdArmConnection> connection, uint64_t gpu_address,
                       uint32_t slot, uint8_t atom_number, magma_arm_mali_user_data user_data)
    : trace_nonce_(TRACE_NONCE()), connection_(connection), gpu_address_(gpu_address), slot_(slot),
      atom_number_(atom_number), user_data_(user_data)
{
}

void MsdArmAtom::set_dependencies(const std::vector<std::weak_ptr<MsdArmAtom>>& dependencies)
{
    DASSERT(dependencies_.empty());
    dependencies_ = dependencies;
}

bool MsdArmAtom::AreDependenciesFinished()
{
    for (auto dependency : dependencies_) {
        auto locked_dependency = dependency.lock();
        if (locked_dependency && !locked_dependency->finished())
            return false;
    }
    return true;
}

void MsdArmAtom::set_address_slot_mapping(std::shared_ptr<AddressSlotMapping> address_slot_mapping)
{
    if (address_slot_mapping) {
        DASSERT(!address_slot_mapping_);
        DASSERT(address_slot_mapping->connection());
        DASSERT(connection_.lock() == address_slot_mapping->connection());
    }
    address_slot_mapping_ = address_slot_mapping;
}

void MsdArmAtom::SetExecutionStarted() { execution_start_time_ = std::chrono::steady_clock::now(); }
