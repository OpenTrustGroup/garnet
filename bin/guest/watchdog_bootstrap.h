// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <functional>
#include <stdint.h>
#include <zircon/types.h>

#include "garnet/bin/guest/proto/vm_message.pb.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/watchdog.h"

namespace guest_watchdog {

using DumpStateHandler =
    std::function<void(const nbl_vmm::GuestDumpState&, uint32_t reason)>;
using StopHandler = std::function<void()>;

zx_status_t BootstrapGuestWatchdog(machina::Guest* guest,
                                   machina::Watchdog* watchdog,
                                   DumpStateHandler dump_state_handler,
                                   StopHandler stop_handler);

}  // namespace guest_watchdog
