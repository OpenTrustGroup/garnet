// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_UTIL_FIDL_PUBLISHER_H_
#define GARNET_BIN_MEDIA_UTIL_FIDL_PUBLISHER_H_

#include <vector>

#include <lib/fit/function.h>

#include "lib/fxl/logging.h"

namespace media {

// Implements pull mode publishing (e.g. MediaPlayer::GetStatus).
template <typename TCallback>
class FidlPublisher {
 public:
  using CallbackRunner = fit::function<void(TCallback, uint64_t)>;

  // Sets the callback runner. This method must be called before calling Get
  // or Updated. The callback runner calls a single callback using current
  // information.
  void SetCallbackRunner(CallbackRunner callback_runner) {
    FXL_DCHECK(callback_runner);
    callback_runner_ = std::move(callback_runner);
  }

  // Handles a get request from the client. This method should be called from
  // the fidl 'get' method (e.g. MediaPlayer::GetStatus).
  void Get(uint64_t version_last_seen, TCallback callback) {
    FXL_DCHECK(callback_runner_);

    if (version_last_seen < version_) {
      callback_runner_(std::move(callback), version_);
    } else {
      pending_callbacks_.push_back(std::move(callback));
    }
  }

  // Increments the version number and runs pending callbacks. This method
  // should be called whenever the published information should be sent to
  // subscribing clients.
  void SendUpdates() {
    FXL_DCHECK(callback_runner_);

    ++version_;

    std::vector<TCallback> pending_callbacks;
    pending_callbacks_.swap(pending_callbacks);

    for (TCallback& pending_callback : pending_callbacks) {
      callback_runner_(std::move(pending_callback), version_);
    }
  }

 private:
  uint64_t version_ = 1u;
  std::vector<TCallback> pending_callbacks_;
  CallbackRunner callback_runner_;
};

}  // namespace media

#endif  // GARNET_BIN_MEDIA_UTIL_FIDL_PUBLISHER_H_
