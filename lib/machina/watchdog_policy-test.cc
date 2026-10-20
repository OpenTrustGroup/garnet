// SPDX-License-Identifier: BSD-3-Clause
#include "watchdog_policy.h"
#include <assert.h>
#include <stdio.h>
#include <type_traits>
#include <initializer_list>

int main() {
  static_assert(!std::is_copy_assignable<machina::WatchdogPolicy>::value,
                "the selected policy must be immutable");
  const machina::WatchdogPolicy sink(true), nosink(false);
  for (int32_t vmid : {ZX_USER_VMID_SOS, ZX_USER_VMID_ALPS, ZX_USER_VMID_TBOX}) {
    assert(!sink.UsesPlatformWatchdog(vmid));
    assert(sink.AllowsRelaunch(vmid));
    assert(nosink.UsesPlatformWatchdog(vmid) == (vmid == ZX_USER_VMID_SOS));
    assert(nosink.AllowsRelaunch(vmid) == (vmid != ZX_USER_VMID_SOS));
  }
  // Config TBox (1) must not be mistaken for hypervisor SOS (also 1).
  assert(nosink.AllowsRelaunch(ZX_USER_VMID_TBOX));
  assert(ZX_VMID_USER_TO_GUEST(ZX_USER_VMID_TBOX) == ZX_GUEST_VMID_TBOX);
  puts("coimage watchdog policy: PASS");
}
