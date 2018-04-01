// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <fuchsia/cpp/media.h>
#include "garnet/bin/media/media_service/fidl_conversion_pipeline_builder.h"
#include "garnet/bin/media/media_service/media_component_factory.h"
#include "garnet/bin/media/util/incident.h"
#include "lib/fidl/cpp/binding.h"

namespace media {

// Fidl agent that consumes a stream and delivers it to a destination specified
// by URL.
class MediaSinkImpl : public MediaComponentFactory::Product<MediaSink>,
                      public MediaSink {
 public:
  static std::shared_ptr<MediaSinkImpl> Create(
      fidl::InterfaceHandle<MediaRenderer> renderer_handle,
      fidl::InterfaceRequest<MediaSink> sink_request,
      MediaComponentFactory* owner);

  ~MediaSinkImpl() override;

  // MediaSink implementation.
  void GetTimelineControlPoint(
      fidl::InterfaceRequest<MediaTimelineControlPoint> req) override;

  void ConsumeMediaType(MediaType media_type,
                        ConsumeMediaTypeCallback callback) override;

 private:
  MediaSinkImpl(fidl::InterfaceHandle<MediaRenderer> renderer_handle,
                fidl::InterfaceRequest<MediaSink> sink_request,
                MediaComponentFactory* owner);

  // Builds the conversion pipeline.
  void BuildConversionPipeline();

  MediaRendererPtr renderer_;
  ConsumeMediaTypeCallback consume_media_type_callback_;
  MediaTypePtr original_media_type_;
  std::unique_ptr<StreamType> stream_type_;
  fidl::VectorPtr<std::unique_ptr<StreamTypeSet>> supported_stream_types_;
  Incident got_supported_stream_types_;
};

}  // namespace media
