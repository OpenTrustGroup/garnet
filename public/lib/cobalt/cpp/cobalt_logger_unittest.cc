// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/cobalt/cpp/cobalt_logger.h"
#include "garnet/public/lib/cobalt/cpp/cobalt_logger_impl.h"

#include <lib/async/default.h>
#include <lib/component/cpp/service_provider_impl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/macros.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/svc/cpp/service_provider_bridge.h>
#include <zx/time.h>

namespace cobalt {
namespace {

constexpr char kFakeCobaltConfig[] = "FakeConfig";
constexpr int32_t kFakeCobaltMetricId = 2;

bool Equals(const OccurrenceEvent* e1, const OccurrenceEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_type_index() == e2->event_type_index();
}

bool Equals(const CountEvent* e1, const CountEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_type_index() == e2->event_type_index() &&
         e1->component() == e2->component() &&
         e1->period_duration_micros() == e2->period_duration_micros() &&
         e1->count() == e2->count();
}

bool Equals(const ElapsedTimeEvent* e1, const ElapsedTimeEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_type_index() == e2->event_type_index() &&
         e1->component() == e2->component() &&
         e1->elapsed_micros() == e2->elapsed_micros();
}

bool Equals(const FrameRateEvent* e1, const FrameRateEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_type_index() == e2->event_type_index() &&
         e1->component() == e2->component() && e1->fps() == e2->fps();
}

bool Equals(const MemoryUsageEvent* e1, const MemoryUsageEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_type_index() == e2->event_type_index() &&
         e1->component() == e2->component() && e1->bytes() == e2->bytes();
}

bool Equals(const StringUsedEvent* e1, const StringUsedEvent* e2) {
  return e1->metric_id() == e2->metric_id() && e1->s() == e2->s();
}

bool Equals(const StartTimerEvent* e1, const StartTimerEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_type_index() == e2->event_type_index() &&
         e1->component() == e2->component() &&
         e1->timer_id() == e2->timer_id() &&
         e1->timestamp() == e2->timestamp() &&
         e1->timeout_s() == e2->timeout_s();
}

bool Equals(const EndTimerEvent* e1, const EndTimerEvent* e2) {
  return e1->timer_id() == e2->timer_id() &&
         e1->timestamp() == e2->timestamp() &&
         e1->timeout_s() == e2->timeout_s();
}

bool Equals(const IntHistogramEvent* e1, const IntHistogramEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_type_index() == e2->event_type_index() &&
         e1->component() == e2->component() &&
         e1->histogram() == e2->histogram();
}

bool Equals(const CustomEvent* e1, const CustomEvent* e2) {
  return e1->metric_id() == e2->metric_id() &&
         e1->event_values() == e2->event_values();
}

enum EventType {
  EVENT_OCCURRED,
  EVENT_COUNT,
  ELAPSED_TIME,
  FRAME_RATE,
  MEMORY_USAGE,
  STRING_USED,
  START_TIMER,
  END_TIMER,
  INT_HISTOGRAM,
  CUSTOM,
};

class FakeLoggerImpl : public fuchsia::cobalt::Logger {
 public:
  FakeLoggerImpl() {}

  void LogEvent(uint32_t metric_id, uint32_t event_type_index,
                LogEventCallback callback) override {
    RecordCall(EventType::EVENT_OCCURRED,
               std::make_unique<OccurrenceEvent>(metric_id, event_type_index));
    callback(fuchsia::cobalt::Status::OK);
  }

  void LogEventCount(uint32_t metric_id, uint32_t event_type_index,
                     fidl::StringPtr component, int64_t period_duration_micros,
                     int64_t count, LogEventCountCallback callback) override {
    RecordCall(
        EventType::EVENT_COUNT,
        std::make_unique<CountEvent>(metric_id, event_type_index, component,
                                     period_duration_micros, count));
    callback(fuchsia::cobalt::Status::OK);
  }

  void LogElapsedTime(uint32_t metric_id, uint32_t event_type_index,
                      fidl::StringPtr component, int64_t elapsed_micros,
                      LogElapsedTimeCallback callback) override {
    RecordCall(EventType::ELAPSED_TIME,
               std::make_unique<ElapsedTimeEvent>(metric_id, event_type_index,
                                                  component, elapsed_micros));
    callback(fuchsia::cobalt::Status::OK);
  }

