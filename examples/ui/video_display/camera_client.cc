// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <garnet/examples/ui/video_display/camera_client.h>

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <fdio/io.h>
#include <lib/fsl/tasks/message_loop.h>
#include <zircon/assert.h>
#include <zircon/device/audio.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zx/channel.h>
#include <zx/handle.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

namespace video_display {

#define CHECK_RESP_RESULT(_resp, _cmd_name)                                    \
  if (ZX_OK != _resp.result) {                                                 \
    FXL_LOG(ERROR) << _cmd_name << " failure (result: " << resp.result << ")"; \
    return _resp.result;                                                       \
  }

CameraClient::CameraClient() = default;

CameraClient::~CameraClient() {
  Close();
}

zx_status_t CameraClient::Open(uint32_t dev_id) {
  if (!IsClosed()) {
    FXL_LOG(ERROR) << "Bad State";
    return ZX_ERR_BAD_STATE;
  }
  if (stream_ch_.is_valid()) {
    FXL_LOG(ERROR) << "channel has already been opened!";
    return ZX_ERR_BAD_STATE;
  }

  char dev_path[64] = {0};
  snprintf(dev_path, sizeof(dev_path), "/dev/class/camera/%03u", dev_id);

  int fd = ::open(dev_path, O_RDONLY);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open \"" << dev_path << "\" (res " << fd
                   << ")";
    return ZX_ERR_IO;
  }

  ssize_t res = ::fdio_ioctl(fd, CAMERA_IOCTL_GET_CHANNEL, nullptr, 0,
                             &stream_ch_, sizeof(stream_ch_));
  ::close(fd);

  if (res != sizeof(stream_ch_)) {
    FXL_LOG(ERROR) << "Failed to obtain channel (res " << res << ")";
    return static_cast<zx_status_t>(res);
  }

  // Set up waiter to wait for messages on this channel:
  cmd_msg_waiter_.set_object(stream_ch_.get());
  cmd_msg_waiter_.set_trigger(ZX_CHANNEL_READABLE);
  zx_status_t status = cmd_msg_waiter_.Begin(async_get_default());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start AutoWaiter";
    return status;
  }
  SetConfigurationState(CameraState::Closed, CameraState::CommandChannelOpen);

  return ZX_OK;
}

zx_status_t CameraClient::GetSupportedFormats(
    GetFormatCallback get_formats_callback) {
  // Check the state.  This state check enforces a strict calling order for
  // camera configuration.  Technically, this call should be supported pretty
  // much any time, but currently we require GetSupportedFormats only be called
  // after the channel is open, and before the format is set.
  zx_status_t status = CheckConfigurationState(CameraState::CommandChannelOpen);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Bad State";
    return status;
  }
  if (!get_formats_callback) {
    return ZX_ERR_INVALID_ARGS;
  }
  get_formats_callback_ = fbl::move(get_formats_callback);

  camera_stream_cmd_get_formats_req req;
  req.hdr.cmd = CAMERA_STREAM_CMD_GET_FORMATS;
  zx_status_t write_status = stream_ch_.write(0, &req, sizeof(req), nullptr, 0);
  if (write_status != ZX_OK) {
    FXL_LOG(ERROR) << "Cmd write failure (cmd " << req.hdr.cmd << ", res "
                   << write_status << ")";
    return write_status;
  }
  SetConfigurationState(CameraState::CommandChannelOpen,
                        CameraState::FormatsRequested);
  return ZX_OK;
}

