// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/net_media_service/net_media_player_impl.h"

#include <fcntl.h>

#include "garnet/bin/media/util/file_channel.h"
#include "lib/fxl/logging.h"
#include "lib/media/fidl/seeking_reader.fidl.h"
#include "lib/url/gurl.h"

namespace media {

// static
std::shared_ptr<NetMediaPlayerImpl> NetMediaPlayerImpl::Create(
    const f1dl::String& service_name,
    f1dl::InterfaceHandle<MediaPlayer> media_player,
    f1dl::InterfaceRequest<NetMediaPlayer> net_media_player_request,
    NetMediaServiceImpl* owner) {
  return std::shared_ptr<NetMediaPlayerImpl>(
      new NetMediaPlayerImpl(service_name, std::move(media_player),
                             std::move(net_media_player_request), owner));
}

NetMediaPlayerImpl::NetMediaPlayerImpl(
    const f1dl::String& service_name,
    f1dl::InterfaceHandle<MediaPlayer> media_player,
    f1dl::InterfaceRequest<NetMediaPlayer> net_media_player_request,
    NetMediaServiceImpl* owner)
    : NetMediaServiceImpl::Product<NetMediaPlayer>(
          this,
          std::move(net_media_player_request),
          owner),
      media_player_(media_player.Bind()),
      responder_(this, service_name, owner->application_context()) {
  FXL_DCHECK(owner);

  media_player_.set_error_handler([this]() {
    media_player_.Unbind();
    UnbindAndReleaseFromOwner();
  });
}

NetMediaPlayerImpl::~NetMediaPlayerImpl() {
  media_player_.Unbind();
}

void NetMediaPlayerImpl::SetUrl(const f1dl::String& url_as_string) {
  url::GURL url = url::GURL(url_as_string);

  if (!url.is_valid()) {
    FXL_DLOG(ERROR) << "Invalid URL " << url_as_string << " specified";
    return;
  }

  if (url.SchemeIsFile()) {
    media_player_->SetFileSource(
        ChannelFromFd(fxl::UniqueFD(open(url.path().c_str(), O_RDONLY))));
  } else {
    media_player_->SetHttpSource(url_as_string);
  }
}

void NetMediaPlayerImpl::Play() {
  media_player_->Play();
}

void NetMediaPlayerImpl::Pause() {
  media_player_->Pause();
}

void NetMediaPlayerImpl::Seek(int64_t position) {
  media_player_->Seek(position);
}

void NetMediaPlayerImpl::GetStatus(uint64_t version_last_seen,
                                   const GetStatusCallback& callback) {
  media_player_->GetStatus(version_last_seen, callback);
}

}  // namespace media
