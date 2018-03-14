// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/net_media_service/net_media_player_net_stub.h"
#include "garnet/bin/media/net_media_service/net_media_service_impl.h"
#include "lib/fxl/macros.h"
#include "lib/media/fidl/net_media_player.fidl.h"
#include "lib/netconnector/cpp/net_stub_responder.h"

namespace media {

// Fidl agent that wraps a MediaPlayer for remote control.
class NetMediaPlayerImpl : public NetMediaServiceImpl::Product<NetMediaPlayer>,
                           public NetMediaPlayer {
 public:
  static std::shared_ptr<NetMediaPlayerImpl> Create(
      const f1dl::String& service_name,
      f1dl::InterfaceHandle<MediaPlayer> media_player,
      f1dl::InterfaceRequest<NetMediaPlayer> net_media_player_request,
      NetMediaServiceImpl* owner);

  ~NetMediaPlayerImpl() override;

  // NetMediaPlayer implementation.
  void SetUrl(const f1dl::String& url) override;

  void Play() override;

  void Pause() override;

  void Seek(int64_t position) override;

  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

 private:
  NetMediaPlayerImpl(
      const f1dl::String& service_name,
      f1dl::InterfaceHandle<MediaPlayer> media_player,
      f1dl::InterfaceRequest<NetMediaPlayer> net_media_player_request,
      NetMediaServiceImpl* owner);

  MediaPlayerPtr media_player_;
  netconnector::NetStubResponder<NetMediaPlayer, NetMediaPlayerNetStub>
      responder_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetMediaPlayerImpl);
};

}  // namespace media