zx_status_t CameraClient::OnGetFormatsResp(
    camera::camera_proto::GetFormatsResp resp) {
  zx_status_t status = CheckConfigurationState(CameraState::FormatsRequested);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unexpected message response (cmd " << resp.hdr.cmd
                   << ", GetFormats)";
    return ZX_ERR_BAD_STATE;
  }

  // If we just began receiving formats:
  if (resp.already_sent_count == 0) {
    out_formats_.clear();
  }

  uint32_t expected_formats = resp.total_format_count;
  FXL_VLOG(3) << "expected_formats: " << expected_formats;
  if (!expected_formats) {
    // done grabbing formats
    SetConfigurationState(CameraState::FormatsRequested,
                          CameraState::FormatsReceived);
    zx_status_t ret = get_formats_callback_(out_formats_);
    get_formats_callback_ = nullptr;
    return ret;
  }

  if (out_formats_.size() == 0) {
    out_formats_.reserve(expected_formats);
  }

  // Check for out of order:
  if (out_formats_.size() != resp.already_sent_count) {
    FXL_LOG(ERROR) << "Bad format index while fetching formats (expected "
                   << out_formats_.size() << ", got " << resp.already_sent_count
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  // Calculate how many frames to grab.  If there are more than
  // CAMERA_STREAM_CMD_GET_FORMATS_MAX_FORMATS_PER_RESPONSE formats,
  // we will be getting multiple messages.  Each message, with the possible
  // exeption of the last message will have the max number of formats.
  // The last message will have (total messages) - (already received)
  // messages.
  uint32_t to_grab =
      fbl::min(static_cast<uint32_t>(expected_formats - out_formats_.size()),
               CAMERA_STREAM_CMD_GET_FORMATS_MAX_FORMATS_PER_RESPONSE);

  for (uint16_t i = 0; i < to_grab; ++i) {
    out_formats_.push_back(resp.formats[i]);
  }

  if (out_formats_.size() == expected_formats) {
    // done grabbing formats.
    FXL_VLOG(4)
        << "CameraClient::OnGetFormatsResp grabbed formats, calling callback";
    SetConfigurationState(CameraState::FormatsRequested,
                          CameraState::FormatsReceived);
    zx_status_t ret = get_formats_callback_(out_formats_);
    get_formats_callback_ = nullptr;
    return ret;
  }

  return ZX_OK;
}

zx_status_t CameraClient::SetFormat(const camera_video_format_t& format,
                                    SetFormatCallback set_format_callback) {
  zx_status_t status = CheckConfigurationState(CameraState::FormatsReceived);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Bad State";
    return status;
  }
  FXL_DCHECK(stream_ch_.is_valid() && !vb_ch_.is_valid())
      << "Channels in wrong state for SetFormat";

  if (!set_format_callback) {
    FXL_LOG(ERROR) << "set_format_callback is invalid";
    return ZX_ERR_INVALID_ARGS;
  }
  set_format_callback_ = fbl::move(set_format_callback);

  camera_stream_cmd_set_format_req_t req;
  req.hdr.cmd = CAMERA_STREAM_CMD_SET_FORMAT;
  req.video_format = format;
  zx_status_t write_status = stream_ch_.write(0, &req, sizeof(req), nullptr, 0);
  if (write_status != ZX_OK) {
    FXL_LOG(ERROR) << "Cmd write failure (cmd " << req.hdr.cmd << ", res "
                   << write_status << ")";
    return write_status;
  }
  SetConfigurationState(CameraState::FormatsReceived,
                        CameraState::SetFormatRequested);

  return ZX_OK;
}

zx_status_t CameraClient::OnSetFormatResp(
    camera::camera_proto::SetFormatResp resp,
    zx::channel resp_handle_out) {
  CHECK_RESP_RESULT(resp, "SetFormat");

  zx_status_t status = CheckConfigurationState(CameraState::SetFormatRequested);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unexpected message response (cmd " << resp.hdr.cmd
                   << ", SetFormat)";
    return ZX_ERR_BAD_STATE;
  }

  vb_ch_.reset(resp_handle_out.release());
  // Now that our buffer is recognized, set up our waiter on the buffer
  // channel:
  buff_msg_waiter_.set_object(vb_ch_.get());
  buff_msg_waiter_.set_trigger(ZX_CHANNEL_READABLE);
  status = buff_msg_waiter_.Begin(async_get_default());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start AutoWaiter";
    return status;
  }
  SetConfigurationState(CameraState::SetFormatRequested,
                        CameraState::SetFormatReceived);
  SetFormatCallback set_format_callback = fbl::move(set_format_callback_);
  return set_format_callback(resp.max_frame_size);
}

zx_status_t CameraClient::SetBuffer(const zx::vmo& buffer_vmo) {
  zx_status_t status = CheckConfigurationState(CameraState::SetFormatReceived);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Bad State";
    return status;
  }
  camera_vb_cmd_set_buffer_req_t req;
  req.hdr.cmd = CAMERA_VB_CMD_SET_BUFFER;
  zx_handle_t vmo_handle;
  zx_handle_duplicate(buffer_vmo.get(), ZX_RIGHT_SAME_RIGHTS, &vmo_handle);

  zx_status_t write_status = vb_ch_.write(0, &req, sizeof(req), &vmo_handle, 1);

  if (write_status != ZX_OK) {
    FXL_LOG(ERROR) << "Cmd write failure (cmd " << req.hdr.cmd << ", res "
                   << write_status << ")";
    return write_status;
  }
  SetConfigurationState(CameraState::SetFormatReceived,
                        CameraState::SetBufferRequested);
  return ZX_OK;
}

