// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <stdint.h>
#include <zircon/vmid.h>

namespace machina {

// Immutable product decision. VMIDs here are user/config IDs, never EL2 IDs.
class WatchdogPolicy {
 public:
  explicit constexpr WatchdogPolicy(bool sink_enabled)
      : sink_enabled_(sink_enabled) {}

  constexpr bool IsSink() const { return sink_enabled_; }
  constexpr const char* name() const { return IsSink() ? "sink" : "no-sink"; }
  constexpr bool UsesPlatformWatchdog(int32_t user_vmid) const {
    return !IsSink() && user_vmid == ZX_USER_VMID_SOS;
  }
  constexpr bool AllowsRelaunch(int32_t user_vmid) const {
    return !UsesPlatformWatchdog(user_vmid);
  }

 private:
  const bool sink_enabled_;
};

}  // namespace machina
