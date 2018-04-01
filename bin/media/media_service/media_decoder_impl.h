// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/media/decode/decoder.h"
#include "garnet/bin/media/fidl/fidl_packet_consumer.h"
#include "garnet/bin/media/fidl/fidl_packet_producer.h"
#include "garnet/bin/media/framework/graph.h"
#include "garnet/bin/media/media_service/media_component_factory.h"
#include "lib/fidl/cpp/binding.h"
#include <fuchsia/cpp/media.h>

namespace media {

// Fidl agent that decodes a stream.
class MediaDecoderImpl
    : public MediaComponentFactory::Product<MediaTypeConverter>,
      public MediaTypeConverter {
 public:
  static std::shared_ptr<MediaDecoderImpl> Create(
      MediaTypePtr input_media_type,
      fidl::InterfaceRequest<MediaTypeConverter> request,
      MediaComponentFactory* owner);

  ~MediaDecoderImpl() override;

  // MediaTypeConverter implementation.
  void GetOutputType(GetOutputTypeCallback callback) override;

  void GetPacketConsumer(
      fidl::InterfaceRequest<MediaPacketConsumer> consumer) override;

  void GetPacketProducer(
      fidl::InterfaceRequest<MediaPacketProducer> producer) override;

 private:
  MediaDecoderImpl(MediaTypePtr input_media_type,
                   fidl::InterfaceRequest<MediaTypeConverter> request,
                   MediaComponentFactory* owner);

  Graph graph_;
  std::shared_ptr<FidlPacketConsumer> consumer_;
  std::shared_ptr<Decoder> decoder_;
  std::shared_ptr<FidlPacketProducer> producer_;
};

}  // namespace media
