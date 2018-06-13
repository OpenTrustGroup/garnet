// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>

#include <fs/managed-vfs.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/vfs.h>
#include <lib/async-loop/cpp/loop.h>

#include <ree_agent/cpp/fidl.h>

#include "garnet/bin/gzos/ree_agent/ree_agent.h"

namespace ree_agent {

class TipcAgent : public ReeAgent {
 public:
  TipcAgent(uint32_t id, zx::channel ch, size_t max_msg_size);
  ~TipcAgent();

  zx_status_t Start() override;
  zx_status_t Stop() override;
  zx_status_t HandleMessage(void* buf, size_t size) override;

 private:
  zx_status_t SendMessage(uint32_t local, uint32_t remote, void* data,
                          size_t data_len);
};

}  // namespace ree_agent