zx_status_t CameraClient::ReleaseFrame(uint64_t data_offset) {
  if (!IsStreaming() && !IsShuttingDown()) {
    FXL_LOG(ERROR) << "ReleaseFrame called while not streaming.";
    return ZX_ERR_BAD_STATE;
  }
  if (!vb_ch_.is_valid()) {
    FXL_LOG(ERROR) << "ReleaseFrame called without an open buffer channel";
    return ZX_ERR_BAD_STATE;
  }
  camera_vb_cmd_frame_release_req req;
  req.hdr.cmd = CAMERA_VB_CMD_FRAME_RELEASE;
  req.data_vb_offset = data_offset;

  zx_status_t write_status = vb_ch_.write(0, &req, sizeof(req), nullptr, 0);
  if (write_status != ZX_OK) {
    FXL_LOG(ERROR) << "Cmd write failure (cmd " << req.hdr.cmd << ", res "
                   << write_status << ")";
  }
  return write_status;
}

zx_status_t CameraClient::Start(FrameNotifyCallback frame_notify_callback) {
  zx_status_t status = CheckConfigurationState(CameraState::SetBufferRequested);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Bad State";
    return status;
  }
  if (!frame_notify_callback) {
    return ZX_ERR_INVALID_ARGS;
  }

  frame_notify_callback_ = fbl::move(frame_notify_callback);

  camera_vb_cmd_start_req_t req;
  req.hdr.cmd = CAMERA_VB_CMD_START;
  zx_status_t write_status = vb_ch_.write(0, &req, sizeof(req), nullptr, 0);
  if (write_status != ZX_OK) {
    FXL_LOG(ERROR) << "Cmd write failure (cmd " << req.hdr.cmd << ", res "
                   << write_status << ")";
    return write_status;
  }
  SetConfigurationState(CameraState::SetBufferRequested,
                        CameraState::StartRequested);
  return ZX_OK;
}

zx_status_t CameraClient::OnFrameNotify(
    camera::camera_proto::VideoBufFrameNotify resp) {
  if (!IsStreaming() && !IsShuttingDown()) {
    FXL_LOG(ERROR) << "Unexpected message response (cmd " << resp.hdr.cmd
                   << ", FrameNotify)";
    return ZX_ERR_BAD_STATE;
  }
  // If we are trying to shut down, immediatly tell the driver to release the
  // frames.
  if (IsShuttingDown()) {
    return ReleaseFrame(resp.data_vb_offset);
  }
  // frame_notify_callback_ is the one callback we don't clear after calling.
  return frame_notify_callback_(resp);
}

zx_status_t CameraClient::SendStop() {
  camera_vb_cmd_stop_req_t req;
  req.hdr.cmd = CAMERA_VB_CMD_STOP;
  zx_status_t write_status = vb_ch_.write(0, &req, sizeof(req), nullptr, 0);
  if (write_status != ZX_OK) {
    FXL_LOG(ERROR) << "Cmd write failure (cmd " << req.hdr.cmd << ", res "
                   << write_status << ")";
    return write_status;
  }
  return ZX_OK;
}

