// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/test/fake_wav_reader.h"

#include <zx/socket.h>

namespace media {

FakeWavReader::FakeWavReader() : binding_(this) {
  WriteHeader();
}

void FakeWavReader::WriteHeader() {
  header_.clear();

  // Master chunk.
  WriteHeader4CC("RIFF");
  WriteHeaderUint32(size_ - kChunkSizeDeficit);
  WriteHeader4CC("WAVE");  // Format
  FXL_DCHECK(header_.size() == kMasterChunkHeaderSize);

  // Format subchunk.
  WriteHeader4CC("fmt ");
  WriteHeaderUint32(kFormatChunkSize - kChunkSizeDeficit);
  WriteHeaderUint16(kAudioEncoding);
  WriteHeaderUint16(kSamplesPerFrame);
  WriteHeaderUint32(kFramesPerSecond);
  // Byte rate.
  WriteHeaderUint32(kFramesPerSecond * kSamplesPerFrame * kBitsPerSample / 8);
  // Block alignment (frame size in bytes).
  WriteHeaderUint16(kSamplesPerFrame * kBitsPerSample / 8);
  WriteHeaderUint16(kBitsPerSample);
  FXL_DCHECK(header_.size() == kMasterChunkHeaderSize + kFormatChunkSize);

  // Data subchunk.
  WriteHeader4CC("data");
  WriteHeaderUint32(size_ - kMasterChunkHeaderSize - kFormatChunkSize -
                    kChunkSizeDeficit);
  FXL_DCHECK(header_.size() ==
             kMasterChunkHeaderSize + kFormatChunkSize + kDataChunkHeaderSize);
}

FakeWavReader::~FakeWavReader() {}

void FakeWavReader::Bind(fidl::InterfaceRequest<SeekingReader> request) {
  binding_.Bind(std::move(request));
}

void FakeWavReader::Describe(DescribeCallback callback) {
  callback(MediaResult::OK, size_, true);
}

void FakeWavReader::ReadAt(uint64_t position, ReadAtCallback callback) {
  if (socket_) {
    if (wait_id_ != 0) {
      GetDefaultAsyncWaiter()->CancelWait(wait_id_);
      wait_id_ = 0;
    }
    socket_.reset();
  }

  zx::socket other_socket;
  zx_status_t status = zx::socket::create(0u, &socket_, &other_socket);
  FXL_DCHECK(status == ZX_OK);
  callback(MediaResult::OK, std::move(other_socket));

  position_ = position;

  WriteToSocket();
}

void FakeWavReader::WriteToSocket() {
  while (true) {
    uint8_t byte = GetByte(position_);
    size_t byte_count;

    zx_status_t status = socket_.write(0u, &byte, 1u, &byte_count);
    if (status == ZX_OK) {
      FXL_DCHECK(byte_count == 1);
      ++position_;
      continue;
    }

    if (status == ZX_ERR_SHOULD_WAIT) {
      wait_id_ = GetDefaultAsyncWaiter()->AsyncWait(
          socket_.get(), ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED,
          ZX_TIME_INFINITE,
          [this](zx_status_t status, zx_signals_t pending, uint64_t count) {
            wait_id_ = 0;
            if (status == ZX_ERR_CANCELED) {
              // Run loop has aborted...the app is shutting down.
              return;
            }

            if (status != ZX_OK) {
              FXL_LOG(ERROR) << "AsyncWait failed " << status;
              socket_.reset();
              return;
            }

            WriteToSocket();
          });
      return;
    }

    if (status == ZX_ERR_PEER_CLOSED) {
      // Consumer end was closed. This is normal behavior, depending on what
      // the consumer is up to.
      socket_.reset();
      return;
    }

    FXL_DCHECK(false) << "zx::socket::write failed, status " << status;
  }
}

void FakeWavReader::WriteHeader4CC(const std::string& value) {
  FXL_DCHECK(value.size() == 4);
  header_.push_back(static_cast<uint8_t>(value[0]));
  header_.push_back(static_cast<uint8_t>(value[1]));
  header_.push_back(static_cast<uint8_t>(value[2]));
  header_.push_back(static_cast<uint8_t>(value[3]));
}

void FakeWavReader::WriteHeaderUint16(uint16_t value) {
  header_.push_back(static_cast<uint8_t>(value));
  header_.push_back(static_cast<uint8_t>(value >> 8));
}

void FakeWavReader::WriteHeaderUint32(uint32_t value) {
  header_.push_back(static_cast<uint8_t>(value));
  header_.push_back(static_cast<uint8_t>(value >> 8));
  header_.push_back(static_cast<uint8_t>(value >> 16));
  header_.push_back(static_cast<uint8_t>(value >> 24));
}

uint8_t FakeWavReader::GetByte(size_t position) {
  if (position < header_.size()) {
    // Header.
    return header_[position];
  }

  // Unpleasant sound.
  return static_cast<uint8_t>(position ^ (position >> 8));
}

}  // namespace media
