// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <memory>

#include <zx/socket.h>

#include "garnet/bin/media/demux/reader.h"
#include "garnet/bin/media/fidl/fidl_default_waiter.h"
#include "garnet/bin/media/util/incident.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/media/fidl/seeking_reader.fidl.h"

namespace media {

// Reads raw data from a SeekingReader service.
class FidlReader : public Reader,
                   public std::enable_shared_from_this<FidlReader> {
 public:
  // Creates an FidlReader. Must be called on a fidl thread.
  static std::shared_ptr<Reader> Create(
      f1dl::InterfaceHandle<SeekingReader> seeking_reader) {
    return std::shared_ptr<Reader>(new FidlReader(std::move(seeking_reader)));
  }

  ~FidlReader() override;

  // Reader implementation.
  void Describe(const DescribeCallback& callback) override;

  void ReadAt(size_t position,
              uint8_t* buffer,
              size_t bytes_to_read,
              const ReadAtCallback& callback) override;

 private:
  // Calls ReadFromSocket.
  static void ReadFromSocketStatic(zx_status_t status,
                                   zx_signals_t pending,
                                   uint64_t count,
                                   void* closure);

  FidlReader(f1dl::InterfaceHandle<SeekingReader> seeking_reader);

  // Continues a ReadAt operation on the thread on which this reader was
  // constructed (a fidl thread).
  void ContinueReadAt();

  // Reads from socket_ into read_at_buffer_.
  void ReadFromSocket();

  // Completes a ReadAt operation by calling the read_at_callback_.
  void CompleteReadAt(Result result, size_t bytes_read = 0);

  // Shuts down the consumer handle and calls CompleteReadAt.
  void FailReadAt(zx_status_t status);

  SeekingReaderPtr seeking_reader_;
  Result result_ = Result::kOk;
  size_t size_ = kUnknownSize;
  bool can_seek_ = false;
  Incident ready_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  std::atomic_bool read_in_progress_;
  size_t read_at_position_;
  uint8_t* read_at_buffer_;
  size_t read_at_bytes_to_read_;
  size_t read_at_bytes_remaining_;
  ReadAtCallback read_at_callback_;
  zx::socket socket_;
  size_t socket_position_ = kUnknownSize;
  FidlAsyncWaitID wait_id_ = 0;
};

}  // namespace media
