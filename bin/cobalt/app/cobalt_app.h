// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_COBALT_APP_H_
#define GARNET_BIN_COBALT_APP_COBALT_APP_H_

#include <stdlib.h>

#include <chrono>
#include <fstream>
#include <string>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/cobalt/app/cobalt_controller_impl.h"
#include "garnet/bin/cobalt/app/cobalt_encoder_factory_impl.h"
#include "garnet/bin/cobalt/app/cobalt_encoder_impl.h"
#include "garnet/bin/cobalt/app/timer_manager.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/network_wrapper/network_wrapper_impl.h"
#include "third_party/cobalt/encoder/client_secret.h"
#include "third_party/cobalt/encoder/send_retryer.h"
#include "third_party/cobalt/encoder/shipping_dispatcher.h"
#include "third_party/cobalt/encoder/shuffler_client.h"

namespace cobalt {

class CobaltApp {
 public:
  // |async| The async_t to be used for all asynchronous operations.
  //
  // |schedule_interval| The scheduling interval provided to
  //                     ShippingManager::ScheduleParams.
  //
  // |min_interval| The minimum interval provided to
  //                ShippingManager::ScheduleParams.
  //
  // |product_name| A product name to override the one used in the
  //                ObservationMetadata.
  CobaltApp(async_t* async, std::chrono::seconds schedule_interval,
            std::chrono::seconds min_interval, const std::string& product_name);

 private:
  static encoder::ClientSecret getClientSecret();

  encoder::SystemData system_data_;

  std::unique_ptr<fuchsia::sys::StartupContext> context_;

  encoder::ShufflerClient shuffler_client_;
  encoder::send_retryer::SendRetryer send_retryer_;
  network_wrapper::NetworkWrapperImpl network_wrapper_;
  encoder::ShippingDispatcher shipping_dispatcher_;
  TimerManager timer_manager_;

  std::shared_ptr<config::ClientConfig> client_config_;

  std::unique_ptr<fuchsia::cobalt::CobaltController> controller_impl_;
  fidl::BindingSet<fuchsia::cobalt::CobaltController> controller_bindings_;

  std::unique_ptr<fuchsia::cobalt::CobaltEncoderFactory> factory_impl_;
  fidl::BindingSet<fuchsia::cobalt::CobaltEncoderFactory> factory_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltApp);
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_COBALT_APP_H_
