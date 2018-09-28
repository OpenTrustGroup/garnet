// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/player_impl.h"

#include <sstream>

#include <fs/pseudo-file.h>
#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediaplayer/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>

#include "garnet/bin/mediaplayer/core/demux_source_segment.h"
#include "garnet/bin/mediaplayer/core/renderer_sink_segment.h"
#include "garnet/bin/mediaplayer/demux/fidl_reader.h"
#include "garnet/bin/mediaplayer/demux/file_reader.h"
#include "garnet/bin/mediaplayer/demux/http_reader.h"
#include "garnet/bin/mediaplayer/demux/reader_cache.h"
#include "garnet/bin/mediaplayer/fidl/fidl_type_conversions.h"
#include "garnet/bin/mediaplayer/graph/formatting.h"
#include "garnet/bin/mediaplayer/render/fidl_audio_renderer.h"
#include "garnet/bin/mediaplayer/render/fidl_video_renderer.h"
#include "garnet/bin/mediaplayer/util/safe_clone.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/type_converter.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/type_converters.h"

namespace media_player {
namespace {

static const char* kDumpEntry = "dump";

}  // namespace

// static
std::unique_ptr<PlayerImpl> PlayerImpl::Create(
    fidl::InterfaceRequest<fuchsia::mediaplayer::Player> request,
    component::StartupContext* startup_context, fit::closure quit_callback) {
  return std::make_unique<PlayerImpl>(std::move(request), startup_context,
                                      std::move(quit_callback));
}

PlayerImpl::PlayerImpl(
    fidl::InterfaceRequest<fuchsia::mediaplayer::Player> request,
    component::StartupContext* startup_context, fit::closure quit_callback)
    : dispatcher_(async_get_default_dispatcher()),
      startup_context_(startup_context),
      quit_callback_(std::move(quit_callback)),
      core_(dispatcher_) {
  FXL_DCHECK(request);
  FXL_DCHECK(startup_context_);
  FXL_DCHECK(quit_callback_);

  demux_factory_ = DemuxFactory::Create(startup_context_);
  FXL_DCHECK(demux_factory_);
  decoder_factory_ = DecoderFactory::Create(startup_context_);
  FXL_DCHECK(decoder_factory_);

  startup_context_->outgoing().debug_dir()->AddEntry(
      kDumpEntry,
      fbl::AdoptRef(new fs::BufferedPseudoFile([this](fbl::String* out) {
        std::ostringstream os;

        os << fostr::NewLine
           << "duration:           " << AsNs(status_.duration_ns);

        if (status_.metadata) {
          for (auto& property : *status_.metadata->properties) {
            os << fostr::NewLine << property.label << ": " << property.value;
          }
        }

        os << fostr::NewLine << "state:              " << ToString(state_);
        if (state_ == State::kWaiting) {
          os << " " << waiting_reason_;
        }

        if (target_state_ != state_) {
          os << fostr::NewLine
             << "transitioning to:   " << ToString(target_state_);
        }

        if (target_position_ != fuchsia::media::NO_TIMESTAMP) {
          os << fostr::NewLine
             << "pending seek to:    " << AsNs(target_position_);
        }

        core_.Dump(os << std::boolalpha);
        os << "\n";
        *out = os.str();
        return ZX_OK;
      })));

  UpdateStatus();
  AddBinding(std::move(request));

  bindings_.set_empty_set_handler([this]() { quit_callback_(); });

  core_.SetUpdateCallback([this]() {
    SendStatusUpdates();
    Update();
  });

  state_ = State::kInactive;
}

PlayerImpl::~PlayerImpl() {
  core_.SetUpdateCallback(nullptr);

  if (video_renderer_) {
    video_renderer_->SetGeometryUpdateCallback(nullptr);
  }
}

void PlayerImpl::MaybeCreateRenderer(StreamType::Medium medium) {
  if (core_.has_sink_segment(medium)) {
    // Renderer already exists.
    return;
  }

  switch (medium) {
    case StreamType::Medium::kAudio:
      if (!audio_renderer_) {
        auto audio = startup_context_
                         ->ConnectToEnvironmentService<fuchsia::media::Audio>();
        fuchsia::media::AudioRendererPtr audio_renderer;
        audio->CreateAudioRenderer(audio_renderer.NewRequest());
        audio_renderer_ = FidlAudioRenderer::Create(std::move(audio_renderer));
        core_.SetSinkSegment(RendererSinkSegment::Create(
                                 audio_renderer_, decoder_factory_.get()),
                             medium);
      }
      break;
    case StreamType::Medium::kVideo:
      if (!video_renderer_) {
        video_renderer_ = FidlVideoRenderer::Create();
        video_renderer_->SetGeometryUpdateCallback(
            [this]() { SendStatusUpdates(); });

        core_.SetSinkSegment(RendererSinkSegment::Create(
                                 video_renderer_, decoder_factory_.get()),
                             medium);
      }
      break;
    default:
      FXL_DCHECK(false) << "Only audio and video are currently supported";
      break;
  }
}

void PlayerImpl::Update() {
  // This method is called whenever we might want to take action based on the
  // current state and recent events. The current state is in |state_|. Recent
  // events are recorded in |target_state_|, which indicates what state we'd
  // like to transition to, |target_position_|, which can indicate a position
  // we'd like to stream to, and |core_.end_of_stream()| which tells us we've
  // reached end of stream.
  //
  // The states are as follows:
  //
  // |kInactive|- Indicates that we have no reader.
  // |kWaiting| - Indicates that we've done something asynchronous, and no
  //              further action should be taken by the state machine until that
  //              something completes (at which point the callback will change
  //              the state and call |Update|).
  // |kFlushed| - Indicates that presentation time is not progressing and that
  //              the pipeline is not primed with packets. This is the initial
  //              state and the state we transition to in preparation for
  //              seeking. A seek is currently only done when when the pipeline
  //              is clear of packets.
  // |kPrimed| -  Indicates that presentation time is not progressing and that
  //              the pipeline is primed with packets. We transition to this
  //              state when the client calls |Pause|, either from |kFlushed| or
  //              |kPlaying| state.
  // |kPlaying| - Indicates that presentation time is progressing and there are
  //              packets in the pipeline. We transition to this state when the
  //              client calls |Play|. If we're in |kFlushed| when |Play| is
  //              called, we transition through |kPrimed| state.
  //
  // The while loop that surrounds all the logic below is there because, after
  // taking some action and transitioning to a new state, we may want to check
  // to see if there's more to do in the new state. You'll also notice that
  // the callback lambdas generally call |Update|.
  while (true) {
    switch (state_) {
      case State::kInactive:
        if (setting_reader_) {
          // Need to set the reader. |FinishSetReader| will set the reader and
          // post another call to |Update|.
          FinishSetReader();
        }
        return;

      case State::kFlushed:
        if (setting_reader_) {
          // We have a new reader. Get rid of the current reader and transition
          // to inactive state. From there, we'll set up the new reader.
          core_.SetSourceSegment(nullptr, nullptr);
          state_ = State::kInactive;
          break;
        }

        // Presentation time is not progressing, and the pipeline is clear of
        // packets.
        if (target_position_ != fuchsia::media::NO_TIMESTAMP) {
          // We want to seek. Enter |kWaiting| state until the operation is
          // complete.
          state_ = State::kWaiting;
          waiting_reason_ = "for renderers to stop progressing prior to seek";

          // Capture the target position and clear it. If we get another
          // seek request while setting the timeline transform and and
          // seeking the source, we'll notice that and do those things
          // again.
          int64_t target_position = target_position_;
          target_position_ = fuchsia::media::NO_TIMESTAMP;

          // |program_range_min_pts_| will be delivered in the
          // |SetProgramRange| call, ensuring that the renderers discard
          // packets with PTS values less than the target position.
          // |transform_subject_time_| is used when setting the timeline.
          transform_subject_time_ = target_position;
          program_range_min_pts_ = target_position;

          SetTimelineFunction(
              0.0f, media::Timeline::local_now(), [this, target_position]() {
                if (target_position_ == target_position) {
                  // We've had a rendundant seek request. Ignore it.
                  target_position_ = fuchsia::media::NO_TIMESTAMP;
                } else if (target_position_ != fuchsia::media::NO_TIMESTAMP) {
                  // We've had a seek request to a new position. Refrain from
                  // seeking the source and re-enter this sequence.
                  state_ = State::kFlushed;
                  Update();
                  return;
                }

                // Seek to the new position.
                core_.Seek(target_position, [this]() {
                  state_ = State::kFlushed;
                  Update();
                });
              });

          // Done for now. We're in kWaiting, and the callback will call
          // Update when the Seek call is complete.
          return;
        }

        if (target_state_ == State::kPlaying ||
            target_state_ == State::kPrimed) {
          // We want to transition to |kPrimed| or to |kPlaying|, for which
          // |kPrimed| is a prerequisite. We enter |kWaiting| state, issue the
          // |SetProgramRange| and |Prime| requests and transition to |kPrimed|
          // when the operation is complete.
          state_ = State::kWaiting;
          waiting_reason_ = "for priming to complete";
          core_.SetProgramRange(0, program_range_min_pts_, kMaxTime);

          core_.Prime([this]() {
            state_ = State::kPrimed;
            Update();
          });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the prime is complete.
          return;
        }

        // No interesting events to respond to. Done for now.
        return;

      case State::kPrimed:
        // Presentation time is not progressing, and the pipeline is primed with
        // packets.
        if (NeedToFlush()) {
          // Either we have a new reader, want to seek, or we otherwise want to
          // flush.
          state_ = State::kWaiting;
          waiting_reason_ = "for flushing to complete";

          core_.Flush(ShouldHoldFrame(), [this]() {
            state_ = State::kFlushed;
            Update();
          });

          break;
        }

        if (target_state_ == State::kPlaying) {
          // We want to transition to |kPlaying|. Enter |kWaiting|, start the
          // presentation timeline and transition to |kPlaying| when the
          // operation completes.
          state_ = State::kWaiting;
          waiting_reason_ = "for renderers to start progressing";
          SetTimelineFunction(
              1.0f, media::Timeline::local_now() + kMinimumLeadTime, [this]() {
                state_ = State::kPlaying;
                Update();
              });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the flush is complete.
          return;
        }

        // No interesting events to respond to. Done for now.
        return;

      case State::kPlaying:
        // Presentation time is progressing, and packets are moving through
        // the pipeline.
        if (NeedToFlush() || target_state_ == State::kPrimed) {
          // Either we have a new reader, we want to seek or we want to stop
          // playback. In any case, we need to enter |kWaiting|, stop the
          // presentation timeline and transition to |kPrimed| when the
          // operation completes.
          state_ = State::kWaiting;
          waiting_reason_ = "for renderers to stop progressing";
          SetTimelineFunction(
              0.0f, media::Timeline::local_now() + kMinimumLeadTime, [this]() {
                state_ = State::kPrimed;
                Update();
              });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the timeline is set.
          return;
        }

        if (core_.end_of_stream()) {
          // We've reached end of stream. The presentation timeline stops by
          // itself, so we just need to transition to |kPrimed|.
          target_state_ = State::kPrimed;
          state_ = State::kPrimed;
          // Loop around to check if there's more work to do.
          break;
        }

        // No interesting events to respond to. Done for now.
        return;

      case State::kWaiting:
        // Waiting for some async operation. Nothing to do until it completes.
        return;
    }
  }
}

void PlayerImpl::SetTimelineFunction(float rate, int64_t reference_time,
                                     fit::closure callback) {
  core_.SetTimelineFunction(
      media::TimelineFunction(transform_subject_time_, reference_time,
                              media::TimelineRate(rate)),
      std::move(callback));
  transform_subject_time_ = fuchsia::media::NO_TIMESTAMP;
  SendStatusUpdates();
}

void PlayerImpl::SetHttpSource(
    fidl::StringPtr http_url,
    fidl::VectorPtr<fuchsia::net::oldhttp::HttpHeader> headers) {
  BeginSetReader(
      HttpReader::Create(startup_context_, http_url, std::move(headers)));
}

void PlayerImpl::SetFileSource(zx::channel file_channel) {
  BeginSetReader(FileReader::Create(std::move(file_channel)));
}

void PlayerImpl::SetReaderSource(
    fidl::InterfaceHandle<fuchsia::mediaplayer::SeekingReader> reader_handle) {
  if (!reader_handle) {
    BeginSetReader(nullptr);
    return;
  }

  BeginSetReader(FidlReader::Create(reader_handle.Bind()));
}

void PlayerImpl::BeginSetReader(std::shared_ptr<Reader> reader) {
  // Note the pending reader change and advance the state machine. When the
  // old reader (if any) is shut down, the state machine will call
  // |FinishSetReader|.
  setting_reader_ = true;
  new_reader_ = reader;
  target_position_ = 0;
  async::PostTask(dispatcher_, [this]() { Update(); });
}

void PlayerImpl::FinishSetReader() {
  FXL_DCHECK(setting_reader_);
  FXL_DCHECK(state_ == State::kInactive);
  FXL_DCHECK(!core_.has_source_segment());

  setting_reader_ = false;

  if (!new_reader_) {
    // We were asked to clear the reader, which was already done by the state
    // machine. We're done.
    return;
  }

  state_ = State::kWaiting;
  waiting_reason_ = "for the source to initialize";
  program_range_min_pts_ = 0;
  transform_subject_time_ = 0;

  MaybeCreateRenderer(StreamType::Medium::kAudio);

  std::shared_ptr<Demux> demux;
  demux_factory_->CreateDemux(ReaderCache::Create(new_reader_), &demux);
  // TODO(dalesat): Handle CreateDemux failure.
  FXL_DCHECK(demux);

  new_reader_ = nullptr;

  core_.SetSourceSegment(DemuxSourceSegment::Create(demux), [this]() {
    state_ = State::kFlushed;
    SendStatusUpdates();
    Update();
  });
}

void PlayerImpl::Play() {
  target_state_ = State::kPlaying;
  Update();
}

void PlayerImpl::Pause() {
  target_state_ = State::kPrimed;
  Update();
}

void PlayerImpl::Seek(int64_t position) {
  target_position_ = position;
  Update();
}

void PlayerImpl::CreateView(
    fidl::InterfaceHandle<::fuchsia::ui::viewsv1::ViewManager> view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request) {
  MaybeCreateRenderer(StreamType::Medium::kVideo);
  if (!video_renderer_) {
    return;
  }

  video_renderer_->CreateView(view_manager.Bind(),
                              std::move(view_owner_request));
}

void PlayerImpl::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::GainControl> gain_control_request) {
  if (!audio_renderer_) {
    MaybeCreateRenderer(StreamType::Medium::kAudio);
  }

  FXL_DCHECK(audio_renderer_);
  audio_renderer_->BindGainControl(std::move(gain_control_request));
}

