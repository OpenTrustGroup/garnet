// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <fbl/auto_call.h>
#include <lib/async-loop/cpp/loop.h>
#include <sysmgr/cpp/fidl.h>

#include "lib/gzos/trusty_app/manifest.h"
#include "lib/gzos/trusty_app/trusty_std.h"
#include "lib/gzos/trusty_app/uapi/err.h"
#include "lib/gzos/trusty_ipc/cpp/channel.h"
#include "lib/gzos/trusty_ipc/cpp/object_manager.h"
#include "lib/gzos/trusty_ipc/cpp/port.h"

#include "lib/app/cpp/environment_services.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/logging.h"

using namespace trusty_ipc;

static fbl::Mutex context_lock;
static std::unique_ptr<fuchsia::sys::StartupContext> startup_context;
static sysmgr::ServiceRegistrySyncPtr service_registry;
static sysmgr::ServiceRegistryPtr service_registry_async;

static fbl::Mutex async_loop_lock;
static bool async_loop_started = false;
static async::Loop* loop_ptr;

static inline long zx_status_to_lk_err(zx_status_t status) {
  if (status == ZX_OK) {
    return NO_ERROR;
  }

  switch (status) {
    case ZX_ERR_INTERNAL:
      return ERR_FAULT;
    case ZX_ERR_NOT_SUPPORTED:
      return ERR_NOT_SUPPORTED;
    case ZX_ERR_NO_RESOURCES:
      return ERR_NO_RESOURCES;
    case ZX_ERR_NO_MEMORY:
      return ERR_NO_MEMORY;
    case ZX_ERR_INVALID_ARGS:
      return ERR_INVALID_ARGS;
    case ZX_ERR_BAD_HANDLE:
      return ERR_BAD_HANDLE;
    case ZX_ERR_WRONG_TYPE:
      return ERR_BAD_HANDLE;
    case ZX_ERR_BAD_SYSCALL:
      return ERR_NOT_VALID;
    case ZX_ERR_OUT_OF_RANGE:
      return ERR_OUT_OF_RANGE;
    case ZX_ERR_BUFFER_TOO_SMALL:
    case ZX_ERR_UNAVAILABLE:
      return ERR_NOT_ENOUGH_BUFFER;
    case ZX_ERR_BAD_STATE:
      return ERR_BAD_STATE;
    case ZX_ERR_TIMED_OUT:
      return ERR_TIMED_OUT;
    case ZX_ERR_SHOULD_WAIT:
      return ERR_NOT_READY;
    case ZX_ERR_CANCELED:
      return ERR_CANCELLED;
    case ZX_ERR_PEER_CLOSED:
      return ERR_CHANNEL_CLOSED;
    case ZX_ERR_NOT_FOUND:
      return ERR_NOT_FOUND;
    case ZX_ERR_ALREADY_EXISTS:
      return ERR_ALREADY_EXISTS;
    case ZX_ERR_ALREADY_BOUND:
      return ERR_ALREADY_EXISTS;
    case ZX_ERR_ACCESS_DENIED:
      return ERR_ACCESS_DENIED;
    case ZX_ERR_IO:
      return ERR_IO;
    default:
      FXL_DLOG(WARNING) << "Unsupported status: " << status;
      return ERR_GENERIC;
  }
}

static constexpr char kScanUuidFormatString[] =
    "%8" SCNx32
    "-"
    "%4" SCNx16
    "-"
    "%4" SCNx16
    "-"
    "%2" SCNx8 "%2" SCNx8
    "-"
    "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8;

static inline void string_to_uuid(std::string& str, uuid_t* uuid) {
  sscanf(str.c_str(), kScanUuidFormatString, &uuid->time_low, &uuid->time_mid,
         &uuid->time_hi_and_version, &uuid->clock_seq_and_node[0],
         &uuid->clock_seq_and_node[1], &uuid->clock_seq_and_node[2],
         &uuid->clock_seq_and_node[3], &uuid->clock_seq_and_node[4],
         &uuid->clock_seq_and_node[5], &uuid->clock_seq_and_node[6],
         &uuid->clock_seq_and_node[7]);
}

