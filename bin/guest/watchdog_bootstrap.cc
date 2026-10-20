// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest/watchdog_bootstrap.h"

#include <google/protobuf/text_format.h>
#include <zircon/device/grt-wdt.h>

#include <utility>
#include <vector>

#include "lib/fxl/logging.h"

namespace guest_watchdog {

namespace {

nbl_vmm::GuestDumpState CollectGuestDumpState(machina::Guest* guest) {
  std::vector<std::string> vcpu_states;
  guest->Dump(vcpu_states);

  nbl_vmm::GuestDumpState guest_state;
  for (auto& state : vcpu_states) {
    auto* vcpu_state = guest_state.add_vcpus();
    const bool success =
        google::protobuf::TextFormat::ParseFromString(state, vcpu_state);
    FXL_CHECK(success);
  }
  return guest_state;
}

}  // namespace

zx_status_t BootstrapGuestWatchdog(machina::Guest* guest,
                                   machina::Watchdog* watchdog,
                                   DumpStateHandler dump_state_handler,
                                   StopHandler stop_handler) {
  FXL_CHECK(guest);
  FXL_CHECK(watchdog);
  FXL_CHECK(stop_handler);

  FXL_LOG(INFO) << "vmid=" << guest->vmid()
                << " event=guest_watchdog_bootstrap_start";
  zx_status_t status = guest->create_wdt_fd();
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Watchdog control device is unavailable for vmid "
                     << guest->vmid()
                     << ", continuing with software-only watchdog: "
                     << status;
  }
  if (guest->get_wdt_fd() >= 0) {
    FXL_LOG(INFO) << "vmid " << guest->vmid() << " watchdog created";
  }

  watchdog->RegisterDumpStateCallback(
      [guest, dump_state_handler = std::move(dump_state_handler)](
          uint32_t reason) mutable {
        FXL_LOG(WARNING) << "vmid=" << guest->vmid()
                         << " event=guest_watchdog_dump_callback reason="
                         << reason;
        auto guest_state = CollectGuestDumpState(guest);
        if (guest->get_wdt_fd() >= 0) {
          FXL_LOG(INFO) << "vmid=" << guest->vmid()
                        << " event=guest_watchdog_set_reset_status";
          ioctl_grt_wdt_set_rst_status(guest->get_wdt_fd());
        }
        if (dump_state_handler) {
          dump_state_handler(guest_state, reason);
        }
      });

  watchdog->RegisterStopCallback(
      [guest, stop_handler = std::move(stop_handler)]() mutable {
        FXL_LOG(WARNING) << "vmid=" << guest->vmid()
                         << " event=guest_watchdog_stop_callback";
        stop_handler();
      });

  status = watchdog->Init(guest->vmid());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize guest watchdog for vmid "
                   << guest->vmid() << ": " << status;
    return status;
  }

  guest->RegisterWatchdog(watchdog);
  FXL_LOG(INFO) << "vmid=" << guest->vmid()
                << " event=guest_watchdog_registered";

  status = watchdog->StartMonitoring();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start guest watchdog monitoring for vmid "
                   << guest->vmid() << ": " << status;
    return status;
  }

  FXL_LOG(INFO) << "vmid=" << guest->vmid()
                << " event=guest_watchdog_bootstrap_complete";
  return ZX_OK;
}

}  // namespace guest_watchdog
