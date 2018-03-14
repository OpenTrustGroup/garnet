// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/intrusive_double_list.h>
#include <stdint.h>

namespace media {
namespace audio {

class AudioServerImpl;

// TODO(johngro): Consider moving instances of this class to a slab allocation
// pattern.  They are the most frequently allocated object in the mixer (easily
// 100s per second) and they do not live very long at all (300-400mSec at most),
// so they could easily be causing heap fragmentation issues.
class AudioPacketRef :
  public fbl::RefCounted<AudioPacketRef>,
  public fbl::Recyclable<AudioPacketRef>,
  public fbl::DoublyLinkedListable<fbl::unique_ptr<AudioPacketRef>> {
 public:
  // Accessors for starting and ending presentation time stamps expressed in
  // units of audio frames (note, not media time), as signed 51.12 fixed point
  // integers (see kPtsFractionalBits).  At 192KHz, this allows for ~372.7
  // years of usable range when starting from a media time of 0.
  //
  // AudioPackets consumed by the AudioServer are all expected to have
  // explicit presentation time stamps.  If packets sent by the user are
  // missing timestamps, appropriate timestamps will be synthesized at this
  // point in the pipeline.
  //
  // Note, the start pts is the time at which the first frame of audio in the
  // packet should be presented.  The end_pts is the time at which the frame
  // after the final frame in the packet would be presented.
  //
  // TODO(johngro): Reconsider this.  It may be best to keep things expressed
  // simply in media time instead of converting to fractional units of
  // renderer frames.  If/when outputs move away from a single fixed step size
  // for output sampling, it will probably be best to just convert this back
  // to media time.
  int64_t  start_pts() const { return start_pts_; }
  int64_t  end_pts() const { return end_pts_; }
  uint32_t frac_frame_len() const { return frac_frame_len_; }

  // TODO(johngro): Remove all of the virtual functions from this class when we
  // have deprecated the v1 renderer interface and no longer need to maintain v1
  // and v2 versions of packet references.
  virtual void Cleanup() = 0;
  virtual void* payload() = 0;
  virtual uint32_t flags() = 0;

 protected:
  friend class fbl::RefPtr<AudioPacketRef>;
  friend class fbl::Recyclable<AudioPacketRef>;
  friend class fbl::unique_ptr<AudioPacketRef>;

  AudioPacketRef(AudioServerImpl* server,
                 uint32_t frac_frame_len,
                 int64_t start_pts);
  virtual ~AudioPacketRef() = default;

  // Check to see if this packet has a valid callback.  If so, when it gets
  // recycled for the first time, it needs to be kept alive and posted to the
  // server's cleanup queue so that the user's callback gets called on the main
  // server dispatcher thread.
  virtual bool NeedsCleanup() { return true; }

  AudioServerImpl* const server_;
  uint32_t frac_frame_len_;
  int64_t start_pts_;
  int64_t end_pts_;
  bool was_recycled_ = false;

 private:
  void fbl_recycle();
};

}  // namespace audio
}  // namespace media