static zx_status_t get_object_by_id(TipcObject::ObjectType type,
                                    uint32_t handle_id,
                                    fbl::RefPtr<TipcObject>* obj_out) {
  auto obj_mgr = TipcObjectManager::Instance();
  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = obj_mgr->GetObject(handle_id, &obj);
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to get object, handle_id: " << handle_id
                << " status: " << status;
    return status;
  }

  if (type != TipcObject::ANY) {
    if (obj->type() != type) {
      FXL_VLOG(1) << "Tipc object type mismatch, expect: " << type
                  << " actual: " << obj->type() << " handle_id: " << handle_id;
      return ZX_ERR_INVALID_ARGS;
    }
  }

  *obj_out = fbl::move(obj);
  return ZX_OK;
}

long trusty_nanosleep(uint32_t clock_id, uint32_t flags, uint64_t sleep_time) {
  zx_status_t status = zx_nanosleep(zx_deadline_after(sleep_time));
  return zx_status_to_lk_err(status);
}

long port_create(const char* path, uint32_t num_recv_bufs,
                 uint32_t recv_buf_size, uint32_t flags) {
  // async_loop and startup_context objects should be created in main thread
  // so they can't be declared as global variable. We assume that the first time
  // TA to do port_create() is always in main thread.
  static async::Loop loop(&kAsyncLoopConfigMakeDefault);

  fbl::AutoLock lock(&context_lock);
  if (startup_context == nullptr) {
    startup_context =
        fbl::move(fuchsia::sys::StartupContext::CreateFromStartupInfo());

    fbl::AutoLock lock(&async_loop_lock);
    loop_ptr = &loop;

    fuchsia::sys::ConnectToEnvironmentService<sysmgr::ServiceRegistry>(
        service_registry.NewRequest());
    fuchsia::sys::ConnectToEnvironmentService<sysmgr::ServiceRegistry>(
        service_registry_async.NewRequest());
  }

  std::string service_name(path);
  if (service_name.empty() || (service_name.size() >= kTipcPortPathMax)) {
    return ERR_INVALID_ARGS;
  }

  if (num_recv_bufs == 0 || (num_recv_bufs > kTipcChanMaxBufItems)) {
    return ERR_INVALID_ARGS;
  }

  if (recv_buf_size == 0 || (recv_buf_size > kTipcChanMaxBufSize)) {
    return ERR_INVALID_ARGS;
  }

  auto port =
      fbl::MakeRefCounted<TipcPortImpl>(num_recv_bufs, recv_buf_size, flags);
  if (port == nullptr) {
    return ERR_NO_MEMORY;
  }

  auto obj_mgr = TipcObjectManager::Instance();
  zx_status_t status = obj_mgr->InstallObject(port);
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to install object: " << status;
    return zx_status_to_lk_err(status);
  }
  auto remove_port = fbl::MakeAutoCall(
      [&obj_mgr, &port]() { obj_mgr->RemoveObject(port->handle_id()); });

  status = startup_context->outgoing().AddPublicService<TipcPort>(
      [port](fidl::InterfaceRequest<TipcPort> request) {
        port->Bind(std::move(request));
      },
      service_name);

  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to publish port service: " << status;
    return zx_status_to_lk_err(status);
  }

  service_registry->AddService(service_name);

  port->set_name(path);
  remove_port.cancel();
  return (long)port->handle_id();
}

static void wait_for_port(fbl::RefPtr<TipcChannelImpl> channel,
                          std::string path) {
  auto port_connect = [path, channel] {
    PortConnectFacade facade(
        std::move(channel), [](TipcPortSyncPtr& port, std::string path) {
          fuchsia::sys::ConnectToEnvironmentService<TipcPort>(port.NewRequest(),
                                                              path);
          return ZX_OK;
        });
    zx_status_t err =
        facade.Connect(path, trusty_app::Manifest::Instance()->GetUuid());
    if (err != ZX_OK) {
      FXL_VLOG(1) << "Failed to connect " << path << ", err=" << err;
      channel->SignalEvent(TipcEvent::HUP);
    }
  };

  service_registry_async->WaitOnService(path,
                                        [port_connect] { port_connect(); });

  channel->SetCloseCallback(
      [path]() { service_registry->CancelWaitOnService(path); });
}

