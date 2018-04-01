// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include <fuchsia/cpp/media.h>

#include "garnet/bin/media/fidl/fidl_packet_producer.h"
#include "garnet/bin/media/framework/types/stream_type.h"
#include "garnet/bin/media/media_service/fidl_conversion_pipeline_builder.h"
#include "garnet/bin/media/media_service/media_component_factory.h"
#include "garnet/bin/media/util/fidl_publisher.h"
#include "garnet/bin/media/util/incident.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/tasks/task_runner.h"

namespace media {

// Fidl agent that produces streams from an origin specified by URL.
class MediaSourceImpl : public MediaComponentFactory::Product<MediaSource>,
                        public MediaSource {
 public:
  static std::shared_ptr<MediaSourceImpl> Create(
      fidl::InterfaceHandle<SeekingReader> reader,
      fidl::VectorPtr<MediaTypeSet> allowed_media_types,
      fidl::InterfaceRequest<MediaSource> request,
      MediaComponentFactory* owner);

  ~MediaSourceImpl() override;

  // MediaSource implementation.
  void Describe(DescribeCallback callback) override;

  void GetPacketProducer(
      uint32_t stream_index,
      fidl::InterfaceRequest<MediaPacketProducer> request) override;

  void GetStatus(uint64_t version_last_seen,
                 GetStatusCallback callback) override;

  void Flush(bool hold_frame, FlushCallback callback) override;

  void Seek(int64_t position, SeekCallback callback) override;

 private:
  MediaSourceImpl(fidl::InterfaceHandle<SeekingReader> reader,
                  fidl::VectorPtr<MediaTypeSet> allowed_media_types,
                  fidl::InterfaceRequest<MediaSource> request,
                  MediaComponentFactory* owner);

  class Stream {
   public:
    Stream(size_t stream_index,
           MediaComponentFactory* factory,
           const ProducerGetter& producer_getter,
           std::unique_ptr<StreamType> stream_type,
           const fidl::VectorPtr<std::unique_ptr<StreamTypeSet>>&
               allowed_stream_types,
           const std::function<void()>& callback);

    ~Stream();

    // Gets the media type of the stream.
    MediaTypePtr media_type() const;

    // Gets the producer.
    void GetPacketProducer(fidl::InterfaceRequest<MediaPacketProducer> request);

    bool valid() { return !!producer_getter_; }

    ProducerGetter producer_getter_;
    std::unique_ptr<StreamType> stream_type_;
  };

  // Handles a status update from the demux. When called with the default
  // argument values, initiates demux status updates.
  void HandleDemuxStatusUpdates(uint64_t version = kInitialStatus,
                                MediaSourceStatusPtr status = nullptr);

  fidl::VectorPtr<std::unique_ptr<StreamTypeSet>> allowed_stream_types_;
  MediaSourcePtr demux_;
  Incident init_complete_;
  std::vector<std::unique_ptr<Stream>> streams_;
  MediaSourceStatusPtr demux_status_;
  FidlPublisher<GetStatusCallback> status_publisher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaSourceImpl);
};

}  // namespace media