void PlayerImpl::SetAudioRenderer(
    fidl::InterfaceHandle<fuchsia::media::AudioRenderer> audio_renderer) {
  if (audio_renderer_) {
    return;
  }

  audio_renderer_ = FidlAudioRenderer::Create(audio_renderer.Bind());

  core_.SetSinkSegment(
      RendererSinkSegment::Create(audio_renderer_, decoder_factory_.get()),
      StreamType::Medium::kAudio);
}

void PlayerImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::mediaplayer::Player> request) {
  FXL_DCHECK(request);
  bindings_.AddBinding(this, std::move(request));

  // Fire |OnStatusChanged| event for the new client.
  bindings_.bindings().back()->events().OnStatusChanged(fidl::Clone(status_));
}

void PlayerImpl::SendStatusUpdates() {
  UpdateStatus();

  for (auto& binding : bindings_.bindings()) {
    binding->events().OnStatusChanged(fidl::Clone(status_));
  }
}

void PlayerImpl::UpdateStatus() {
  status_.timeline_function =
      fidl::MakeOptional(fxl::To<fuchsia::mediaplayer::TimelineFunction>(
          core_.timeline_function()));
  status_.end_of_stream = core_.end_of_stream();
  status_.has_audio = core_.content_has_medium(StreamType::Medium::kAudio);
  status_.has_video = core_.content_has_medium(StreamType::Medium::kVideo);
  status_.audio_connected = core_.medium_connected(StreamType::Medium::kAudio);
  status_.video_connected = core_.medium_connected(StreamType::Medium::kVideo);

  status_.duration_ns = core_.duration_ns();

  auto metadata = core_.metadata();
  status_.metadata =
      metadata ? fidl::MakeOptional(
                     fxl::To<fuchsia::mediaplayer::Metadata>(*metadata))
               : nullptr;

  if (video_renderer_) {
    status_.video_size = SafeClone(video_renderer_->video_size());
    status_.pixel_aspect_ratio =
        SafeClone(video_renderer_->pixel_aspect_ratio());
  }

  status_.problem = SafeClone(core_.problem());
}

// static
const char* PlayerImpl::ToString(State value) {
  switch (value) {
    case State::kInactive:
      return "inactive";
    case State::kWaiting:
      return "waiting";
    case State::kFlushed:
      return "flushed";
    case State::kPrimed:
      return "primed";
    case State::kPlaying:
      return "playing";
  }

  return "ILLEGAL STATE VALUE";
}

}  // namespace media_player
