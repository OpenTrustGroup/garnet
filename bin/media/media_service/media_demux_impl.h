// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "garnet/bin/media/demux/demux.h"
#include "garnet/bin/media/fidl/fidl_packet_producer.h"
#include "garnet/bin/media/framework/graph.h"
#include "garnet/bin/media/media_service/media_component_factory.h"
#include "garnet/bin/media/util/fidl_publisher.h"
#include "garnet/bin/media/util/incident.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/media/fidl/media_source.fidl.h"
#include "lib/media/fidl/seeking_reader.fidl.h"

namespace media {

// Fidl agent that decodes a stream.
class MediaDemuxImpl : public MediaComponentFactory::Product<MediaSource>,
                       public MediaSource {
 public:
  static std::shared_ptr<MediaDemuxImpl> Create(
      f1dl::InterfaceHandle<SeekingReader> reader,
      f1dl::InterfaceRequest<MediaSource> request,
      MediaComponentFactory* owner);

  ~MediaDemuxImpl() override;

  // MediaSource implementation.
  void Describe(const DescribeCallback& callback) override;

  void GetPacketProducer(
      uint32_t stream_index,
      f1dl::InterfaceRequest<MediaPacketProducer> producer) override;

  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void Flush(bool hold_frame, const FlushCallback& callback) override;

  void Seek(int64_t position, const SeekCallback& callback) override;

 private:
  MediaDemuxImpl(f1dl::InterfaceHandle<SeekingReader> reader,
                 f1dl::InterfaceRequest<MediaSource> request,
                 MediaComponentFactory* owner);

  class Stream {
   public:
    Stream(OutputRef output,
           std::unique_ptr<StreamType> stream_type,
           Graph* graph);

    ~Stream();

    // Returns the stream's |StreamType|.
    const std::unique_ptr<StreamType>& stream_type() const {
      return stream_type_;
    }

    // Returns the stream's producer.
    std::shared_ptr<FidlPacketProducer> producer() const { return producer_; }

    // Binds the producer.
    void BindPacketProducer(
        f1dl::InterfaceRequest<MediaPacketProducer> producer);

    // Tells the producer to flush its connection.
    void FlushConnection(
        bool hold_frame,
        const FidlPacketProducer::FlushConnectionCallback callback);

   private:
    std::unique_ptr<StreamType> stream_type_;
    Graph* graph_;
    OutputRef output_;
    std::shared_ptr<FidlPacketProducer> producer_;
  };

  // Handles the completion of demux initialization.
  void OnDemuxInitialized(Result result);

  // Reports a problem via status.
  void ReportProblem(const std::string& type, const std::string& details);

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  Graph graph_;
  NodeRef demux_node_;
  std::shared_ptr<Demux> demux_;
  Incident init_complete_;
  std::vector<std::unique_ptr<Stream>> streams_;
  FidlPublisher<GetStatusCallback> status_publisher_;
  MediaMetadataPtr metadata_;
  ProblemPtr problem_;
};

}  // namespace media
