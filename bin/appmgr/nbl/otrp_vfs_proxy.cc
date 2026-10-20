// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2021 The GoldenRiver Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "otrp_vfs_proxy.h"

#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <lib/async/cpp/task.h>

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/logging.h"

typedef async_wait_result_t(async_wait_handler_t)(async_t* async,
                                                  async_wait_t* wait,
                                                  zx_status_t status,
                                                  const zx_packet_signal_t* signal);

namespace component {
namespace {
typedef struct otrp_proxy_session {
  async_wait_t proxy_wait;
  async_wait_t secmgr_wait;
  bool is_root;
  OtrpVfsProxy* vfs_proxy;
} otrp_proxy_session_t;
}  // namespace

OtrpVfsProxy::~OtrpVfsProxy() {
  if (loop_ != nullptr) {
    async_loop_destroy(loop_);
  }
}

zx_status_t OtrpVfsProxy::Init(zx_handle_t otrp_channel,
                               zx_handle_t secmgr_channel) {
  otrp_proxy_session_t* otrp_proxy = nullptr;

  if (root_session_ != nullptr) {
    FXL_LOG(ERROR) << "Error: otrp root channel re-init.";
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status = async_loop_create(NULL, &loop_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create async loop.";
    loop_ = nullptr;
    return ZX_ERR_INTERNAL;
  }

  auto cleanup = fxl::MakeAutoCall([&]() {
    if (otrp_proxy != nullptr) {
      async_cancel_wait(async_, &otrp_proxy->proxy_wait);
      async_cancel_wait(async_, &otrp_proxy->secmgr_wait);
      delete otrp_proxy;
    }
    async_loop_destroy(loop_);
    loop_ = nullptr;
  });

  status = async_loop_start_thread(loop_, "otrp-proxy-service", nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create async loop thread.";
    return ZX_ERR_INTERNAL;
  }
  async_ = async_loop_get_dispatcher(loop_);

  otrp_proxy = new otrp_proxy_session_t;
  otrp_proxy->is_root = true;
  otrp_proxy->vfs_proxy = this;
  InitOtrpProxySession(otrp_proxy->proxy_wait, otrp_channel, &OtrpVfsProxy::HandleOtrpProxyMessage);
  status = async_begin_wait(async_, &otrp_proxy->proxy_wait);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed wait otrp proxy message.";
    return ZX_ERR_INTERNAL;
  }

  InitOtrpProxySession(otrp_proxy->secmgr_wait, secmgr_channel, &OtrpVfsProxy::HandleOtrpSecmgrMessage);
  status = async_begin_wait(async_, &otrp_proxy->secmgr_wait);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed wait otrp secmgr message.";
    return ZX_ERR_INTERNAL;
  }

  root_session_ = otrp_proxy;
  cleanup.cancel();

  return ZX_OK;
}

void OtrpVfsProxy::ResetSecmgrRequestChannel(zx_handle_t secmgr_channel) {
  async::PostTask(this->async_, [this, secmgr_channel]() {
    RebuildRootSession(secmgr_channel);
    for (auto it = root_clone_sessions_.begin();
         it != root_clone_sessions_.end(); it++) {
      RebuildRootCloneSession(*it, secmgr_channel);
    }
  });
}

void OtrpVfsProxy::InitOtrpProxySession(async_wait_t& wait,
                                        zx_handle_t handle,
                                        async_wait_handler_t *handler) {
  wait.handler = handler;
  wait.object = handle;
  wait.trigger = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  wait.flags = ASYNC_FLAG_HANDLE_SHUTDOWN;
}

