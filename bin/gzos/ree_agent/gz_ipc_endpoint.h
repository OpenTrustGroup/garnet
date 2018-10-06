// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>
#include <zircon/types.h>
#include <zx/channel.h>

#include "garnet/bin/gzos/ree_agent/message_reader.h"

namespace ree_agent {

class GzIpcAgent;
class GzIpcEndpoint : public MessageHandler {
 public:
  GzIpcEndpoint() = delete;
  ~GzIpcEndpoint() = default;

  GzIpcEndpoint(GzIpcAgent* agent, uint32_t local_addr, uint32_t remote_addr,
                zx::channel channel)
      : message_reader_(this, fbl::move(channel), PAGE_SIZE),
        agent_(agent),
        local_addr_(local_addr),
        remote_addr_(remote_addr) {}

  void Serve() {
    FXL_CHECK(message_reader_.Start() == ZX_OK);
  }

  zx_status_t Write(void* msg, size_t msg_len, zx_handle_t* handles = nullptr,
                    size_t num_handles = 0);

  bool IsWaitingForConnectResponse();

  MessageReader& reader() { return message_reader_; }
  const MessageReader& reader() const { return message_reader_; }

  void set_remote_addr(uint32_t addr) { remote_addr_ = addr; }
  auto remote_addr() { return remote_addr_; }

 private:
  zx_status_t OnMessage(Message message) override;

  MessageReader message_reader_;
  GzIpcAgent* agent_;
  uint32_t local_addr_;
  uint32_t remote_addr_;
};

};  // namespace ree_agent
