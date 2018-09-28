// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_DECODER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_DECODER_H_

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/zx/bti.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <functional>

#include "codec_frame.h"
#include "decoder_core.h"
#include "pts_manager.h"
#include "registers.h"
#include "video_frame.h"

class FirmwareBlob;
class PtsManager;

enum class DeviceType {
  kUnknown,
  kGXM,   // S912
  kG12A,  // S905D2
};

class CanvasEntry {
 public:
  CanvasEntry(uint32_t index) : index_(index) {}

  ~CanvasEntry() { assert(!valid_); }

  __WARN_UNUSED_RESULT
  uint32_t index() const {
    assert(valid_);
    return index_;
  }
  void invalidate() {
    assert(valid_);
    valid_ = false;
  }

 private:
  uint32_t index_;
  bool valid_ = true;
};

class CodecPacket;
class VideoDecoder {
 public:
  using FrameReadyNotifier = std::function<void(std::shared_ptr<VideoFrame>)>;
  using InitializeFramesHandler =
      std::function<zx_status_t(::zx::bti,
                                uint32_t,  // frame_count
                                uint32_t,  // width
                                uint32_t,  // height
                                uint32_t,  // stride
                                uint32_t,  // display_width
                                uint32_t   // display_height
                                )>;
  class Owner {
   public:
    virtual __WARN_UNUSED_RESULT DosRegisterIo* dosbus() = 0;
    virtual __WARN_UNUSED_RESULT zx_handle_t bti() = 0;
    virtual __WARN_UNUSED_RESULT DeviceType device_type() = 0;
    virtual __WARN_UNUSED_RESULT FirmwareBlob* firmware_blob() = 0;
    virtual __WARN_UNUSED_RESULT std::unique_ptr<CanvasEntry> ConfigureCanvas(
        io_buffer_t* io_buffer, uint32_t offset, uint32_t width,
        uint32_t height, uint32_t wrap, uint32_t blockmode) = 0;
    virtual void FreeCanvas(std::unique_ptr<CanvasEntry> canvas) = 0;
    virtual __WARN_UNUSED_RESULT DecoderCore* core() = 0;
    virtual __WARN_UNUSED_RESULT zx_status_t
    AllocateIoBuffer(io_buffer_t* buffer, size_t size, uint32_t alignement_log2,
                     uint32_t flags) = 0;
    virtual __WARN_UNUSED_RESULT bool IsDecoderCurrent(
        VideoDecoder* decoder) = 0;
  };

  VideoDecoder() { pts_manager_ = std::make_unique<PtsManager>(); }

  virtual __WARN_UNUSED_RESULT zx_status_t Initialize() = 0;
  virtual void HandleInterrupt() = 0;
  virtual void SetFrameReadyNotifier(FrameReadyNotifier notifier) {}
  virtual void SetInitializeFramesHandler(InitializeFramesHandler handler) {
    ZX_ASSERT_MSG(false, "not yet implemented");
  }
  virtual void SetErrorHandler(fit::closure error_handler) {
    ZX_ASSERT_MSG(false, "not yet implemented");
  }
  virtual void ReturnFrame(std::shared_ptr<VideoFrame> frame) = 0;
  virtual void InitializedFrames(std::vector<CodecFrame> frames, uint32_t width,
                                 uint32_t height, uint32_t stride) = 0;
  virtual ~VideoDecoder() {}

  __WARN_UNUSED_RESULT PtsManager* pts_manager() { return pts_manager_.get(); }

 protected:
  std::unique_ptr<PtsManager> pts_manager_;
  uint64_t next_non_codec_buffer_lifetime_ordinal_ = 0;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_DECODER_H_
