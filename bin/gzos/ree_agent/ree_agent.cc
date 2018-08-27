// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/ree_agent.h"

namespace ree_agent {

ReeAgent::ReeAgent(uint32_t id, zx::channel ch, size_t max_msg_size)
    : Agent(this, fbl::move(ch), max_msg_size),
      message_channel_id_(id),
      state(State::STOP) {}

ReeAgent::~ReeAgent() {}

zx_status_t ReeAgent::Start() {
  fbl::AutoLock lock(&lock_);

  if (state != State::STOP) {
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t status = message_reader_.Start();
  if (status != ZX_OK) {
    return status;
  }

  state = State::START;
  return ZX_OK;
};

zx_status_t ReeAgent::Stop() {
  fbl::AutoLock lock(&lock_);

  if (state != State::START) {
    return ZX_ERR_BAD_STATE;
  }

  message_reader_.Stop();

  state = State::STOP;
  return ZX_OK;
};

zx_status_t ReeAgent::OnMessage(Message message) {
  return HandleMessage(message.data(), message.actual());
};

}  // namespace ree_agent
