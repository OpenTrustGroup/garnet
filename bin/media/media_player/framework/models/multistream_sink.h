// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_MULTISTREAM_SINK_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_MULTISTREAM_SINK_H_

#include "garnet/bin/media/media_player/framework/models/demand.h"
#include "garnet/bin/media/media_player/framework/models/node.h"
#include "garnet/bin/media/media_player/framework/models/stage.h"
#include "garnet/bin/media/media_player/framework/packet.h"

namespace media_player {

// Stage for |MultistreamSink|.
class MultistreamSinkStage : public Stage {
 public:
  ~MultistreamSinkStage() override {}

  // TODO(dalesat): Revisit allocation semantics.

  // Allocates an input and returns its index.
  virtual size_t AllocateInput() = 0;

  // Releases a previously-allocated input and returns the container size
  // required to hold the remaining inputs (i.e. max input index + 1). The
  // return value can be used to resize the caller's input container.
  virtual size_t ReleaseInput(size_t index) = 0;

  // Updates demand for the specified input.
  virtual void UpdateDemand(size_t input_index, Demand demand) = 0;
};

// Synchronous sink of packets for multiple streams.
class MultistreamSink : public Node<MultistreamSinkStage> {
 public:
  ~MultistreamSink() override {}

  // Flushes media state. |hold_frame| indicates whether a video renderer
  // should hold (and display) the newest frame.
  virtual void Flush(bool hold_frame){};

  // Supplies a packet to the sink, returning the new demand for the input.
  virtual Demand SupplyPacket(size_t input_index, PacketPtr packet) = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_MULTISTREAM_SINK_H_
