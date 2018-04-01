// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/media_sink_impl.h"

#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "garnet/bin/media/media_service/fidl_conversion_pipeline_builder.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/types/type_converters.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace media {

// static
std::shared_ptr<MediaSinkImpl> MediaSinkImpl::Create(
    fidl::InterfaceHandle<MediaRenderer> renderer_handle,
    fidl::InterfaceRequest<MediaSink> sink_request,
    MediaComponentFactory* owner) {
  return std::shared_ptr<MediaSinkImpl>(new MediaSinkImpl(
      std::move(renderer_handle), std::move(sink_request), owner));
}

MediaSinkImpl::MediaSinkImpl(
    fidl::InterfaceHandle<MediaRenderer> renderer_handle,
    fidl::InterfaceRequest<MediaSink> sink_request,
    MediaComponentFactory* owner)
    : MediaComponentFactory::Product<MediaSink>(this,
                                                std::move(sink_request),
                                                owner),
      renderer_(renderer_handle.Bind()) {
  FXL_DCHECK(renderer_);

  renderer_->GetSupportedMediaTypes(
      [this](fidl::VectorPtr<MediaTypeSet> supported_media_types) {
        FXL_DCHECK(supported_media_types);

        for (auto it = supported_media_types->begin();
             it != supported_media_types->end(); ++it) {
          supported_stream_types_.push_back(
              fxl::To<std::unique_ptr<media::StreamTypeSet>>(*it));
        }

        got_supported_stream_types_.Occur();
      });
}

MediaSinkImpl::~MediaSinkImpl() {}

void MediaSinkImpl::GetTimelineControlPoint(
    fidl::InterfaceRequest<MediaTimelineControlPoint> request) {
  FXL_DCHECK(renderer_);
  renderer_->GetTimelineControlPoint(std::move(request));
}

void MediaSinkImpl::ConsumeMediaType(MediaType media_type,
                                     ConsumeMediaTypeCallback callback) {
  if (consume_media_type_callback_) {
    FXL_DLOG(ERROR) << "ConsumeMediaType called while already pending.";
    callback(nullptr);
    UnbindAndReleaseFromOwner();
    return;
  }

  original_media_type_ = fidl::MakeOptional(std::move(media_type));
  stream_type_ = fxl::To<std::unique_ptr<StreamType>>(original_media_type_);
  consume_media_type_callback_ = callback;

  got_supported_stream_types_.When([this]() { BuildConversionPipeline(); });
}

void MediaSinkImpl::BuildConversionPipeline() {
  BuildFidlConversionPipeline(
      owner(), *supported_stream_types_, nullptr,
      [this](fidl::InterfaceRequest<MediaPacketConsumer> request) {
        renderer_->GetPacketConsumer(std::move(request));
      },
      std::move(stream_type_),
      [this](bool succeeded, const ConsumerGetter& consumer_getter,
             const ProducerGetter& producer_getter,
             std::unique_ptr<StreamType> stream_type) {
        FXL_DCHECK(!producer_getter);
        FXL_DCHECK(consume_media_type_callback_);

        if (!succeeded) {
          FXL_LOG(WARNING) << "Failed to create conversion pipeline.";
          consume_media_type_callback_(nullptr);
          consume_media_type_callback_ = nullptr;
          original_media_type_.reset();
          return;
        }

        FXL_DCHECK(consumer_getter);

        stream_type_ = std::move(stream_type);

        renderer_->SetMediaType(fxl::To<MediaType>(stream_type_));

        // Not needed anymore.
        original_media_type_.reset();

        MediaPacketConsumerPtr consumer;
        consumer_getter(consumer.NewRequest());
        consume_media_type_callback_(std::move(consumer));
        consume_media_type_callback_ = nullptr;
      });
}

}  // namespace media
