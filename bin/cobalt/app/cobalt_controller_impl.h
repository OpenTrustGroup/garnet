// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_COBALT_CONTROLLER_IMPL_H_
#define GARNET_BIN_COBALT_APP_COBALT_CONTROLLER_IMPL_H_

#include <cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "third_party/cobalt/encoder/shipping_dispatcher.h"

namespace cobalt {

class CobaltControllerImpl : public CobaltController {
 public:
  // Does not take ownerhsip of |shipping_dispatcher|.
  CobaltControllerImpl(async_t* async,
                       encoder::ShippingDispatcher* shipping_dispatcher);

 private:
  void RequestSendSoon(RequestSendSoonCallback callback) override;

  void BlockUntilEmpty(uint32_t max_wait_seconds,
                       BlockUntilEmptyCallback callback) override;

  void NumSendAttempts(NumSendAttemptsCallback callback) override;

  void FailedSendAttempts(FailedSendAttemptsCallback callback) override;

  async_t* const async_;
  encoder::ShippingDispatcher* shipping_dispatcher_;  // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltControllerImpl);
};
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_COBALT_CONTROLLER_IMPL_H_
