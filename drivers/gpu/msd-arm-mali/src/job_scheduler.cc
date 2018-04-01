// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "job_scheduler.h"

#include "magma_util/dlog.h"
#include "platform_trace.h"

JobScheduler::JobScheduler(Owner* owner, uint32_t job_slots)
    : owner_(owner), job_slots_(job_slots), executing_atoms_(job_slots)
{
}

void JobScheduler::EnqueueAtom(std::shared_ptr<MsdArmAtom> atom)
{
    atoms_.push_back(std::move(atom));
}

// Use different names for different slots so they'll line up cleanly in the
// trace viewer.
static const char* AtomRunningString(uint32_t slot)
{
    switch (slot) {
        case 0:
            return "Atom running slot 0";
        case 1:
            return "Atom running slot 1";
        case 2:
            return "Atom running slot 2";
        default:
            DASSERT(false);
            return "Atom running unknown slot";
    }
}

void JobScheduler::TryToSchedule()
{
    while (true) {
        if (atoms_.empty())
            return;
        bool found_atom = false;
        for (auto it = atoms_.begin(); it != atoms_.end(); ++it) {
            std::shared_ptr<MsdArmAtom> atom = *it;
            bool dependencies_finished;
            atom->UpdateDependencies(&dependencies_finished);
            if (dependencies_finished) {
                ArmMaliResultCode dep_status = atom->GetFinalDependencyResult();
                if (dep_status != kArmMaliResultSuccess) {
                    owner_->AtomCompleted(it->get(), dep_status);
                    atoms_.erase(it);
                    break;
                }

                auto soft_atom = MsdArmSoftAtom::cast(atom);
                if (soft_atom) {
                    found_atom = true;
                    atoms_.erase(it);
                    soft_atom->SetExecutionStarted();
                    ProcessSoftAtom(soft_atom);
                    break;
                } else if (atom->IsDependencyOnly()) {
                    found_atom = true;
                    owner_->AtomCompleted(it->get(), kArmMaliResultSuccess);
                    atoms_.erase(it);
                    break;

                } else {
                    uint32_t slot = atom->slot();
                    DASSERT(slot < executing_atoms_.size());
                    if (!executing_atoms_[slot]) {
                        found_atom = true;
                        atom->SetExecutionStarted();
                        executing_atoms_[slot] = atom;
                        atoms_.erase(it);
                        TRACE_ASYNC_BEGIN("magma", AtomRunningString(slot),
                                          executing_atoms_[slot]->trace_nonce());
                        owner_->RunAtom(executing_atoms_[slot].get());
                        break;
                    }
                }
            } else {
                DLOG("Skipping atom %lx due to dependency", (atom)->gpu_address());
            }
        }
        if (!found_atom)
            return;
    }
}

void JobScheduler::CancelAtomsForConnection(std::shared_ptr<MsdArmConnection> connection)
{
    auto removal_function = [connection](auto it) {
        auto locked = it->connection().lock();
        return !locked || locked == connection;
    };
    waiting_atoms_.erase(
        std::remove_if(waiting_atoms_.begin(), waiting_atoms_.end(), removal_function),
        waiting_atoms_.end());

    atoms_.remove_if(removal_function);
}

void JobScheduler::JobCompleted(uint64_t slot, ArmMaliResultCode result_code)
{
    DASSERT(executing_atoms_[slot]);
    TRACE_ASYNC_END("magma", AtomRunningString(slot), executing_atoms_[slot]->trace_nonce());
    owner_->AtomCompleted(executing_atoms_[slot].get(), result_code);
    executing_atoms_[slot].reset();
    TryToSchedule();
}

void JobScheduler::SoftJobCompleted(std::shared_ptr<MsdArmSoftAtom> atom)
{
    owner_->AtomCompleted(atom.get(), kArmMaliResultSuccess);
    // The loop in TryToSchedule should cause any atoms that just had their
    // dependencies satisfied to run.
}

void JobScheduler::PlatformPortSignaled(uint64_t key)
{
    std::vector<std::shared_ptr<MsdArmSoftAtom>> unfinished_atoms;
    bool completed_atom = false;
    for (auto& atom : waiting_atoms_) {
        bool wait_succeeded;
        if (atom->soft_flags() == kAtomFlagSemaphoreWait) {
            wait_succeeded = atom->platform_semaphore()->WaitNoReset(0);
        } else {
            DASSERT(atom->soft_flags() == kAtomFlagSemaphoreWaitAndReset);
            wait_succeeded = atom->platform_semaphore()->Wait(0);
        }

        if (wait_succeeded) {
            completed_atom = true;
            owner_->AtomCompleted(atom.get(), kArmMaliResultSuccess);
        } else {
            if (atom->platform_semaphore()->id() == key)
                atom->platform_semaphore()->WaitAsync(owner_->GetPlatformPort());
            unfinished_atoms.push_back(atom);
        }
    }
    if (completed_atom) {
        waiting_atoms_ = unfinished_atoms;
        TryToSchedule();
    }
}

size_t JobScheduler::GetAtomListSize() { return atoms_.size(); }

JobScheduler::Clock::duration JobScheduler::GetCurrentTimeoutDuration()
{
    auto execution_start = Clock::time_point::max();
    for (auto& atom : executing_atoms_) {
        if (!atom || atom->hard_stopped())
            continue;
        if (atom->execution_start_time() < execution_start)
            execution_start = atom->execution_start_time();
    }

    if (execution_start == Clock::time_point::max())
        return Clock::duration::max();
    return execution_start + std::chrono::milliseconds(timeout_duration_ms_) - Clock::now();
}

void JobScheduler::KillTimedOutAtoms()
{
    auto now = Clock::now();
    for (auto& atom : executing_atoms_) {
        if (!atom || atom->hard_stopped())
            continue;
        if (atom->execution_start_time() + std::chrono::milliseconds(timeout_duration_ms_) <= now) {
            atom->set_hard_stopped();
            owner_->HardStopAtom(atom.get());
        }
    }
}

void JobScheduler::ProcessSoftAtom(std::shared_ptr<MsdArmSoftAtom> atom)
{
    DASSERT(owner_->GetPlatformPort());
    if (atom->soft_flags() == kAtomFlagSemaphoreSet) {
        atom->platform_semaphore()->Signal();
        SoftJobCompleted(atom);
    } else if (atom->soft_flags() == kAtomFlagSemaphoreReset) {
        atom->platform_semaphore()->Reset();
        SoftJobCompleted(atom);
    } else if ((atom->soft_flags() == kAtomFlagSemaphoreWait) ||
               (atom->soft_flags() == kAtomFlagSemaphoreWaitAndReset)) {
        bool wait_succeeded;
        if (atom->soft_flags() == kAtomFlagSemaphoreWait) {
            wait_succeeded = atom->platform_semaphore()->WaitNoReset(0);
        } else {
            wait_succeeded = atom->platform_semaphore()->Wait(0);
        }

        if (wait_succeeded) {
            SoftJobCompleted(atom);
        } else {
            waiting_atoms_.push_back(atom);
            atom->platform_semaphore()->WaitAsync(owner_->GetPlatformPort());
        }
    } else {
        DASSERT(false);
    }
}

void JobScheduler::ReleaseMappingsForConnection(std::shared_ptr<MsdArmConnection> connection)
{

    for (auto& executing_atom : executing_atoms_) {
        if (executing_atom && executing_atom->connection().lock() == connection) {
            executing_atom->set_hard_stopped();
            owner_->ReleaseMappingsForAtom(executing_atom.get());
        }
    }
}
