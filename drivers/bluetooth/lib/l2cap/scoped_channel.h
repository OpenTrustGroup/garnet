// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace l2cap {

// A Channel wrapper that automatically deactivates a channel when it gets
// deleted.
class ScopedChannel final {
 public:
  explicit ScopedChannel(fbl::RefPtr<Channel> channel);
  ~ScopedChannel();

  inline void operator=(decltype(nullptr)) { Close(); }
  inline operator bool() const { return static_cast<bool>(chan_); }

  inline Channel* get() const { return chan_.get(); }
  inline Channel* operator->() const { return get(); }

 private:
  void Close();

  fbl::RefPtr<Channel> chan_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedChannel);
};

}  // namespace l2cap
}  // namespace btlib
