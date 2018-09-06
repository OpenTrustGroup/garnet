// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frame_sink_view.h"

#include "frame_sink.h"
#include "util.h"

#include <lib/fxl/logging.h>
#include <lib/ui/scenic/cpp/commands.h>

#include <iomanip>
#include <memory>

namespace {

constexpr uint32_t kShapeWidth = 640;
constexpr uint32_t kShapeHeight = 480;
constexpr float kDisplayHeight = 50;
constexpr float kInitialWindowXPos = 320;
constexpr float kInitialWindowYPos = 240;

// Context for a frame's async lifetime.  When the presentation wait is done,
// the WaitHandler deletes this.
//
// As with async::WaitMethod, this relies on all this code running on the
// dispatcher's single thread.
class Frame {
 public:
  Frame(async_dispatcher_t* dispatcher, ::zx::eventpair& release_eventpair,
        fit::closure on_done)
      : wait_(this), on_done_(std::move(on_done)) {
    zx_status_t status =
        release_eventpair.duplicate(ZX_RIGHT_SAME_RIGHTS, &release_eventpair_);
    if (status != ZX_OK) {
      FXL_LOG(FATAL) << "::zx::event::duplicate() failed - status: " << status;
      FXL_NOTREACHED();
    }
    wait_.set_object(release_eventpair_.get());
    // TODO(dustingreen): We should make it so an eventpair A B can have A wait
    // for B to be signalled without B's holder caring that it's an eventpair
    // instead of an event.
    //
    // TODO(dustingreen): Have ImagePipe accept eventpair as well as event, or
    // instead of event.  Maybe have it signal the peer instead of signalling
    // its end, or maybe signal both ends.
    //
    // For now we use the other end getting closed instead of the other end
    // being signalled, so we can more easily move on if Scenic dies.
    wait_.set_trigger(ZX_EVENTPAIR_PEER_CLOSED);
    status = wait_.Begin(dispatcher);
    if (status != ZX_OK) {
      FXL_LOG(FATAL) << "Begin() failed - status: " << status;
      FXL_NOTREACHED();
    }
  }

  ~Frame() {
    // on_done_ was already run prior to ~Frame, and set to nullptr.
    FXL_DCHECK(!on_done_);
  }

  // Normally this runs when the remote end of the eventpair is closed.  In
  // addition, when the dispatcher shuts down, if this Frame is still waiting,
  // this callback runs and is passed status ZX_ERR_CANCELED.
  void WaitHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                   zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      if (status == ZX_ERR_CANCELED) {
        // Probably normal if this is not happening until dispatcher shutdown.
        //
        // TODO(dustingreen): Mute this output if |dispatcher| is shutting down.
        FXL_LOG(INFO)
            << "WaitHandler() sees ZX_ERR_CANCELED (normal if shutting down)";
      } else {
        FXL_LOG(INFO) << "WaitHandler() sees failure status: " << status;
      }
    }

    // Regardless of status or signal, this frame is done.  The callee here
    // knows nothing about |Packet|, so cannot delete |this|.
    on_done_();
    on_done_ = nullptr;

    // If the handler is called it means the handler needs to delete the frame.
    // This applies even if the status is ZX_ERR_CANCELED because that'll only
    // happen if the dispatcher is shutting down, not if a canceller manually
    // cancels (which we don't do currently anyway).
    delete this;
  }

  ::zx::eventpair release_eventpair_;
  async::WaitMethod<Frame, &Frame::WaitHandler> wait_;
  fit::closure on_done_;
};

}  // namespace

std::unique_ptr<FrameSinkView> FrameSinkView::Create(
    FrameSink* parent, async::Loop* main_loop,
    ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request) {
  return std::unique_ptr<FrameSinkView>(
      new FrameSinkView(parent, main_loop, std::move(view_manager),
                        std::move(view_owner_request)));
}

FrameSinkView::~FrameSinkView() { parent_->RemoveFrameSinkView(this); }