  void LogFrameRate(uint32_t metric_id, uint32_t event_type_index,
                    fidl::StringPtr component, float fps,
                    LogFrameRateCallback callback) override {
    RecordCall(EventType::FRAME_RATE,
               std::make_unique<FrameRateEvent>(metric_id, event_type_index,
                                                component, fps));
    callback(fuchsia::cobalt::Status::OK);
  }

  void LogMemoryUsage(uint32_t metric_id, uint32_t event_type_index,
                      fidl::StringPtr component, int64_t bytes,
                      LogMemoryUsageCallback callback) override {
    RecordCall(EventType::MEMORY_USAGE,
               std::make_unique<MemoryUsageEvent>(metric_id, event_type_index,
                                                  component, bytes));
    callback(fuchsia::cobalt::Status::OK);
  }

  void LogString(uint32_t metric_id, fidl::StringPtr s,
                 LogStringCallback callback) override {
    RecordCall(EventType::STRING_USED,
               std::make_unique<StringUsedEvent>(metric_id, s));
    callback(fuchsia::cobalt::Status::OK);
  }

  void StartTimer(uint32_t metric_id, uint32_t event_type_index,
                  fidl::StringPtr component, fidl::StringPtr timer_id,
                  uint64_t timestamp, uint32_t timeout_s,
                  StartTimerCallback callback) override {
    RecordCall(EventType::START_TIMER,
               std::make_unique<StartTimerEvent>(metric_id, event_type_index,
                                                 component, timer_id, timestamp,
                                                 timeout_s));
    callback(fuchsia::cobalt::Status::OK);
  }

  void EndTimer(fidl::StringPtr timer_id, uint64_t timestamp,
                uint32_t timeout_s, EndTimerCallback callback) override {
    RecordCall(EventType::END_TIMER,
               std::make_unique<EndTimerEvent>(timer_id, timestamp, timeout_s));
    callback(fuchsia::cobalt::Status::OK);
  }

  void LogIntHistogram(
      uint32_t metric_id, uint32_t event_type_index, fidl::StringPtr component,
      fidl::VectorPtr<fuchsia::cobalt::HistogramBucket> histogram,
      LogIntHistogramCallback callback) override {
    RecordCall(EventType::INT_HISTOGRAM, std::make_unique<IntHistogramEvent>(
                                             metric_id, event_type_index,
                                             component, std::move(histogram)));
    callback(fuchsia::cobalt::Status::OK);
  }

  void LogCustomEvent(
      uint32_t metric_id,
      fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> event_values,
      LogCustomEventCallback callback) override {
    RecordCall(EventType::CUSTOM, std::make_unique<CustomEvent>(
                                      metric_id, std::move(event_values)));
    callback(fuchsia::cobalt::Status::OK);
  }

  void ExpectCalledOnceWith(EventType type, const Event* expected) {
    ASSERT_EQ(1U, calls_[type].size());
    switch (type) {
      case EventType::EVENT_OCCURRED:
        EXPECT_TRUE(
            Equals(static_cast<const OccurrenceEvent*>(expected),
                   static_cast<OccurrenceEvent*>(calls_[type][0].get())));
        break;
      case EventType::EVENT_COUNT:
        EXPECT_TRUE(Equals(static_cast<const CountEvent*>(expected),
                           static_cast<CountEvent*>(calls_[type][0].get())));
        break;
      case EventType::ELAPSED_TIME:
        EXPECT_TRUE(
            Equals(static_cast<const ElapsedTimeEvent*>(expected),
                   static_cast<ElapsedTimeEvent*>(calls_[type][0].get())));
        break;
      case EventType::FRAME_RATE:
        EXPECT_TRUE(
            Equals(static_cast<const FrameRateEvent*>(expected),
                   static_cast<FrameRateEvent*>(calls_[type][0].get())));
        break;
      case EventType::MEMORY_USAGE:
        EXPECT_TRUE(
            Equals(static_cast<const MemoryUsageEvent*>(expected),
                   static_cast<MemoryUsageEvent*>(calls_[type][0].get())));
        break;
      case EventType::STRING_USED:
        EXPECT_TRUE(
            Equals(static_cast<const StringUsedEvent*>(expected),
                   static_cast<StringUsedEvent*>(calls_[type][0].get())));
        break;
      case EventType::START_TIMER:
        EXPECT_TRUE(
            Equals(static_cast<const StartTimerEvent*>(expected),
                   static_cast<StartTimerEvent*>(calls_[type][0].get())));
        break;
      case EventType::END_TIMER:
        EXPECT_TRUE(Equals(static_cast<const EndTimerEvent*>(expected),
                           static_cast<EndTimerEvent*>(calls_[type][0].get())));
        break;
      case EventType::INT_HISTOGRAM:
        EXPECT_TRUE(
            Equals(static_cast<const IntHistogramEvent*>(expected),
                   static_cast<IntHistogramEvent*>(calls_[type][0].get())));
        break;
      case EventType::CUSTOM:
        EXPECT_TRUE(Equals(static_cast<const CustomEvent*>(expected),
                           static_cast<CustomEvent*>(calls_[type][0].get())));
        break;
      default:
        ASSERT_TRUE(false);
    }
  }

