// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/media/media_service/media_component_factory.h"
#include "garnet/bin/media/util/incident.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/media/fidl/seeking_reader.fidl.h"
#include "lib/network/fidl/url_loader.fidl.h"

namespace media {

// Fidl agent that reads from an HTTP service.
class NetworkReaderImpl : public MediaComponentFactory::Product<SeekingReader>,
                          public SeekingReader {
 public:
  static std::shared_ptr<NetworkReaderImpl> Create(
      const f1dl::String& url,
      f1dl::InterfaceRequest<SeekingReader> request,
      MediaComponentFactory* owner);

  ~NetworkReaderImpl() override;

  // SeekingReader implementation.
  void Describe(const DescribeCallback& callback) override;

  void ReadAt(uint64_t position, const ReadAtCallback& callback) override;

 private:
  static const char* kContentLengthHeaderName;
  static const char* kAcceptRangesHeaderName;
  static const char* kAcceptRangesHeaderBytesValue;
  static const char* kRangeHeaderName;
  static constexpr uint32_t kStatusOk = 200u;
  static constexpr uint32_t kStatusPartialContent = 206u;
  static constexpr uint32_t kStatusNotFound = 404u;

  NetworkReaderImpl(const f1dl::String& url,
                    f1dl::InterfaceRequest<SeekingReader> request,
                    MediaComponentFactory* owner);

  std::string url_;
  network::URLLoaderPtr url_loader_;
  MediaResult result_ = MediaResult::OK;
  uint64_t size_ = kUnknownSize;
  bool can_seek_ = false;
  Incident ready_;
};

}  // namespace media