long connect(const char* path, uint32_t flags) {
  if (!path) {
    return ERR_INVALID_ARGS;
  }

  size_t path_len = strlen(path);
  if (path_len == 0 || (path_len >= kTipcPortPathMax)) {
    return ERR_INVALID_ARGS;
  }

  fbl::RefPtr<TipcChannelImpl> channel;
  channel = fbl::MakeRefCounted<TipcChannelImpl>();
  if (!channel) {
    FXL_VLOG(1) << "Failed to create tipc channel";
    return ERR_NO_MEMORY;
  }

  channel->SetReadyCallback(
      [channel] { channel->SignalEvent(TipcEvent::READY); });

  PortConnectFacade facade(
      channel, [flags, channel](TipcPortSyncPtr& port, std::string path) {
        bool found;
        service_registry->LookupService(path, &found);
        if (!found) {
          if (flags & IPC_CONNECT_WAIT_FOR_PORT) {
            wait_for_port(channel, path);
            return ZX_ERR_SHOULD_WAIT;
          }

          return ZX_ERR_NOT_FOUND;
        }
        fuchsia::sys::ConnectToEnvironmentService<TipcPort>(port.NewRequest(),
                                                            path);
        return ZX_OK;
      });

  zx_status_t status =
      facade.Connect(path, trusty_app::Manifest::Instance()->GetUuid());
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto obj_mgr = TipcObjectManager::Instance();
  status = obj_mgr->InstallObject(channel);
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to install object: " << status;
    return zx_status_to_lk_err(status);
  }

  if (flags & IPC_CONNECT_ASYNC) {
    return channel->handle_id();
  }

  auto close_channel = fbl::MakeAutoCall([&channel]() { channel->Close(); });

  WaitResult result;
  status = channel->Wait(&result, zx::time::infinite());
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to wait event: " << status;
    return zx_status_to_lk_err(status);
  }

  if ((result.event & TipcEvent::HUP) && !(result.event & TipcEvent::MSG)) {
    return ERR_CHANNEL_CLOSED;
  }

  if (!(result.event & TipcEvent::READY)) {
    return ERR_NOT_READY;
  }

  close_channel.cancel();
  return channel->handle_id();
}

long accept(uint32_t handle_id, uuid_t* peer_uuid) {
  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::PORT, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto port = fbl::RefPtr<TipcPortImpl>::Downcast(fbl::move(obj));

  std::string uuid_str;
  fbl::RefPtr<TipcChannelImpl> new_channel;
  status = port->Accept(&uuid_str, &new_channel);
  if (status == ZX_ERR_SHOULD_WAIT) {
    return ERR_NO_MSG;
  }
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to accept new connection,"
                << " handle_id: " << handle_id << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  if (peer_uuid) {
    if (uuid_str.empty()) {
      memset(peer_uuid, 0, sizeof(uuid_t));
    } else {
      string_to_uuid(uuid_str, peer_uuid);
    }
  }

  return (long)new_channel->handle_id();
}

long trusty_close(uint32_t handle_id) {
  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::ANY, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  if (obj->is_port()) {
    fbl::AutoLock lock(&context_lock);

    auto port = fbl::RefPtr<TipcPortImpl>::Downcast(obj);

    status =
        startup_context->outgoing().RemovePublicService<TipcPort>(port->name());
    if (status == ZX_OK) {
      service_registry->RemoveService(port->name());
    }
  }

  obj->Close();

  return zx_status_to_lk_err(status);
}

long set_cookie(uint32_t handle_id, void* cookie) {
  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::ANY, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  obj->set_cookie(cookie);
  return NO_ERROR;
}

long handle_set_create(void) {
  auto hset = fbl::MakeRefCounted<TipcObjectSet>();
  if (hset == nullptr) {
    return ERR_NO_MEMORY;
  }

  zx_status_t status = TipcObjectManager::Instance()->InstallObject(hset);
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to install hset object: " << status;
    return zx_status_to_lk_err(status);
  }
  return (long)hset->handle_id();
}

