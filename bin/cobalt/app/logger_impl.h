// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_LOGGER_IMPL_H_
#define GARNET_BIN_COBALT_APP_LOGGER_IMPL_H_

#include <stdlib.h>

#include <fuchsia/cobalt/cpp/fidl.h>

#include "garnet/bin/cobalt/app/timer_manager.h"
#include "third_party/cobalt/config/client_config.h"
#include "third_party/cobalt/encoder/client_secret.h"
#include "third_party/cobalt/encoder/encoder.h"
#include "third_party/cobalt/encoder/observation_store_dispatcher.h"
#include "third_party/cobalt/encoder/project_context.h"
#include "third_party/cobalt/encoder/send_retryer.h"
#include "third_party/cobalt/encoder/shipping_dispatcher.h"
#include "third_party/cobalt/encoder/shuffler_client.h"
#include "third_party/cobalt/util/encrypted_message_util.h"

namespace cobalt {

class LoggerImpl : public fuchsia::cobalt::Logger {
 public:
  LoggerImpl(std::unique_ptr<encoder::ProjectContext> project_context,
             encoder::ClientSecret client_secret,
             encoder::ObservationStoreDispatcher* store_dispatcher,
             util::EncryptedMessageMaker* encrypt_to_analyzer,
             encoder::ShippingDispatcher* shipping_dispatcher,
             const encoder::SystemData* system_data,
             TimerManager* timer_manager);

 protected:
  // Helper function to allow LogEventCount, LogElapsedTime, LogMemoryUsage and
  // LogFrameRate to share their codepaths since they have very similar
  // implementations.
  //
  // If |value_part_required| is true, then |event_type_index| and |component|
  // are required only if the metric given by |metric_id| has INDEX and STRING
  // parts respectively. If |value_part_required| is false, then at least 2 of
  // |event_type_index|, |component| and |value| must be supplied and must have
  // corresponding MetricParts. |value_part_name| is only used to identify the
  // metric that could not be logged when an error occurs.
  template <class ValueType, class CB>
  void LogThreePartMetric(const std::string& value_part_name,
                          uint32_t metric_id, uint32_t event_type_index,
                          fidl::StringPtr component, ValueType value,
                          CB callback, bool value_part_required);

  template <class CB>
  void AddEncodedObservation(cobalt::encoder::Encoder::Result* result,
                             CB callback);

  uint32_t GetSinglePartMetricEncoding(uint32_t metric_id);

  void LogEvent(uint32_t metric_id, uint32_t event_type_index,
                LogEventCallback callback) override;

  // In the current implementation, |period_duration_micros| is ignored
  void LogEventCount(uint32_t metric_id, uint32_t event_type_index,
                     fidl::StringPtr component, int64_t period_duration_micros,
                     int64_t count, LogEventCountCallback callback) override;

  void LogElapsedTime(uint32_t metric_id, uint32_t event_type_index,
                      fidl::StringPtr component, int64_t elapsed_micros,
                      LogElapsedTimeCallback callback) override;

  void LogFrameRate(uint32_t metric_id, uint32_t event_type_index,
                    fidl::StringPtr component, float fps,
                    LogFrameRateCallback callback) override;

  void LogMemoryUsage(uint32_t metric_id, uint32_t event_type_index,
                      fidl::StringPtr component, int64_t bytes,
                      LogMemoryUsageCallback callback) override;

  void LogString(uint32_t metric_id, fidl::StringPtr s,
                 LogStringCallback callback) override;

  // Adds an observation from the timer given if both StartTimer and EndTimer
  // have been encountered.
  template <class CB>
  void AddTimerObservationIfReady(std::unique_ptr<TimerVal> timer_val_ptr,
                                  CB callback);

  void StartTimer(uint32_t metric_id, uint32_t event_type_index,
                  fidl::StringPtr component, fidl::StringPtr timer_id,
                  uint64_t timestamp, uint32_t timeout_s,
                  StartTimerCallback callback) override;

  void EndTimer(fidl::StringPtr timer_id, uint64_t timestamp,
                uint32_t timeout_s, EndTimerCallback callback) override;

  cobalt::encoder::Encoder encoder_;
  encoder::ObservationStoreDispatcher* store_dispatcher_;  // not owned
  util::EncryptedMessageMaker* encrypt_to_analyzer_;       // not owned
  encoder::ShippingDispatcher* shipping_dispatcher_;       // not owned
  TimerManager* timer_manager_;                            // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(LoggerImpl);
};

class LoggerExtImpl : public LoggerImpl, public fuchsia::cobalt::LoggerExt {
 public:
  using LoggerImpl::LoggerImpl;

 private:
  // In the current implementation, |event_type_index| and |component| are
  // ignored.
  void LogIntHistogram(
      uint32_t metric_id, uint32_t event_type_index, fidl::StringPtr component,
      fidl::VectorPtr<fuchsia::cobalt::HistogramBucket> histogram,
      LogIntHistogramCallback callback) override;

  void LogCustomEvent(
      uint32_t metric_id,
      fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> event_values,
      LogCustomEventCallback callback) override;
};

class LoggerSimpleImpl : public LoggerImpl,
                         public fuchsia::cobalt::LoggerSimple {
 public:
  using LoggerImpl::LoggerImpl;

 private:
  void LogIntHistogram(uint32_t metric_id, uint32_t event_type_index,
                       fidl::StringPtr component,
                       fidl::VectorPtr<uint32_t> bucket_indices,
                       fidl::VectorPtr<uint64_t> bucket_counts,
                       LogIntHistogramCallback callback) override;

  void LogCustomEvent(uint32_t metric_id, fidl::StringPtr json_string,
                      LogCustomEventCallback callback) override;
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_LOGGER_IMPL_H_