// You can call Stop anytime. Go nuts! It will only send a command if you are
// streaming though.
zx_status_t CameraClient::Stop() {
  if (!IsStreaming() && !IsConfiguring()) {
    return ZX_OK;
  }
  // There is one configuration state where we accept a stop command, and that
  // if we have sent Start, but not received a response:
  if (IsConfiguring() &&
      CheckConfigurationState(CameraState::StartRequested) != ZX_OK) {
    return ZX_ERR_BAD_STATE;
  }

  if (!vb_ch_.is_valid()) {
    FXL_LOG(ERROR) << "Stop called without an open buffer channel";
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t write_status = SendStop();
  if (write_status != ZX_OK) {
    return write_status;
  }

  // TODO(garratt): Make sure the driver is able to transition:
  // Start->Stop->Start.  If it just shoots itself in the head, we will
  // have to reset everything back to the beginning.
  CameraState state;
  {
    fbl::AutoLock lock(&state_lock_);
    state = state_;
  }
  SetConfigurationState(state, CameraState::SetBufferRequested);
  return ZX_OK;
}

// CameraClient's Shutdown prcedure: (Not yet implimented)
// Several resources need to be dealt with upon shutdown:
//  - The channel handles
//  - The Async waiters
//  - The memory in the VMO, that a consumer may be holding, or the driver
//  may be writing to.
//  Clean up order of operations:
//  1) Set Internal State
//     Set our state to shutting down, so if we recieve any more frames, we do
//     not pass them on to the consumer.
//  2) Signal the driver
//     Send a Stop command to the driver, so it will stop writing new frames,
//     and it can put the hardware in a good state. Ideally, This service
//     should wait for the response before step 4.  A timeout should be given,
//     in case the driver has died.
//  3) Signal the Consumer.
//     The consumer is signalled by closing the image pipe.
//     (see image_pipe.fidl) The consumer will signal it is done by signalling
//     all remaining release fences.  This class does not deal with image
//     pipes, but should call OnShutdown() to signal the next layer to perform
//     these operations.  The next layer will make sure that all the buffers
//     are released, and then call Shutdownchannels()
//  4) Close the channels.
//     Now that the vmo is not being used by the driver or the consumer, and
//     the hardware knows it should be stopping the stream, we can shut down
//     communication with the driver.  We close both channels, and cancel the
//     AutoWait objects, so we will not get signaled by any remaining messages.
void CameraClient::Close() {
  if (IsClosed()) {
    return;
  }
  // 1) Set State to shutting down:
  SetShuttingDown(true, true);
  // Signal Driver:
  zx_status_t write_status = SendStop();
  if (write_status != ZX_OK) {
    // We could not send the stop signal!
    // TODO(garratt): check the return value, maybe we could retry.
    // If we can't send the signal, we should expect a response from the
    // driver...
    SetShuttingDown(false, true);
  }
  // Signal Consumer
  // Set Shutdown timer
  // Until the above is implemented just call the consumer shutdown ourselves:
  ShutDown();
}

struct JustResultResp {
  camera_cmd_hdr_t hdr;
  zx_status_t result;
};

typedef union {
  camera::camera_proto::CmdHdr hdr;
  camera::camera_proto::GetFormatsResp get_format;
  camera::camera_proto::SetFormatResp set_format;
  JustResultResp just_result;
} CameraCmdResponse;

typedef union {
  camera::camera_proto::CmdHdr hdr;
  camera::camera_proto::VideoBufSetBufferResp set_buffer;
  camera::camera_proto::VideoBufStartResp start;
  camera::camera_proto::VideoBufStopResp stop;
  camera::camera_proto::VideoBufFrameReleaseResp release_frame;
  camera::camera_proto::VideoBufFrameNotify frame_notify;
  JustResultResp just_result;
} CameraBufferResponse;

// We use just_result instead of payload to avoid compile errors for payloads
// without a result field...
#define CHECK_RESP(_ioctl, _payload, check_result_string)                \
  do {                                                                   \
    if (resp_size != sizeof(resp._payload)) {                            \
      FXL_LOG(ERROR) << "Bad " #_ioctl " response length (" << resp_size \
                     << " != " << sizeof(resp._payload) << ")";          \
      return ZX_ERR_INVALID_ARGS;                                        \
    }                                                                    \
    if (strlen(check_result_string) > 1) {                               \
      if (resp.just_result.result != ZX_OK) {                            \
        FXL_LOG(ERROR) << "Failed to " << check_result_string            \
                       << " Shutting down!";                             \
        return resp.just_result.result;                                  \
      }                                                                  \
    }                                                                    \
  } while (0);

zx_status_t CameraClient::ProcessBufferChannel() {
  CameraBufferResponse resp;
  static_assert(sizeof(resp) <= ZX_CHANNEL_MAX_MSG_BYTES,
                "Response buffer is getting to be too large!");

  uint32_t resp_size;
  zx_status_t res =
      vb_ch_.read(0, &resp, sizeof(resp), &resp_size, nullptr, 0, nullptr);

  if (resp_size < sizeof(resp.hdr) || res != ZX_OK) {
    return res == ZX_OK ? ZX_ERR_INVALID_ARGS : res;
  }

  auto cmd = static_cast<camera::camera_proto::Cmd>(resp.hdr.cmd);
  switch (cmd) {
    case CAMERA_VB_FRAME_NOTIFY:
      CHECK_RESP(CAMERA_VB_FRAME_NOTIFY, frame_notify, "");
      return OnFrameNotify(resp.frame_notify);
    case CAMERA_VB_CMD_SET_BUFFER:
      CHECK_RESP(CAMERA_VB_CMD_SET_BUFFER, set_buffer, "SetBuffer");
      return ZX_OK;
    case CAMERA_VB_CMD_START:
      CHECK_RESP(CAMERA_VB_CMD_START, start, "Start");
      SetStreaming();
      return ZX_OK;
    case CAMERA_VB_CMD_STOP:
      CHECK_RESP(CAMERA_VB_CMD_STOP, stop, "");
      // Check if we are shutting down:
      bool waiting_for_driver, waiting_for_consumer;
      if (IsShuttingDown(&waiting_for_driver, &waiting_for_consumer)) {
        // If we are shutting down, we are now done waiting for the
        // driver.  Even if there was an error, we can't do much about
        // it at this point.
        if (!waiting_for_driver) {
          FXL_LOG(ERROR) << "Unexpected stop response during shutdown.";
        }
        SetShuttingDown(false, waiting_for_consumer);
        return ZX_OK;
      }
      // If we are not shutting down, but failed to stop, we will shutdown!
      if (resp.stop.result != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to Stop. Shutting down!";
      }
      return resp.stop.result;
    case CAMERA_VB_CMD_FRAME_RELEASE:
      CHECK_RESP(CAMERA_VB_CMD_FRAME_RELEASE, release_frame, "Release");
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unrecognized stream command " << resp.hdr.cmd;
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t CameraClient::ProcessCmdChannel() {
  CameraCmdResponse resp;
  static_assert(sizeof(resp) <= ZX_CHANNEL_MAX_MSG_BYTES,
                "Response buffer is getting to be too large!");

  uint32_t resp_size = 0, num_rxed_handles = 0;
  zx_handle_t rxed_handle;
  zx_status_t res = stream_ch_.read(0, &resp, sizeof(resp), &resp_size,
                                    &rxed_handle, 1, &num_rxed_handles);

  if (resp_size < sizeof(resp.hdr) || res != ZX_OK) {
    if (num_rxed_handles != 0) {
      zx_handle_close(rxed_handle);
    }
    return res == ZX_OK ? ZX_ERR_INVALID_ARGS : res;
  }
  FXL_VLOG(4) << "Received command response. cmd: " << resp.hdr.cmd << "  "
              << resp_size << " resp_size, " << rxed_handle << " handle, "
              << num_rxed_handles << " num_handles";

  auto cmd = static_cast<camera::camera_proto::Cmd>(resp.hdr.cmd);
  switch (cmd) {
    case CAMERA_STREAM_CMD_GET_FORMATS:
      CHECK_RESP(CAMERA_STREAM_CMD_GET_FORMAT, get_format, "");
      if (num_rxed_handles != 0) {
        FXL_LOG(ERROR) << "received unexpected channel on GetFormatResponse";
        zx_handle_close(rxed_handle);
        return ZX_ERR_INTERNAL;
      }
      return OnGetFormatsResp(resp.get_format);
      break;
    case CAMERA_STREAM_CMD_SET_FORMAT:
      CHECK_RESP(CAMERA_STREAM_CMD_SET_FORMAT, set_format, "");
      if (num_rxed_handles != 1) {
        FXL_LOG(ERROR) << "Failed to receive channel on SetFormatResponse";
        return ZX_ERR_INTERNAL;
      }
      return OnSetFormatResp(resp.set_format, zx::channel(rxed_handle));
      break;
    default:
      FXL_LOG(ERROR) << "Unrecognized command response " << resp.hdr.cmd;
      return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_ERR_NOT_SUPPORTED;
}
#undef CHECK_RESP

void CameraClient::OnNewCmdMessage(
    async_t* async,
    async::WaitBase* wait,
    zx_status_t status,
    const zx_packet_signal* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error: CameraClient received an error.  Exiting.";
    return;
  }
  // Read channel
  zx_status_t ret_status = ProcessCmdChannel();
  if (ret_status != ZX_OK) {
    FXL_LOG(ERROR) << "Error: Got bad status when processing channel ("
                   << ret_status << ")";
    Close();
    return;
  }
  status = wait->Begin(async);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error: CameraClient wait failed.  Exiting.";
  }
}

void CameraClient::OnNewBufferMessage(
    async_t* async,
    async::WaitBase* wait,
    zx_status_t status,
    const zx_packet_signal* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error: CameraClient received an error.  Exiting.";
    return;
  }
  // Read channel
  zx_status_t ret_status = ProcessBufferChannel();
  if (ret_status != ZX_OK) {
    FXL_LOG(ERROR) << "Error: Got bad status when processing channel ("
                   << ret_status << ")";
    // TODO(garratt): Shut down only this stream, instead of whole process
    Close();
    return;
  }
  status = wait->Begin(async);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error: CameraClient wait failed.  Exiting.";
  }
}
zx_status_t CameraClient::CheckConfigurationState(CameraState required_state) {
  fbl::AutoLock lock(&state_lock_);
  if (0 == (state_ & CameraState::Configuring)) {
    FXL_LOG(ERROR) << "Camera Client is not in configuration state! "
                      "Current state: "
                   << (int)state_;
    return ZX_ERR_BAD_STATE;
  }
  if (state_ != required_state) {
    FXL_LOG(ERROR) << "CameraClient in wrong configuration state! Expected:"
                   << " " << (int)required_state
                   << " current state: " << (int)state_;
    return ZX_ERR_BAD_STATE;
  }
  // Make sure the right channels are open:
  // Clear the configuration bit, or else we will match everything:
  uint16_t required_channels = required_state & ChannelsMask;
  // If buffer command, make sure buffer channel is open:
  if ((required_channels & BufferChannelOpen) && !vb_ch_.is_valid()) {
    FXL_LOG(ERROR) << "Buffer command called without an open buffer channel";
    return ZX_ERR_BAD_STATE;
  }
  // if we require a command channel is open:
  if ((required_channels & CommandChannelOpen) && !stream_ch_.is_valid()) {
    FXL_LOG(ERROR) << "Stream command called without an open cmd channel";
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

bool CameraClient::IsStreaming() {
  fbl::AutoLock lock(&state_lock_);
  return state_ == CameraState::Streaming;
}

bool CameraClient::IsShuttingDown(bool* waiting_for_driver,
                                  bool* waiting_for_consumer) {
  fbl::AutoLock lock(&state_lock_);
  if (state_ & CameraState::ShuttingDown) {
    uint16_t waits = state_ & ~CameraState::ShuttingDown;
    if (waiting_for_driver) {
      *waiting_for_driver = (waits & CameraState::WaitingForDriver);
    }
    if (waiting_for_consumer) {
      *waiting_for_consumer = (waits & CameraState::WaitingForConsumer);
    }
    return true;
  }
  return false;
}

bool CameraClient::IsConfiguring() {
  fbl::AutoLock lock(&state_lock_);
  return state_ & CameraState::Configuring;
}
bool CameraClient::IsClosed() {
  fbl::AutoLock lock(&state_lock_);
  return state_ == CameraState::Closed;
}

void CameraClient::SetStreaming() {
  fbl::AutoLock lock(&state_lock_);
  state_ = CameraState::Streaming;
}
void CameraClient::SetShuttingDown(bool waiting_for_driver,
                                   bool waiting_for_consumer) {
  fbl::AutoLock lock(&state_lock_);
  if (!waiting_for_driver && !waiting_for_consumer) {
    state_ = CameraState::ShuttingDown;
    // Kill the AutoWaiters:
    cmd_msg_waiter_.Cancel();
    buff_msg_waiter_.Cancel();
    // close streams
    vb_ch_.reset();
    stream_ch_.reset();
  }
  if (!waiting_for_driver && waiting_for_consumer) {
    state_ = CameraState::WaitingForConsumer;
  }
  if (waiting_for_driver && !waiting_for_consumer) {
    state_ = CameraState::WaitingForDriver;
  }
  if (waiting_for_driver && waiting_for_consumer) {
    state_ = CameraState::WaitingForBoth;
  }
}

void CameraClient::SetConfigurationState(CameraState current_state,
                                         CameraState next_state) {
  fbl::AutoLock lock(&state_lock_);
  // First check that we are still in the state we want to be in:
  FXL_DCHECK(state_ == current_state)
      << "Unexpected state encountered. "
      << "Expected: " << (int)current_state
      << "  current state: " << (int)state_
      << "  This probably means the state changed during the function.";
  state_ = next_state;
}

}  // namespace video_display
