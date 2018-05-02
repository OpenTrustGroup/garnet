// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_SINK_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_SINK_H_

#include "garnet/bin/media/media_player/framework/models/demand.h"
#include "garnet/bin/media/media_player/framework/models/node.h"
#include "garnet/bin/media/media_player/framework/models/stage.h"
#include "garnet/bin/media/media_player/framework/packet.h"
#include "garnet/bin/media/media_player/framework/payload_allocator.h"

namespace media_player {

// Stage for |Sink|.
class SinkStage : public Stage {
 public:
  ~SinkStage() override {}

  virtual void SetDemand(Demand demand) = 0;
};

// Sink that consumes packets asynchronously.
class Sink : public Node<SinkStage> {
 public:
  ~Sink() override {}

  // Flushes media state. |hold_frame| indicates whether a video renderer
  // should hold (and display) the newest frame.
  virtual void Flush(bool hold_frame){};

  // An allocator that must be used for supplied packets or nullptr if there's
  // no such requirement.
  virtual std::shared_ptr<PayloadAllocator> allocator() = 0;

  // Supplies a packet to the sink, returning the new demand for the input.
  virtual Demand SupplyPacket(PacketPtr packet) = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_SINK_H_
