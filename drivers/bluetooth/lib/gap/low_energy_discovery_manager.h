// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>
#include <unordered_set>

#include <fbl/function.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/gap/discovery_filter.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/fxl/tasks/task_runner.h"

namespace btlib {

namespace hci {
class Transport;
}  // namespace hci

namespace gap {

class LowEnergyDiscoveryManager;
class RemoteDevice;
class RemoteDeviceCache;

// LowEnergyDiscoveryManager implements GAP LE central/observer role device
// discovery procedures. This class provides mechanisms for multiple clients to
// simultaneously scan for nearby devices filtered by adveritising data
// contents. This class also provides hooks for other layers to manage the
// Adapter's scan state for other procedures that require it (e.g. connection
// establishment, pairing procedures, and other scan and advertising
// procedures).
// TODO(armansito): The last sentence of this paragraph hasn't been implemented
// yet.
//
// An instance of LowEnergyDiscoveryManager can be initialized in either
// "legacy" or "extended" mode. The legacy mode is intended for Bluetooth
// controllers that only support the pre-5.0 HCI scan command set. The extended
// mode is intended for Bluetooth controllers that claim to support the "LE
// Extended Advertising" feature.
//
// Only one instance of LowEnergyDiscoveryManager should be created per
// hci::Transport object as multiple instances cannot correctly maintain state
// if they operate concurrently.
//
// To request a session, a client calls StartDiscovery() and asynchronously
// obtains a LowEnergyDiscoverySession that it uniquely owns. The session object
// can be configured with a callback to receive scan results. The session
// maintains an internal filter that may be modified to restrict the scan
// results based on properties of received advertisements.
//
// PROCEDURE:
//
// Starting the first discovery session initiates a periodic scan procedure, in
// which the device scan is stopped and restarted for a given scan period (10.24
// seconds by default). This continues until all sessions have been removed.
//
// By default duplicate filtering is used which means that a new advertising
// report will be generated for each discovered advertiser only once per scan
// period. Scan results for each scan period are cached so that sessions added
// during a scan period can receive previously processed results.
//
// EXAMPLE:
//     btlib::gap::LowEnergyDiscoveryManager discovery_manager(
//         btlib::gap::LowEnergyDiscoveryManager::Mode::kLegacy,
//         transport, task_runner);
//     ...
//
//     std::unique_ptr<btlib::gap::LowEnergyDiscoverySession> session;
//     discovery_manager.StartDiscovery([&session](auto new_session) {
//       // Take ownership of the session to make sure it isn't terminated when
//       // this callback returns.
//       session = std::move(new_session);
//
//       // Only scan for devices advertising the "Heart Rate" GATT Service.
//       uint16_t uuid = 0x180d;
//       session->filter()->set_service_uuids({btlib::common::UUID(uuid)});
//       session->SetResultCallback([](const
//       btlib::hci::LowEnergyScanResult& result,
//                                     const btlib::common::ByteBuffer&
//                                     advertising_data) {
//         // Do stuff with |result| and |advertising_data|. (|advertising_data|
//         // contains any received Scan Response data as well).
//       });
//     });
//
// NOTE: These classes are not thread-safe. An instance of
// LowEnergyDiscoveryManager is bound to its creation thread and the associated
// TaskRunner and must be accessed and destroyed on the same thread.

// Represents a LE device discovery session initiated via
// LowEnergyDiscoveryManager::StartDiscovery(). Instances cannot be created
// directly; instead they are handed to callers by LowEnergyDiscoveryManager.
//
// The discovery classes are not thread-safe. A LowEnergyDiscoverySession MUST
// be accessed and destroyed on the thread that it was created on.
class LowEnergyDiscoverySession final {
 public:
  // Destroying a session instance automatically ends the session. To terminate
  // a session, a client may either explicitly call Stop() or simply destroy
  // this instance.
  ~LowEnergyDiscoverySession();

  // Sets a callback for receiving notifications on newly discovered devices.
  // |data| contains advertising and scan response data (if any) obtained during
  // discovery.
  //
  // When this callback is set, it will immediately receive notifications for
  // the cached results from the most recent scan period. If a filter was
  // assigned earlier, then the callback will only receive results that match
  // the filter.
  using DeviceFoundCallback = std::function<void(const RemoteDevice& device)>;
  void SetResultCallback(const DeviceFoundCallback& callback);

  // Sets a callback to get notified when the session becomes inactive due to an
  // internal error.
  void set_error_callback(fbl::Closure callback) {
    error_callback_ = std::move(callback);
  }

  // Returns the filter that belongs to this session. The caller may modify the
  // filter as desired. By default the filter is configured to match
  // discoverable devices (i.e. limited and general discoverable) based on their
  // "Flags" field.
  DiscoveryFilter* filter() { return &filter_; }

