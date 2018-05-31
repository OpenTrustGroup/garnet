// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_COBALT_ENCODER_IMPL_H_
#define GARNET_BIN_COBALT_APP_COBALT_ENCODER_IMPL_H_

#include <stdlib.h>

#include <cobalt/cpp/fidl.h>

#include "garnet/bin/cobalt/app/timer_manager.h"
#include "third_party/cobalt/config/client_config.h"
#include "third_party/cobalt/encoder/client_secret.h"
#include "third_party/cobalt/encoder/encoder.h"
#include "third_party/cobalt/encoder/project_context.h"
#include "third_party/cobalt/encoder/send_retryer.h"
#include "third_party/cobalt/encoder/shipping_dispatcher.h"
#include "third_party/cobalt/encoder/shuffler_client.h"

namespace cobalt {
namespace encoder {

class CobaltEncoderImpl : public CobaltEncoder {
 public:
  // Does not take ownership of |timer_manager|, |shipping_dispatcher| or
  // |system_data|.
  CobaltEncoderImpl(std::unique_ptr<encoder::ProjectContext> project_context,
                    ClientSecret client_secret,
                    ShippingDispatcher* shipping_dispatcher,
                    const SystemData* system_data, TimerManager* timer_manager);

 private:
  template <class CB>
  void AddEncodedObservation(Encoder::Result* result, CB callback);

  void AddStringObservation(uint32_t metric_id, uint32_t encoding_id,
                            fidl::StringPtr observation,
                            AddStringObservationCallback callback) override;

  void AddIntObservation(uint32_t metric_id, uint32_t encoding_id,
                         const int64_t observation,
                         AddIntObservationCallback callback) override;

  void AddDoubleObservation(uint32_t metric_id, uint32_t encoding_id,
                            const double observation,
                            AddDoubleObservationCallback callback) override;

  void AddIndexObservation(uint32_t metric_id, uint32_t encoding_id,
                           uint32_t index,
                           AddIndexObservationCallback callback) override;

  void AddObservation(uint32_t metric_id, uint32_t encoding_id,
                      Value observation,
                      AddObservationCallback callback) override;

  void AddMultipartObservation(
      uint32_t metric_id, fidl::VectorPtr<ObservationValue> observation,
      AddMultipartObservationCallback callback) override;

  void AddIntBucketDistribution(
      uint32_t metric_id, uint32_t encoding_id,
      fidl::VectorPtr<BucketDistributionEntry> distribution,
      AddIntBucketDistributionCallback callback) override;

  // Adds an observation from the timer given if both StartTimer and EndTimer
  // have been encountered.
  template <class CB>
  void AddTimerObservationIfReady(std::unique_ptr<TimerVal> timer_val_ptr,
                                  CB callback);

  void StartTimer(uint32_t metric_id, uint32_t encoding_id,
                  fidl::StringPtr timer_id, uint64_t timestamp,
                  uint32_t timeout_s, StartTimerCallback callback) override;

  void EndTimer(fidl::StringPtr timer_id, uint64_t timestamp,
                uint32_t timeout_s, EndTimerCallback callback) override;

  void EndTimerMultiPart(fidl::StringPtr timer_id, uint64_t timestamp,
                         fidl::StringPtr part_name,
                         fidl::VectorPtr<ObservationValue> observation,
                         uint32_t timeout_s,
                         EndTimerMultiPartCallback callback) override;

  void SendObservations(SendObservationsCallback callback) override;

  Encoder encoder_;
  ShippingDispatcher* shipping_dispatcher_;  // not owned
  TimerManager* timer_manager_;              // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltEncoderImpl);
};

}  // namespace encoder
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_COBALT_ENCODER_IMPL_H_