async_wait_result_t OtrpVfsProxy::HandleOtrpProxyMessage(
    async_t* async,
    async_wait_t* wait,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  otrp_proxy_session_t* otrp_proxy = (otrp_proxy_session_t*)wait;
  if ((status == ZX_OK) && (signal->observed & ZX_CHANNEL_READABLE)) {
    otrp_proxy->vfs_proxy->HandleProxyZxioMsg(otrp_proxy);
    return ASYNC_WAIT_AGAIN;
  }

  zx_handle_close(wait->object);

  async_cancel_wait(otrp_proxy->vfs_proxy->async_, &otrp_proxy->secmgr_wait);
  otrp_proxy->vfs_proxy->RemoveOtrpProxySession(otrp_proxy);
  zx_handle_close(otrp_proxy->secmgr_wait.object);

  delete otrp_proxy;
  return ASYNC_WAIT_FINISHED;
}

zx_status_t OtrpVfsProxy::HandleProxyZxioMsg(otrp_proxy_session_t* otrp_proxy) {
  zx_handle_t proxy_channel = otrp_proxy->proxy_wait.object;
  zx_handle_t secmgr_channel = otrp_proxy->secmgr_wait.object;
  zxrio_msg_t msg;
  uint32_t actual_handles;

  zx_status_t status = zx_channel_read(proxy_channel, 0, &msg, msg.handle,
                                       sizeof(msg), 4, NULL, &actual_handles);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read message from proxy channel, status:"
                   << status;
    return status;
  }

  switch (msg.op) {
    case ZXRIO_CLONE:
      status = HandleProxyZxioCloneMsg(otrp_proxy, msg, actual_handles);
      if (status != ZX_OK) {
        return status;
      }
      break;
    case ZXRIO_OPEN:
      // clang-format off
      status = zx_channel_write(secmgr_channel, 0, &msg,
                                ZXRIO_HDR_SZ + msg.datalen,
                                msg.handle, actual_handles);
      // clang-format on
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed send zxio open msg to secmgr.";
        for (uint32_t i = 0; i < actual_handles; i++) {
          zx_handle_close(msg.handle[i]);
        }
        return status;
      }
      break;
    default:
      FXL_LOG(ERROR) << "Unable to handle zxio message, op code:" << msg.op;
      return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t OtrpVfsProxy::HandleProxyZxioCloneMsg(
    otrp_proxy_session_t* otrp_proxy,
    zxrio_msg_t& msg,
    uint32_t actual_handles) {
  zx_handle_t new_secmgr_channels[2];
  zx_handle_t new_proxy_channel;
  otrp_proxy_session_t* new_session = nullptr;
  zx_handle_t secmgr_channel = otrp_proxy->secmgr_wait.object;
  zx_status_t status;

  auto cleanup = fxl::MakeAutoCall([&]() {
    if (new_session != nullptr) {
      async_cancel_wait(async_, &new_session->proxy_wait);
      async_cancel_wait(async_, &new_session->secmgr_wait);
      delete new_session;
    }
    zx_handle_close(new_secmgr_channels[0]);
    zx_handle_close(new_secmgr_channels[1]);
    for (uint32_t i = 0; i < actual_handles; i++) {
      zx_handle_close(msg.handle[i]);
    }
  });

  if (otrp_proxy->is_root) {
    if (actual_handles < 1) {
      FXL_LOG(ERROR) << "Error : clone with no handle.";
      return ZX_ERR_INTERNAL;
    }
    new_proxy_channel = msg.handle[0];
    zx_channel_create(0, &new_secmgr_channels[0], &new_secmgr_channels[1]);
    msg.handle[0] = new_secmgr_channels[0];
    new_session = new otrp_proxy_session_t;
    new_session->is_root = true;
    new_session->vfs_proxy = this;
    InitOtrpProxySession(new_session->proxy_wait, new_proxy_channel, &OtrpVfsProxy::HandleOtrpProxyMessage);
    status = async_begin_wait(async_, &new_session->proxy_wait);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed wait otrp proxy message.";
      return ZX_ERR_INTERNAL;
    }
    InitOtrpProxySession(new_session->secmgr_wait, new_secmgr_channels[1], &OtrpVfsProxy::HandleOtrpSecmgrMessage);
    status = async_begin_wait(async_, &new_session->secmgr_wait);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed wait otrp secmgr message.";
      return ZX_ERR_INTERNAL;
    }
  }

  status = zx_channel_write(secmgr_channel, 0, &msg, ZXRIO_HDR_SZ + msg.datalen,
                            msg.handle, actual_handles);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed send zxio msg to secmgr.";
    return status;
  }

  if (new_session->is_root) {
    root_clone_sessions_.push_back(new_session);
  }

  cleanup.cancel();
  return ZX_OK;
}