  // Ends this session. This instance will stop receiving notifications for
  // devices.
  void Stop();

  // Returns true if this session is active. A session is considered inactive
  // after a call to Stop().
  bool active() const { return active_; }

  // Resets the filter values to its defaults, which will match all connectable
  // and limited & general discoverable devices.
  void ResetToDefault();

 private:
  friend class LowEnergyDiscoveryManager;

  // Called by LowEnergyDiscoveryManager.
  explicit LowEnergyDiscoverySession(
      fxl::WeakPtr<LowEnergyDiscoveryManager> manager);

  // Called by LowEnergyDiscoveryManager on newly discovered scan results.
  void NotifyDiscoveryResult(const RemoteDevice& device) const;

  // Marks this session as inactive and notifies the error handler.
  void NotifyError();

  inline void SetGeneralDiscoverableFlags() {
    filter_.set_flags(
        static_cast<uint8_t>(AdvFlag::kLELimitedDiscoverableMode) |
        static_cast<uint8_t>(AdvFlag::kLEGeneralDiscoverableMode));
  }

  bool active_;
  fxl::WeakPtr<LowEnergyDiscoveryManager> manager_;
  fbl::Closure error_callback_;
  DeviceFoundCallback device_found_callback_;
  DiscoveryFilter filter_;
  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyDiscoverySession);
};

// See comments above.
class LowEnergyDiscoveryManager final : public hci::LowEnergyScanner::Delegate {
 public:
  // |device_cache| MUST out-live this LowEnergyDiscoveryManager.
  LowEnergyDiscoveryManager(Mode mode,
                            fxl::RefPtr<hci::Transport> hci,
                            RemoteDeviceCache* device_cache);
  virtual ~LowEnergyDiscoveryManager();

  // Starts a new discovery session and reports the result via |callback|. If a
  // session has been successfully started the caller will receive a new
  // LowEnergyDiscoverySession instance via |callback| which it uniquely owns.
  // On failure a nullptr will be returned via |callback|.
  //
  // TODO(armansito): Implement option to disable duplicate filtering. Would
  // this require software filtering for clients that did not request it?
  using SessionCallback =
      std::function<void(std::unique_ptr<LowEnergyDiscoverySession>)>;
  void StartDiscovery(const SessionCallback& callback);

  // Sets a new scan period to any future and ongoing discovery procedures.
  void set_scan_period(int64_t period_ms) { scan_period_ = period_ms; }

 private:
  friend class LowEnergyDiscoverySession;

  const RemoteDeviceCache* device_cache() const { return device_cache_; }

  const std::unordered_set<std::string>& cached_scan_results() const {
    return cached_scan_results_;
  }

  // Creates and stores a new session object and returns it.
  std::unique_ptr<LowEnergyDiscoverySession> AddSession();

  // Called by LowEnergyDiscoverySession to stop a session that it was assigned
  // to.
  void RemoveSession(LowEnergyDiscoverySession* session);

  // hci::LowEnergyScanner::Delegate override:
  void OnDeviceFound(const hci::LowEnergyScanResult& result,
                     const common::ByteBuffer& data) override;

  // Called by hci::LowEnergyScanner
  void OnScanStatus(hci::LowEnergyScanner::Status status);

  // Tells the scanner to start scanning.
  void StartScan();

  // The task runner that we use for invoking callbacks asynchronously.
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  // The device cache that we use for storing and looking up scan results. We
  // hold a raw pointer as we expect this to out-live us.
  RemoteDeviceCache* device_cache_;

  // The list of currently pending calls to start discovery.
  std::queue<SessionCallback> pending_;

  // The list of currently active/known sessions. We store raw (weak) pointers
  // here because, while we don't actually own the session objects they will
  // always notify us before destruction so we can remove them from this list.
  //
  // The number of elements in |sessions_| acts as our scan reference count.
  // When |sessions_| becomes empty scanning is stopped. Similarly, scanning is
  // started on the insertion of the first element.
  std::unordered_set<LowEnergyDiscoverySession*> sessions_;

  // Identifiers for the cached scan results for the current scan period during
  // device discovery. The minimum (and default) scan period is 10.24 seconds
  // when performing LE discovery. This can cause a long wait for a discovery
  // session that joined in the middle of a scan period and duplicate filtering
  // is enabled. We maintain this cache to immediately notify new sessions of
  // the currently cached results for this period.
  std::unordered_set<std::string> cached_scan_results_;

  // The value (in ms) that we use for the duration of each scan period.
  int64_t scan_period_ = kLEGeneralDiscoveryScanMinMs;

  // The scanner that performs the HCI procedures.
  std::unique_ptr<hci::LowEnergyScanner> scanner_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<LowEnergyDiscoveryManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyDiscoveryManager);
};

}  // namespace gap
}  // namespace btlib