void FrameSinkView::PutFrame(
    uint32_t image_id, zx_time_t present_time, const zx::vmo& vmo,
    uint64_t vmo_offset,
    const fuchsia::mediacodec::VideoUncompressedFormat& video_format,
    fit::closure on_done) {
  FXL_DCHECK((image_id != FrameSink::kBlankFrameImageId) || !on_done);

  fuchsia::images::PixelFormat pixel_format =
      fuchsia::images::PixelFormat::BGRA_8;
  uint32_t fourcc = video_format.fourcc;
  switch (fourcc) {
    case make_fourcc('N', 'V', '1', '2'):
      pixel_format = fuchsia::images::PixelFormat::NV12;
      break;
    case make_fourcc('B', 'G', 'R', 'A'):
      pixel_format = fuchsia::images::PixelFormat::BGRA_8;
      break;
    default:
      FXL_CHECK(false) << "fourcc conversion not implemented - fourcc: "
                       << fourcc_to_string(fourcc) << " in hex: 0x" << std::hex
                       << std::setw(8) << fourcc;
      FXL_NOTREACHED();
      pixel_format = fuchsia::images::PixelFormat::BGRA_8;
      break;
  }
  fuchsia::images::ImageInfo image_info{
      .width = video_format.primary_width_pixels,
      .height = video_format.primary_height_pixels,
      .stride = video_format.primary_line_stride_bytes,
      .pixel_format = pixel_format,
  };

  FXL_VLOG(3) << "#### image_id: " << image_id << " width: " << image_info.width
              << " height: " << image_info.height
              << " stride: " << image_info.stride;

  ::zx::vmo image_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &image_vmo);
  if (status != ZX_OK) {
    FXL_LOG(FATAL) << "vmo.duplicate() failed - status: " << status;
    FXL_NOTREACHED();
  }

  image_pipe_->AddImage(image_id, image_info, std::move(image_vmo),
                        fuchsia::images::MemoryType::HOST_MEMORY, vmo_offset);

  ::zx::eventpair release_frame_client;
  ::zx::eventpair release_frame_server;
  status =
      ::zx::eventpair::create(0, &release_frame_client, &release_frame_server);
  if (status != ZX_OK) {
    FXL_LOG(FATAL) << "::zx::eventpair::create() failed";
    FXL_NOTREACHED();
  }

  // TODO(dustingreen): Stop doing this, or rather, do use eventpair(s) but stop
  // needing to force-cast them to "event" like this, by changing ImagePipe
  // interface to take eventpair instead.  Specifically, a release fence that's
  // just an event hampers detection when Scenic dies, since Scenic dying
  // doesn't set the release fence. What we're doing here for now is forcing an
  // eventpair to be treated as an event, and then relying on the eventpair
  // being closed by Scenic (a short while after it's signalled by Scenic, but
  // we don't notice the signalling because by then we've closed the handle
  // under release_frame_server).  The alternative of just using an event and
  // keeping the event handle locally and trying to notice Scenic dying via
  // other means seems to have some issues around not necessarily knowing that
  // everything under Scenic is really done with the frame yet.  The alternative
  // of cloning the VMO per frame and noticing when all the clones are gone is
  // perhaps workable, but doesn't seem likely to be as efficient as using
  // eventpair, and it might not work for all types of VMOs (at least at the
  // moment).
  ::zx::event release_frame_server_hack(release_frame_server.release());

  // There is no expectation that ~frame would be run from this method, but
  // avoid just having a raw pointer since that may be considered harmful-ish.
  //
  // If ~frame were to run, we rely on ~frame being able to cancel the frame's
  // wait without the frame's handler running yet, which is among the ways in
  // which we're relying on running on the dispatcher's single thread here.
  auto on_done_wrapper = [this, image_id, on_done = std::move(on_done)] {
    if (image_id == FrameSink::kBlankFrameImageId) {
      // The image_pipe_ may already be gone, so don't touch ImagePipe.  There
      // is no on_done callback, so no worries re. not running it.
      FXL_DCHECK(!on_done);
      return;
    }
    // According to ImagePipe .fidl, this RemoveImage() doesn't impact the
    // present queue or the currently-displayed image.  However, it might
    // help avoid some complaining in the log from Scenic, for now, if we
    // wait this long, instead of removing earlier.
    image_pipe_->RemoveImage(image_id);
    on_done();
  };
  auto frame =
      std::make_unique<Frame>(main_loop_->dispatcher(), release_frame_client,
                              std::move(on_done_wrapper));

  // TODO(dustingreen): When release_frame_server_hack is gone, change these to
  // use "auto".  For now, we'll leave the types explicit to make the temp hack
  // as obvious as possible.
  ::fidl::VectorPtr<::zx::event> acquire_fences =
      ::fidl::VectorPtr<::zx::event>::New(0);
  ::fidl::VectorPtr<::zx::event> release_fences =
      ::fidl::VectorPtr<::zx::event>::New(0);
  release_fences.push_back(std::move(release_frame_server_hack));

  // For the moment we just display every frame asap.  This will hopefully tend
  // to show frames faster than real-time, and provide some general impression
  // of how fast they're decoding - it's fairly useless for determining the max
  // presentation frame rate - that's not the point here.
  image_pipe_->PresentImage(
      image_id, present_time, std::move(acquire_fences),
      std::move(release_fences),
      [image_id](fuchsia::images::PresentationInfo presentation_info) {
        FXL_VLOG(3) << "PresentImageCallback() called - presentation_time: "
                    << presentation_info.presentation_time
                    << " presenation_interval: "
                    << presentation_info.presentation_interval
                    << " image_id: " << image_id;
      });

  // The frame will self-delete when its wait is done, so don't ~frame here.
  frame.release();
}

FrameSinkView::FrameSinkView(
    FrameSink* parent, async::Loop* main_loop,
    ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request)
    : BaseView(std::move(view_manager), std::move(view_owner_request),
               "FrameSinkView"),
      parent_(parent),
      main_loop_(main_loop),
      node_(session()) {
  FXL_VLOG(3) << "Creating View";

  // Create an ImagePipe and use it.
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipeCmd(
      image_pipe_id, image_pipe_.NewRequest(main_loop_->dispatcher())));

  // Create a material that has our image pipe mapped onto it:
  scenic::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rectangle shape to display the YUV on.
  scenic::Rectangle shape(session(), kShapeWidth, kShapeHeight);

  node_.SetShape(shape);
  node_.SetMaterial(material);
  parent_node().AddChild(node_);

  // Translation of 0, 0 is the middle of the screen
  node_.SetTranslation(kInitialWindowXPos, kInitialWindowYPos, kDisplayHeight);
  InvalidateScene();

  parent_->AddFrameSinkView(this);
}

// From mozart::BaseView. Called when the scene is "invalidated".
void FrameSinkView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  float width = logical_size().width;
  float height = logical_size().height;
  scenic::Rectangle shape(session(), width, height);
  node_.SetShape(shape);
  float half_width = width * 0.5f;
  float half_height = height * 0.5f;
  node_.SetTranslation(half_width, half_height, kDisplayHeight);
}