async_wait_result_t OtrpVfsProxy::HandleOtrpSecmgrMessage(
    async_t* async,
    async_wait_t* wait,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  // Now there isn't any message received from secmgr, so we only
  // need to handle ZX_CHANNEL_PEER_CLOSED signal
  if ((status == ZX_OK) && !(signal->observed & ZX_CHANNEL_PEER_CLOSED)) {
    FXL_LOG(ERROR) << "Error : unexpected singal: " << signal->observed;
    return ASYNC_WAIT_AGAIN;
  }
  zx_handle_close(wait->object);
  return ASYNC_WAIT_FINISHED;
}

void OtrpVfsProxy::RemoveOtrpProxySession(otrp_proxy_session_t* session) {
  for (auto it = root_clone_sessions_.begin();
       it != root_clone_sessions_.end();) {
    if (*it == session) {
      it = root_clone_sessions_.erase(it);
    } else {
      ++it;
    }
  }
}

void OtrpVfsProxy::RebuildRootSession(zx_handle_t secmgr_channel) {
  async_cancel_wait(async_, &root_session_->secmgr_wait);
  zx_handle_close(root_session_->secmgr_wait.object);

  root_session_->secmgr_wait.object = secmgr_channel;
  zx_status_t status = async_begin_wait(async_, &root_session_->secmgr_wait);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed wait otrp secmgr message, status:" << status;
    zx_handle_close(secmgr_channel);
    return;
  }
}

void OtrpVfsProxy::RebuildRootCloneSession(otrp_proxy_session_t* session,
                                           zx_handle_t secmgr_channel) {
  zx_status_t status;
  zx_handle_t new_secmgr_channels[2];

  auto cleanup = fxl::MakeAutoCall([&]() {
    async_cancel_wait(async_, &session->secmgr_wait);
    zx_handle_close(new_secmgr_channels[0]);
    zx_handle_close(new_secmgr_channels[1]);
  });

  async_cancel_wait(async_, &session->secmgr_wait);
  zx_handle_close(session->secmgr_wait.object);

  zx_channel_create(0, &new_secmgr_channels[0], &new_secmgr_channels[1]);
  status = SendZxioCloneMsgToSecmgr(secmgr_channel, new_secmgr_channels[0]);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed send clone msg to secmgr, status:" << status;
    return;
  }

  session->secmgr_wait.object = new_secmgr_channels[1];
  status = async_begin_wait(async_, &session->secmgr_wait);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed wait otrp secmgr message, status:" << status;
    return;
  }

  cleanup.cancel();
}

zx_status_t OtrpVfsProxy::SendZxioCloneMsgToSecmgr(zx_handle_t svc,
                                                   zx_handle_t srv) {
  zxrio_msg_t msg;

  memset(&msg, 0, ZXRIO_HDR_SZ);
  msg.op = ZXRIO_CLONE;
  msg.datalen = 0;
  msg.arg = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE;
  msg.arg2.mode = 0755;
  msg.hcount = 1;
  msg.handle[0] = srv;

  zx_status_t r;
  if ((r = zx_channel_write(svc, 0, &msg, ZXRIO_HDR_SZ + msg.datalen,
                            msg.handle, 1)) < 0) {
    return r;
  }

  return ZX_OK;
}

}  // namespace component