 private:
  void RecordCall(EventType type, std::unique_ptr<Event> event) {
    calls_[type].push_back(std::move(event));
  }

  std::map<EventType, std::vector<std::unique_ptr<Event>>> calls_;
};

class FakeLoggerFactoryImpl : public fuchsia::cobalt::LoggerFactory {
 public:
  FakeLoggerFactoryImpl() {}

  void CreateLogger(fuchsia::cobalt::ProjectProfile profile,
                    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
                    CreateLoggerCallback callback) override {
    logger_.reset(new FakeLoggerImpl());
    logger_bindings_.AddBinding(logger_.get(), std::move(request));
    callback(fuchsia::cobalt::Status::OK);
  }

  void CreateLoggerSimple(
      fuchsia::cobalt::ProjectProfile profile,
      fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
      CreateLoggerSimpleCallback callback) override {
    callback(fuchsia::cobalt::Status::OK);
  }

  FakeLoggerImpl* logger() { return logger_.get(); }

 private:
  std::unique_ptr<FakeLoggerImpl> logger_;
  fidl::BindingSet<fuchsia::cobalt::Logger> logger_bindings_;
};

class CobaltLoggerTest : public gtest::TestLoopFixture {
 public:
  CobaltLoggerTest() : context_(InitStartupContext()) {}
  ~CobaltLoggerTest() override {}

  component::StartupContext* context() { return context_.get(); }

  FakeLoggerImpl* logger() { return factory_impl_->logger(); }

  CobaltLogger* cobalt_logger() { return cobalt_logger_.get(); }

 private:
  std::unique_ptr<component::StartupContext> InitStartupContext() {
    factory_impl_.reset(new FakeLoggerFactoryImpl());
    service_provider.AddService<fuchsia::cobalt::LoggerFactory>(
        [this](fidl::InterfaceRequest<fuchsia::cobalt::LoggerFactory> request) {
          factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
        });
    service_provider.AddService<fuchsia::sys::Environment>(
        [this](fidl::InterfaceRequest<fuchsia::sys::Environment> request) {
          app_environment_request_ = std::move(request);
        });
    service_provider.AddService<fuchsia::sys::Launcher>(
        [this](fidl::InterfaceRequest<fuchsia::sys::Launcher> request) {
          launcher_request_ = std::move(request);
        });
    return std::make_unique<component::StartupContext>(
        service_provider.OpenAsDirectory(), zx::channel());
  }

  virtual void SetUp() override {
    fsl::SizedVmo fake_cobalt_config;
    ASSERT_TRUE(fsl::VmoFromString(kFakeCobaltConfig, &fake_cobalt_config));
    fuchsia::cobalt::ProjectProfile profile;
    profile.config = std::move(fake_cobalt_config).ToTransport();
    cobalt_logger_ = NewCobaltLogger(async_get_default_dispatcher(), context(),
                                     std::move(profile));
    RunLoopUntilIdle();
  }

  component::ServiceProviderBridge service_provider;
  std::unique_ptr<FakeLoggerFactoryImpl> factory_impl_;
  std::unique_ptr<FakeLoggerImpl> logger_;
  std::unique_ptr<component::StartupContext> context_;
  std::unique_ptr<CobaltLogger> cobalt_logger_;
  fidl::BindingSet<fuchsia::cobalt::LoggerFactory> factory_bindings_;
  fidl::InterfaceRequest<fuchsia::sys::Launcher> launcher_request_;
  fidl::InterfaceRequest<fuchsia::sys::Environment> app_environment_request_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltLoggerTest);
};

TEST_F(CobaltLoggerTest, InitializeCobalt) {
  EXPECT_NE(cobalt_logger(), nullptr);
}

TEST_F(CobaltLoggerTest, LogEvent) {
  OccurrenceEvent event(kFakeCobaltMetricId, 123);
  cobalt_logger()->LogEvent(event.metric_id(), event.event_type_index());
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::EVENT_OCCURRED, &event);
}

