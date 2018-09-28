// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/timekeeper/system_clock.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>
#include <zircon/types.h>

namespace wlan {

class Timer {
   public:
    explicit Timer(uint64_t id);
    virtual ~Timer();

    virtual zx::time Now() const = 0;

    // TODO(tkilbourn): add slack
    zx_status_t SetTimer(zx::time deadline);
    zx_status_t CancelTimer();

    uint64_t id() const { return id_; }
    zx::time deadline() const { return deadline_; }

   protected:
    virtual zx_status_t SetTimerImpl(zx::time deadline) = 0;
    virtual zx_status_t CancelTimerImpl() = 0;

   private:
    uint64_t id_;
    zx::time deadline_;
};

class SystemTimer final : public Timer {
   public:
    SystemTimer(uint64_t id, zx::timer timer);

    zx::time Now() const override { return clock_.Now(); }

   protected:
    zx_status_t SetTimerImpl(zx::time deadline) override;
    zx_status_t CancelTimerImpl() override;

   private:
    timekeeper::SystemClock clock_;
    zx::timer timer_;
};

}  // namespace wlan