long handle_set_ctrl(uint32_t hset_id, uint32_t cmd, struct uevent* evt) {
  FXL_DCHECK(evt);

  fbl::RefPtr<TipcObject> hset_obj;
  zx_status_t status =
      get_object_by_id(TipcObject::OBJECT_SET, hset_id, &hset_obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto hset = fbl::RefPtr<TipcObjectSet>::Downcast(fbl::move(hset_obj));

  fbl::RefPtr<TipcObject> child_obj;
  status = get_object_by_id(TipcObject::ANY, evt->handle, &child_obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  switch (cmd) {
    case HSET_ADD:
      status = hset->AddObject(child_obj, evt->cookie, evt->event);
      break;
    case HSET_DEL:
      hset->RemoveObject(child_obj);
      status = ZX_OK;
      break;
    case HSET_MOD:
      status = hset->ModifyObject(child_obj, evt->cookie, evt->event);
      break;
    default:
      FXL_LOG(ERROR) << "Invalid hset cmd: " << cmd;
      status = ZX_ERR_INVALID_ARGS;
      break;
  }

  return zx_status_to_lk_err(status);
}

// Message loop will start to serve request when TA start to wait event.
// TA should guarantee that all ports are published by port_create()
// before message loop start, or port connect request might be lost.
static void start_message_loop() {
  fbl::AutoLock lock(&async_loop_lock);
  if (!async_loop_started) {
    FXL_DCHECK(loop_ptr);

    // We need exactly 2 message loop threads
    loop_ptr->StartThread();
    loop_ptr->StartThread();

    async_loop_started = true;
  }
}

long wait(uint32_t handle_id, uevent_t* event, uint32_t timeout_ms) {
  start_message_loop();

  fbl::RefPtr<TipcObject> obj;

  zx_status_t status = get_object_by_id(TipcObject::ANY, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  WaitResult result;
  if (timeout_ms == UINT32_MAX) {
    status = obj->Wait(&result, zx::time::infinite());
  } else {
    status = obj->Wait(&result, zx::deadline_after(zx::msec(timeout_ms)));
  }

  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to wait event, handle_id: " << handle_id
                << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  event->handle = result.handle_id;
  event->event = result.event;
  event->cookie = result.cookie;
  return NO_ERROR;
}

long wait_any(uevent_t* event, uint32_t timeout_ms) {
  start_message_loop();

  WaitResult result;
  zx_status_t status;
  auto obj_mgr = TipcObjectManager::Instance();

  if (timeout_ms == UINT32_MAX) {
    status = obj_mgr->Wait(&result, zx::time::infinite());
  } else {
    status = obj_mgr->Wait(&result, zx::deadline_after(zx::msec(timeout_ms)));
  }

  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to wait any events: " << status;
    return zx_status_to_lk_err(status);
  }

  event->handle = result.handle_id;
  event->event = result.event;
  event->cookie = result.cookie;
  return NO_ERROR;
}

long get_msg(uint32_t handle_id, ipc_msg_info_t* msg_info) {
  FXL_DCHECK(msg_info);

  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::CHANNEL, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto channel = fbl::RefPtr<TipcChannelImpl>::Downcast(fbl::move(obj));
  uint32_t msg_id;
  size_t len;
  status = channel->GetMessage(&msg_id, &len);
  if (status == ZX_ERR_SHOULD_WAIT) {
    return ERR_NO_MSG;
  }

  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to get message, handle_id: " << handle_id
                << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  msg_info->id = msg_id;
  msg_info->len = len;
  // TODO(james): support handle transfer
  msg_info->num_handles = 0;

  return NO_ERROR;
}

long read_msg(uint32_t handle_id, uint32_t msg_id, uint32_t offset,
              ipc_msg_t* msg) {
  if (!msg) {
    return ERR_FAULT;
  }

  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::CHANNEL, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto channel = fbl::RefPtr<TipcChannelImpl>::Downcast(fbl::move(obj));
  size_t actual_read = 0;
  status = channel->ReadMessage(msg_id, offset, msg, actual_read);
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to read message, handle_id: " << handle_id
                << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  return actual_read;
}

long put_msg(uint32_t handle_id, uint32_t msg_id) {
  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::CHANNEL, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto channel = fbl::RefPtr<TipcChannelImpl>::Downcast(fbl::move(obj));
  status = channel->PutMessage(msg_id);
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to put message, handle_id: " << handle_id
                << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  return NO_ERROR;
}

long send_msg(uint32_t handle_id, ipc_msg_t* msg) {
  if (!msg) {
    return ERR_FAULT;
  }

  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::CHANNEL, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto channel = fbl::RefPtr<TipcChannelImpl>::Downcast(fbl::move(obj));
  size_t actual_send = 0;
  status = channel->SendMessage(msg, actual_send);
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to send message, handle_id: " << handle_id
                << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  return actual_send;
}