TEST_F(CobaltLoggerTest, LogEventCount) {
  CountEvent event(kFakeCobaltMetricId, 123, "some_component", 2, 321);
  cobalt_logger()->LogEventCount(
      event.metric_id(), event.event_type_index(), event.component(),
      zx::duration(event.period_duration_micros() * ZX_USEC(1)), event.count());
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::EVENT_COUNT, &event);
}

TEST_F(CobaltLoggerTest, LogElapsedTime) {
  ElapsedTimeEvent event(kFakeCobaltMetricId, 123, "some_component", 321);
  cobalt_logger()->LogElapsedTime(
      event.metric_id(), event.event_type_index(), event.component(),
      zx::duration(event.elapsed_micros() * ZX_USEC(1)));
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::ELAPSED_TIME, &event);
}

TEST_F(CobaltLoggerTest, LogFrameRate) {
  FrameRateEvent event(kFakeCobaltMetricId, 123, "some_component", 321.5f);
  cobalt_logger()->LogFrameRate(event.metric_id(), event.event_type_index(),
                                event.component(), event.fps());
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::FRAME_RATE, &event);
}

TEST_F(CobaltLoggerTest, LogMemoryUsage) {
  MemoryUsageEvent event(kFakeCobaltMetricId, 123, "some_component", 534582);
  cobalt_logger()->LogMemoryUsage(event.metric_id(), event.event_type_index(),
                                  event.component(), event.bytes());
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::MEMORY_USAGE, &event);
}

TEST_F(CobaltLoggerTest, LogString) {
  StringUsedEvent event(kFakeCobaltMetricId, "some_string");
  cobalt_logger()->LogString(event.metric_id(), event.s());
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::STRING_USED, &event);
}

TEST_F(CobaltLoggerTest, StartTimer) {
  zx::time timestamp = zx::clock::get_monotonic();
  StartTimerEvent event(kFakeCobaltMetricId, 123, "some_component", "timer_1",
                        timestamp.get() / ZX_USEC(1), 3);
  cobalt_logger()->StartTimer(event.metric_id(), event.event_type_index(),
                              event.component(), event.timer_id(), timestamp,
                              zx::duration(event.timeout_s() * ZX_SEC(1)));
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::START_TIMER, &event);
}

TEST_F(CobaltLoggerTest, EndTimer) {
  zx::time timestamp = zx::clock::get_monotonic();
  EndTimerEvent event("timer_1", timestamp.get() / ZX_USEC(1), 3);
  cobalt_logger()->EndTimer(event.timer_id(), timestamp,
                            zx::duration(event.timeout_s() * ZX_SEC(1)));
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::END_TIMER, &event);
}

TEST_F(CobaltLoggerTest, LogIntHistogram) {
  fidl::VectorPtr<fuchsia::cobalt::HistogramBucket> histogram(1);
  histogram->at(0).index = 1;
  histogram->at(0).count = 234;

  fidl::VectorPtr<fuchsia::cobalt::HistogramBucket> histogram_clone;
  ASSERT_EQ(ZX_OK, fidl::Clone(histogram, &histogram_clone));

  IntHistogramEvent event(kFakeCobaltMetricId, 123, "some_component",
                          std::move(histogram_clone));
  cobalt_logger()->LogIntHistogram(event.metric_id(), event.event_type_index(),
                                   event.component(), histogram.take());
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::INT_HISTOGRAM, &event);
}

TEST_F(CobaltLoggerTest, LogCustomEvent) {
  fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> event_values(1);
  event_values->at(0).dimension_name = "some_dimension";
  event_values->at(0).value.set_int_value(234);

  fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> event_values_clone;
  ASSERT_EQ(ZX_OK, fidl::Clone(event_values, &event_values_clone));

  CustomEvent event(kFakeCobaltMetricId, std::move(event_values_clone));
  cobalt_logger()->LogCustomEvent(event.metric_id(), event_values.take());
  RunLoopUntilIdle();
  logger()->ExpectCalledOnceWith(EventType::CUSTOM, &event);
}

}  // namespace
}  // namespace cobalt
