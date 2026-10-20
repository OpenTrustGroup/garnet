// SPDX-License-Identifier: BSD-3-Clause


// Copyright 2021 The GoldenRiver Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_OTRP_VFS_PROXY_H_

#include <vector>

#include <fdio/remoteio.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/wait.h>

namespace component {
namespace {
typedef struct otrp_proxy_session otrp_proxy_session_t;
}

class OtrpVfsProxy {
 public:
  ~OtrpVfsProxy();
  zx_status_t Init(zx_handle_t otrp_channel, zx_handle_t secmgr_channel);
  void ResetSecmgrRequestChannel(zx_handle_t channel);

 private:
  async_loop_t* loop_;
  async_t* async_;
  std::vector<otrp_proxy_session_t*> root_clone_sessions_;
  otrp_proxy_session_t* root_session_;
  void InitOtrpProxySession(async_wait_t& wait,
                            zx_handle_t handle,
                            async_wait_handler_t* handler);
  static async_wait_result_t HandleOtrpProxyMessage(
      async_t* async,
      async_wait_t* wait,
      zx_status_t status,
      const zx_packet_signal_t* signal);
  zx_status_t HandleProxyZxioMsg(otrp_proxy_session_t* otrp_proxy);
  zx_status_t HandleProxyZxioCloneMsg(otrp_proxy_session_t* session,
                                      zxrio_msg_t& msg,
                                      uint32_t actual_handles);
  void RemoveOtrpProxySession(otrp_proxy_session_t* session);
  static async_wait_result_t HandleOtrpSecmgrMessage(
      async_t* async,
      async_wait_t* wait,
      zx_status_t status,
      const zx_packet_signal_t* signal);
  void RebuildRootSession(zx_handle_t secmgr_channel);
  void RebuildRootCloneSession(otrp_proxy_session_t* session,
                               zx_handle_t secmgr_channel);
  zx_status_t SendZxioCloneMsgToSecmgr(zx_handle_t svc, zx_handle_t srv);
};
}  // namespace component
#endif  // GARNET_BIN_APPMGR_OTRP_VFS_PROXY_H
