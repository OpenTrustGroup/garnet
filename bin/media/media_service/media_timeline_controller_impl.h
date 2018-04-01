// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include <fuchsia/cpp/media.h>
#include "garnet/bin/media/media_service/media_component_factory.h"
#include "garnet/bin/media/util/callback_joiner.h"
#include "garnet/bin/media/util/fidl_publisher.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {

// Fidl agent that controls timing in a graph.
class MediaTimelineControllerImpl
    : public MediaComponentFactory::Product<MediaTimelineController>,
      public MediaTimelineController,
      public MediaTimelineControlPoint,
      public TimelineConsumer {
 public:
  static std::shared_ptr<MediaTimelineControllerImpl> Create(
      fidl::InterfaceRequest<MediaTimelineController> request,
      MediaComponentFactory* owner);

  ~MediaTimelineControllerImpl() override;

  // MediaTimelineController implementation.
  void AddControlPoint(
      fidl::InterfaceHandle<MediaTimelineControlPoint> control_point) override;

  void GetControlPoint(
      fidl::InterfaceRequest<MediaTimelineControlPoint> control_point) override;

  // MediaTimelineControlPoint implementation.
  void GetStatus(uint64_t version_last_seen,
                 GetStatusCallback callback) override;

  void GetTimelineConsumer(
      fidl::InterfaceRequest<TimelineConsumer> timeline_consumer) override;

  void SetProgramRange(uint64_t program,
                       int64_t min_pts,
                       int64_t max_pts) override;

  void Prime(PrimeCallback callback) override;

  // TimelineConsumer implementation.
  void SetTimelineTransform(TimelineTransform timeline_transform,
                            SetTimelineTransformCallback callback) override;

  void SetTimelineTransformNoReply(
      TimelineTransform timeline_transform) override;

 private:
  static constexpr int64_t kDefaultLeadTime = Timeline::ns_from_ms(30);

  // Relationship to subordinate control point.
  struct ControlPointState {
    ControlPointState(MediaTimelineControllerImpl* parent,
                      MediaTimelineControlPointPtr control_point);

    ~ControlPointState();

    void HandleStatusUpdates(uint64_t version = kInitialStatus,
                             MediaTimelineControlPointStatus* status = nullptr);

    MediaTimelineControllerImpl* parent_;
    MediaTimelineControlPointPtr control_point_;
    TimelineConsumerPtr consumer_;
    bool end_of_stream_ = false;
  };

  class TimelineTransition
      : public std::enable_shared_from_this<TimelineTransition> {
   public:
    TimelineTransition(int64_t subject_time,
                       int64_t reference_time,
                       uint32_t subject_delta,
                       uint32_t reference_delta,
                       SetTimelineTransformCallback callback);

    ~TimelineTransition();

    // Returns a new callback for a child (control point) transition. THIS
    // METHOD WILL ONLY WORK IF THERE IS ALREADY A SHARED POINTER TO THIS
    // OBJECT.
    std::function<void(bool)> NewCallback() {
      callback_joiner_.Spawn();

      std::shared_ptr<TimelineTransition> this_ptr = shared_from_this();
      FXL_DCHECK(!this_ptr.unique());

      return
          [this_ptr](bool completed) {
            FXL_DCHECK(this_ptr);
            if (!completed && !this_ptr->cancelled_) {
              FXL_LOG(WARNING)
                  << "A control point transition was cancelled unexpectedly.";
            }
            this_ptr->callback_joiner_.Complete();
          };
    }

    // Cancels this transition.
    void Cancel() {
      FXL_DCHECK(!cancelled_);
      cancelled_ = true;
      if (callback_) {
        callback_(false);
        callback_ = nullptr;
      }
      completed_callback_ = nullptr;
    }

    // Specifies a callback to be called if and when the transition is complete.
    // The callback will never be called if the transition is cancelled.
    void WhenCompleted(const std::function<void()>& completed_callback) {
      FXL_DCHECK(!completed_callback_);
      if (!callback_ && !cancelled_) {
        completed_callback();
      } else {
        completed_callback_ = completed_callback;
      }
    }

    // Returns the TimelineFunction that will result from this transition.
    const TimelineFunction& new_timeline_function() const {
      return new_timeline_function_;
    }

   private:
    TimelineFunction new_timeline_function_;
    SetTimelineTransformCallback callback_;
    CallbackJoiner callback_joiner_;
    bool cancelled_ = false;
    std::function<void()> completed_callback_;
  };

  MediaTimelineControllerImpl(
      fidl::InterfaceRequest<MediaTimelineController> request,
      MediaComponentFactory* owner);

  // Takes action when a control point changes its end-of-stream value.
  void HandleControlPointEndOfStreamChange();

  fidl::Binding<MediaTimelineControlPoint> control_point_binding_;
  fidl::Binding<TimelineConsumer> consumer_binding_;
  FidlPublisher<GetStatusCallback> status_publisher_;
  std::vector<std::unique_ptr<ControlPointState>> control_point_states_;
  TimelineFunction current_timeline_function_;
  bool end_of_stream_ = false;
  std::weak_ptr<TimelineTransition> pending_transition_;
};

}  // namespace media
