// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include "packet_protocol.h"
#include "router.h"
#include "trace.h"

namespace overnet {

class PacketLink : public Link, private PacketProtocol::PacketSender {
 public:
  PacketLink(Router* router, TraceSink trace_sink, NodeId peer, uint32_t mss);
  void Close(Callback<void> quiesced) override final;
  void Forward(Message message) override final;
  void Process(TimeStamp received, Slice packet);
  virtual void Emit(Slice packet) = 0;
  LinkMetrics GetLinkMetrics() override final;

 private:
  void SchedulePacket();
  void SendPacket(SeqNum seq, LazySlice data,
                  Callback<void> done) override final;
  Status ProcessBody(TimeStamp received, Slice packet);
  Slice BuildPacket(LazySliceArgs args);

  Router* const router_;
  Timer* const timer_;
  const TraceSink trace_sink_;
  const NodeId peer_;
  const uint64_t label_;
  uint64_t metrics_version_ = 1;
  PacketProtocol protocol_;
  bool sending_ = false;
  Optional<MessageWithPayload> stashed_;

  // data for a send
  std::vector<Slice> send_slices_;

  struct Emitting {
    Emitting(Timer* timer, TimeStamp when, Slice slice, Callback<void> done,
             StatusCallback timer_cb)
        : slice(std::move(slice)),
          done(std::move(done)),
          timeout(timer, when, std::move(timer_cb)) {}
    Slice slice;
    Callback<void> done;
    Timeout timeout;
  };
  Optional<Emitting> emitting_;

  std::queue<Message> outgoing_;
};

}  // namespace overnet
