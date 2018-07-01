// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_hash_table.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/fit/function.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/gatt/client.h"
#include "garnet/drivers/bluetooth/lib/gatt/remote_characteristic.h"

namespace btlib {
namespace gatt {

// Callback type invoked to notify when GATT services get discovered.
class RemoteService;
using RemoteServiceWatcher = fit::function<void(fbl::RefPtr<RemoteService>)>;

using ServiceList = std::vector<fbl::RefPtr<RemoteService>>;
using ServiceListCallback = fit::function<void(att::Status, ServiceList)>;

using RemoteServiceCallback = fit::function<void(fbl::RefPtr<RemoteService>)>;
using RemoteCharacteristicList = std::vector<RemoteCharacteristic>;

namespace internal {
class RemoteServiceManager;
}  // namespace internal

// Represents the state of a GATT service that was discovered on a remote
// device. Clients can interact with a remote GATT service by obtaining a
// RemoteService object from the GATT system.
//
// THREAD SAFETY:
//
// A RemoteService can be accessed from multiple threads. All continuations
// provided in |callback| parameters below will run on the GATT thread unless an
// async dispatcher is explicitly provided.
class RemoteService : public fbl::RefCounted<RemoteService> {
 public:
  // Shuts down this service. Called when the service gets removed (e.g. due to
  // disconnection or because it was removed by the peer).
  void ShutDown();

  const ServiceData& info() const { return service_data_; }

  // Returns the service range start handle. This is used to uniquely identify
  // this service.
  att::Handle handle() const { return service_data_.range_start; }

  // Returns the service UUID.
  const common::UUID& uuid() const { return service_data_.type; }

  // Adds a handler which will be called when this service gets removed.
  // Returns false if the service was already shut down. |callback| will be
  // posted on |dispatcher|.
  bool AddRemovedHandler(fit::closure handler, async_t* dispatcher = nullptr);

  // Returns true if all contents of this service have been discovered. This can
  // only be called on the GATT thread and is primarily intended for unit tests.
  // Clients should not rely on this and use DiscoverCharacteristics() to
  // guarantee discovery.
  bool IsDiscovered() const;

  // Performs characteristic discovery and reports the result asynchronously in
  // |callback|. Returns the cached results if characteristics were already
  // discovered.
  using CharacteristicCallback =
      fit::function<void(att::Status, const RemoteCharacteristicList&)>;
  void DiscoverCharacteristics(CharacteristicCallback callback,
                               async_t* dispatcher = nullptr);

  // Sends a read request to the characteristic with the given identifier. Fails
  // if characteristics have not been discovered.
  //
  // NOTE: Providing a |dispatcher| results in a copy of the resulting value.
  using ReadValueCallback =
      fit::function<void(att::Status, const common::ByteBuffer&)>;
  void ReadCharacteristic(IdType id,
                          ReadValueCallback callback,
                          async_t* dispatcher = nullptr);

  // Sends a write request to the characteristic with the given identifier.
  // Fails if characteristics have not been discovered.
  //
  // TODO(armansito): Add a ByteBuffer version.
  void WriteCharacteristic(IdType id,
                           std::vector<uint8_t> value,
                           att::StatusCallback callback,
                           async_t* dispatcher = nullptr);

  // Subscribe to characteristic handle/value notifications or indications
  // from the characteristic with the given identifier. Either notifications or
  // indications will be enabled depending on the characteristic properties.
  //
  // This method can be called more than once to register multiple subscribers.
  // The remote Client Characteristic Configuration descriptor will be written
  // only if this is called for the first subscriber.
  //
  // |status_callback| will be called with the status of the operation. On
  // success, a |handler_id| will be returned that can be used to unregister the
  // handler.
  //
  // On success, notifications will be delivered to |callback|.
  //
  // NOTE: Providing a |dispatcher| results in a copy of the notified value.
  using ValueCallback = RemoteCharacteristic::ValueCallback;
  using NotifyStatusCallback = RemoteCharacteristic::NotifyStatusCallback;
  void EnableNotifications(IdType id, ValueCallback callback,
                           NotifyStatusCallback status_callback,
                           async_t* dispatcher = nullptr);

