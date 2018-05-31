// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_DEMUX_DEMUX_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_DEMUX_DEMUX_H_

#include <memory>
#include <vector>

#include "garnet/bin/media/media_player/demux/reader.h"
#include "garnet/bin/media/media_player/framework/metadata.h"
#include "garnet/bin/media/media_player/framework/models/async_node.h"
#include "garnet/bin/media/media_player/framework/packet.h"
#include "garnet/bin/media/media_player/framework/result.h"
#include "garnet/bin/media/media_player/framework/types/stream_type.h"

namespace media_player {

// Abstract base class for sources that parse input from a reader and
// produce one or more output streams.
class Demux : public AsyncNode {
 public:
  using SeekCallback = std::function<void()>;
  using StatusCallback = std::function<void(
      const std::unique_ptr<Metadata>& metadata,
      const std::string& problem_type, const std::string& problem_details)>;

  // Represents a stream produced by the demux.
  class DemuxStream {
    // TODO(dalesat): Replace this class with stream_type_, unless more stuff
    // needs to be added.
   public:
    virtual ~DemuxStream() {}

    virtual size_t index() const = 0;

    virtual std::unique_ptr<StreamType> stream_type() const = 0;

    virtual media::TimelineRate pts_rate() const = 0;
  };

  // Creates a Demux object for a given reader.
  static std::shared_ptr<Demux> Create(std::shared_ptr<Reader> reader);

  ~Demux() override {}

  // Sets a callback to call when metadata or problem changes occur.
  virtual void SetStatusCallback(StatusCallback callback) = 0;

  // Calls the callback when the initial streams and metadata have
  // established.
  virtual void WhenInitialized(std::function<void(Result)> callback) = 0;

  // Gets the stream collection. This method should not be called until the
  // WhenInitialized callback has been called.
  virtual const std::vector<std::unique_ptr<DemuxStream>>& streams() const = 0;

  // Seeks to the specified position and calls the callback. THE CALLBACK MAY
  // BE CALLED ON AN ARBITRARY THREAD.
  virtual void Seek(int64_t position, SeekCallback callback) = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_DEMUX_DEMUX_H_
