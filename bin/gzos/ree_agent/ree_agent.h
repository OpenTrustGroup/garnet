// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"

#include <ree_agent/cpp/fidl.h>

namespace ree_agent {

class ReeAgent {
 public:
  enum State { START, STOP };

  ReeAgent(uint32_t id, zx::channel ch, size_t max_msg_size);
  virtual ~ReeAgent();

  virtual zx_status_t Start();
  virtual zx_status_t Stop();

  uint32_t message_channel_id() { return message_channel_id_; }
  size_t max_message_size() { return max_message_size_; }

 protected:
  zx_status_t WriteMessage(void* buf, size_t size);
  virtual zx_status_t HandleMessage(void* buf, size_t size) = 0;

 private:
  void OnChannelReady(async_t* async, async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal_t* sig);
  void OnChannelClosed(zx_status_t status, const char* action);

  fbl::Mutex lock_;
  uint32_t message_channel_id_;
  zx::channel message_channel_;
  size_t max_message_size_;
  State state FXL_GUARDED_BY(lock_);
  fbl::unique_ptr<char> read_buffer_ FXL_GUARDED_BY(lock_);
  async_t* async_;
  async::WaitMethod<ReeAgent, &ReeAgent::OnChannelReady> channel_wait_{this};

  FXL_DISALLOW_COPY_AND_ASSIGN(ReeAgent);
};

}  // namespace ree_agent