  // Disables characteristic notifications for the given |handler_id| previously
  // obtained via EnableNotifications. The value of the Client Characteristic
  // Configuration descriptor will be cleared if no subscribers remain.
  void DisableNotifications(IdType characteristic_id, IdType handler_id,
                            att::StatusCallback status_callback,
                            async_t* dispatcher = nullptr);

 private:
  friend class fbl::RefPtr<RemoteService>;
  friend class internal::RemoteServiceManager;

  static constexpr size_t kSentinel = std::numeric_limits<size_t>::max();

  template <typename T>
  struct PendingCallback {
    PendingCallback(T callback, async_t* dispatcher)
        : callback(std::move(callback)), dispatcher(dispatcher) {
      FXL_DCHECK(this->callback);
    }

    T callback;
    async_t* dispatcher;
  };

  using PendingClosure = PendingCallback<fit::closure>;
  using PendingCharacteristicCallback = PendingCallback<CharacteristicCallback>;

  // A RemoteService can only be constructed by a RemoteServiceManager.
  RemoteService(const ServiceData& service_data,
                fxl::WeakPtr<Client> client,
                async_t* gatt_dispatcher);
  ~RemoteService();

  bool alive() const __TA_REQUIRES(mtx_) { return !shut_down_; }

  // Returns true if called on the GATT dispatcher's thread. False otherwise.
  // Intended for assertions only.
  bool IsOnGattThread() const;

  // Returns a pointer to the characteristic with |id|. Returns nullptr if not
  // found.
  common::HostError GetCharacteristic(IdType id,
                                      RemoteCharacteristic** out_char);

  // Called immediately after characteristic discovery to initiate descriptor
  // discovery.
  void StartDescriptorDiscovery() __TA_EXCLUDES(mtx_);

  // Runs |task| on the GATT dispatcher. |mtx_| must not be held when calling
  // this method. This guarantees that this object's will live for the duration
  // of |task|.
  void RunGattTask(fit::closure task) __TA_EXCLUDES(mtx_);

  // Used to complete a characteristic discovery request.
  void ReportCharacteristics(att::Status status,
                             CharacteristicCallback callback,
                             async_t* dispatcher) __TA_EXCLUDES(mtx_);

  // Completes all pending characteristic discovery requests.
  void CompleteCharacteristicDiscovery(att::Status status) __TA_EXCLUDES(mtx_);

  // Returns true if characteristic discovery has completed. This must be
  // accessed only through |gatt_dispatcher_|.
  inline bool HasCharacteristics() const {
    FXL_DCHECK(IsOnGattThread());
    return remaining_descriptor_requests_ == 0u;
  }

  // Called by RemoteServiceManager when a notification is received for one of
  // this service's characteristics.
  void HandleNotification(att::Handle value_handle,
                          const common::ByteBuffer& value);

  ServiceData service_data_;

  // All unguarded members below MUST be accessed via |gatt_dispatcher_|.
  async_t* gatt_dispatcher_;

  // The GATT Client bearer for performing remote procedures.
  fxl::WeakPtr<Client> client_;

  // Queued discovery requests. Accessed only on the GATT dispatcher.
  using PendingDiscoveryList = std::vector<PendingCharacteristicCallback>;
  PendingDiscoveryList pending_discov_reqs_;

  // The known characteristics of this service. If not |characteristics_ready_|,
  // this may contain a partial list of characteristics stored during the
  // discovery process.
  //
  // The id of each characteristic corresponds to its index in this vector.
  //
  // NOTE: This collection gets populated on |gatt_dispatcher_| and does not get
  // modified after discovery finishes. It is not publicly exposed until
  // discovery completes.
  RemoteCharacteristicList characteristics_;

  // The number of pending characteristic descriptor discoveries.
  // Characteristics get marked as ready when this number reaches 0.
  size_t remaining_descriptor_requests_;

  // Guards the members below.
  std::mutex mtx_;

  // Set to true by ShutDown() which makes this service defunct. This happens
  // when the remote device that this service was found on removes this service
  // or gets disconnected.
  //
  // This member will only get modified on the GATT thread while holding |mtx_|.
  // Holding |mtx_| is not necessary when read on the GATT thread but necessary
  // for all other threads.
  bool shut_down_;

  // Called by ShutDown().
  std::vector<PendingClosure> rm_handlers_ __TA_GUARDED(mtx_);

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteService);
};

}  // namespace gatt
}  // namespace btlib
