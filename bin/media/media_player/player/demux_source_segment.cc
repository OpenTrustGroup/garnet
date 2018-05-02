// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/media.h>

#include "garnet/bin/media/media_player/player/demux_source_segment.h"

#include "garnet/bin/media/media_player/util/safe_clone.h"
#include "lib/fxl/logging.h"

namespace media_player {

// static
std::unique_ptr<DemuxSourceSegment> DemuxSourceSegment::Create(
    std::shared_ptr<Demux> demux) {
  return std::make_unique<DemuxSourceSegment>(demux);
}

DemuxSourceSegment::DemuxSourceSegment(std::shared_ptr<Demux> demux)
    : demux_(demux) {
  FXL_DCHECK(demux_);

  demux_->SetStatusCallback([this](const std::unique_ptr<Metadata>& metadata,
                                   const std::string& problem_type,
                                   const std::string& problem_details) {
    metadata_ = SafeClone(metadata);
    NotifyUpdate();

    if (problem_type.empty()) {
      ReportNoProblem();
    } else {
      ReportProblem(problem_type, problem_details);
    }
  });

  demux_->WhenInitialized(
      [this](Result result) { demux_initialized_.Occur(); });
}

DemuxSourceSegment::~DemuxSourceSegment() {}

void DemuxSourceSegment::DidProvision() {
  demux_initialized_.When([this]() {
    if (provisioned()) {
      BuildGraph();
    }
  });
}

void DemuxSourceSegment::WillDeprovision() {
  if (demux_node_) {
    graph().RemoveNode(demux_node_);
    demux_node_ = nullptr;
  }

  if (demux_) {
    demux_->SetStatusCallback(nullptr);
    demux_ = nullptr;
  }
}

void DemuxSourceSegment::BuildGraph() {
  demux_node_ = graph().Add(demux_);

  const auto& streams = demux_->streams();
  for (size_t index = 0; index < streams.size(); ++index) {
    auto& stream = streams[index];
    OnStreamUpdated(stream->index(), *stream->stream_type(),
                    demux_node_.output(stream->index()),
                    index != streams.size() - 1);
  }
}

void DemuxSourceSegment::Flush(bool hold_frame) {
  FXL_DCHECK(demux_initialized_.occurred());
  graph().FlushAllOutputs(demux_node_, hold_frame);
}

void DemuxSourceSegment::Seek(int64_t position, fxl::Closure callback) {
  FXL_DCHECK(demux_initialized_.occurred());
  demux_->Seek(position, callback);
}

}  // namespace media_player
