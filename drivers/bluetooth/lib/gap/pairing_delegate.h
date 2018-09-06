// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_GAP_PAIRING_DELEGATE_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_GAP_PAIRING_DELEGATE_H_

#include <lib/fit/function.h>

#include "garnet/drivers/bluetooth/lib/sm/smp.h"
#include "garnet/drivers/bluetooth/lib/sm/status.h"

namespace btlib {
namespace gap {

// An object that implements PairingDelegate is responsible for fulfilling user
// authentication challenges during pairing.
class PairingDelegate {
 public:
  using ConfirmCallback = fit::function<void(bool confirm)>;

  virtual ~PairingDelegate() = default;

  // Returns the I/O capability of this delegate.
  virtual sm::IOCapability io_capability() const = 0;

  // Terminate any ongoing pairing challenge for the peer device with the given
  // |identifier|.
  virtual void CompletePairing(std::string id, sm::Status status) = 0;

  // Ask the user to confirm the pairing request from the device with the given
  // |id| and confirm or reject by calling |confirm|.
  virtual void ConfirmPairing(std::string id, ConfirmCallback confirm) = 0;

  // Ask the user to confirm the 6-digit |passkey| and report status by invoking
  // |confirm|.
  virtual void DisplayPasskey(std::string id, uint32_t passkey,
                              ConfirmCallback confirm) = 0;

  // Ask the user to enter a 6-digit passkey or reject pairing. Report the
  // result by invoking |respond|.
  //
  // A valid |passkey| must be a non-negative integer. Pass a negative value to
  // reject pairing.
  using PasskeyResponseCallback = fit::function<void(int64_t passkey)>;
  virtual void RequestPasskey(std::string id,
                              PasskeyResponseCallback respond) = 0;

 protected:
  PairingDelegate() = default;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PairingDelegate);
};

}  // namespace gap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_GAP_PAIRING_DELEGATE_H_
