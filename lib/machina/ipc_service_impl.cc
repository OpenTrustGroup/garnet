// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc_service_impl.h"

#include <fbl/auto_lock.h>

#include <stdio.h>
static int fast_call_loop(void* ctx) {
  machina::IpcRequestHandler *ipcRequest = (machina::IpcRequestHandler *)ctx;
  ipcRequest->FastCallLoop();
  return 0;
}

namespace machina {

IpcServiceImpl::IpcServiceImpl(
    component::ApplicationContext* application_context,
    Guest* guest,
    const std::vector<machina::IpcMboxSpec> &ipcmbox)
    : IpcRequestHandler(guest) {
  application_context->outgoing_services()->AddService<IpcService>(
      [this](fidl::InterfaceRequest<IpcService> request) {
        bindings_.AddBinding(this, std::move(request));
      });

  for (size_t i = 0; i < ipcmbox.size(); ++i) {
    std::unique_ptr<IpcRequestHandler> ipcRequestHandler =
      std::make_unique<machina::IpcRequestHandler>(
        guest,
        ipcmbox[i],
        i
      );
    FXL_CHECK(ipcRequestHandler != NULL);
    std::unique_ptr<IpcEnumeratorSyncPtr> ipcEnumeratorSyncPtr =
                        std::make_unique<machina::IpcEnumeratorSyncPtr>();
    FXL_CHECK(ipcEnumeratorSyncPtr != NULL);

    for (uint8_t j = 0; j < kNumChannels; j++) {
      std::unique_ptr<IpcChannel> chan;
      auto status = IpcChannel::Create(guest, &chan, i);
      FXL_CHECK(status == ZX_OK);

      ipcRequestHandler->chan_array()[j] = std::move(chan);
    }
    ipchandlermap_.insert(std::make_pair(i, std::move(ipcRequestHandler)));
    enumeratorsync_.push_back(std::move(ipcEnumeratorSyncPtr));
  }

  ipcmbox_.assign(ipcmbox.begin(), ipcmbox.end());
  FXL_CHECK(loop_.StartThread() == ZX_OK);
}

void IpcServiceImpl::EnumerateIpcChannels(
    uint16_t vmid,
    EnumerateIpcChannelsCallback callback) {
  auto enumerator_itor = enumeratormap_.find(vmid);
  FXL_CHECK(enumerator_itor != enumeratormap_.end());
  auto enumerator = enumerator_itor->second.get();
  FXL_CHECK(enumerator->is_bound());

  machina::IpcRequestHandler *ipchandler = NULL;
  for (auto it = ipchandlermap_.begin();
            it != ipchandlermap_.end(); ++it) {
    ipchandler = it->second.get();
    if (ipchandler->get_mailbox_vmid() == vmid)
      break;
  }
  FXL_CHECK(ipchandler->get_mailbox_vmid() == vmid);
  for (auto& chan : ipchandler->chan_array()) {
    zx::vmo tx_vmo, rx_vmo;
    auto id = chan->id();
    chan->DuplicateVmoForPeer(&tx_vmo, &rx_vmo);
    auto success =
        (*enumerator)->OnNewIpcChannel(id%kNumChannels, std::move(tx_vmo), std::move(rx_vmo));
    FXL_CHECK(success);
  }
  callback();
}

void IpcServiceImpl::GetIrqInfo(uint16_t vmid, GetIrqInfoCallback callback) {
  machina::IpcRequestHandler *ipchandler = NULL;
  for (auto it = ipchandlermap_.begin();
            it != ipchandlermap_.end(); ++it) {
    ipchandler = it->second.get();
    if (ipchandler->get_mailbox_vmid() == vmid)
      break;
  }
  FXL_CHECK(ipchandler->get_mailbox_vmid() == vmid);
  callback(ipchandler->get_rx_ready_irq(), ipchandler->get_tx_done_irq());
}

void IpcServiceImpl::GetSchedBuf(uint16_t vmid, GetSchedBufCallback callback) {
  machina::IpcRequestHandler *ipchandler = NULL;
  for (auto it = ipchandlermap_.begin();
            it != ipchandlermap_.end(); ++it) {
    ipchandler = it->second.get();
    if (ipchandler->get_mailbox_vmid() == vmid)
      break;
  }
  FXL_CHECK(ipchandler->get_mailbox_vmid() == vmid);
  callback(ipchandler->get_sched_mem_pa(), ipchandler->get_sched_mem_sz());
}

void IpcServiceImpl::SetupFastCallChannel(uint16_t vmid, SetupFastCallChannelCallback callback) {
  machina::IpcRequestHandler *ipchandler = NULL;
  for (auto it = ipchandlermap_.begin();
            it != ipchandlermap_.end(); ++it) {
    ipchandler = it->second.get();
    if (ipchandler->get_mailbox_vmid() == vmid)
      break;
  }
  FXL_CHECK(ipchandler->get_mailbox_vmid() == vmid);

  zx::channel local_rx, remote_rx;
  zx_status_t status = zx::channel::create(0u, &local_rx, &remote_rx);
  FXL_CHECK(status == ZX_OK);
  ipchandler->set_fast_server(std::move(local_rx));

  zx::channel local_tx, remote_tx;
  status = zx::channel::create(0u, &local_tx, &remote_tx);
  FXL_CHECK(status == ZX_OK);
  ipchandler->set_fast_client(std::move(local_tx));

  thrd_t thread;
  std::string thread_name = "fastcall-srv-";
  thread_name += std::to_string(vmid);
  int ret = thrd_create_with_name(&thread, fast_call_loop, ipchandler,
                                    thread_name.c_str());
  FXL_CHECK(ret == thrd_success);
  ret = thrd_detach(thread);
  FXL_CHECK(ret == thrd_success);

  // TODO release thread in deconstructor.
  callback(std::move(remote_rx), std::move(remote_tx));
}

void IpcServiceImpl::SetupVMCallChannel(uint16_t vmid, SetupVMCallChannelCallback callback) {
  machina::IpcRequestHandler *ipchandler = NULL;
  for (auto it = ipchandlermap_.begin();
            it != ipchandlermap_.end(); ++it) {
    ipchandler = it->second.get();
    if (ipchandler->get_mailbox_vmid() == vmid)
      break;
  }
  FXL_CHECK(ipchandler->get_mailbox_vmid() == vmid);
  zx::channel local_rx, remote_rx;
  zx_status_t status = zx::channel::create(0u, &local_rx, &remote_rx);
  FXL_CHECK(status == ZX_OK);
  ipchandler->set_vm_server(std::move(local_rx));

  zx::channel local_tx, remote_tx;
  status = zx::channel::create(0u, &local_tx, &remote_tx);
  FXL_CHECK(status == ZX_OK);
  ipchandler->set_vm_client(std::move(local_tx));

  callback(std::move(remote_rx), std::move(remote_tx));
}

void IpcServiceImpl::RegisterIpcEventListener(
    uint16_t vmid,
    fidl::InterfaceHandle<IpcEventListener> handle,
    RegisterIpcEventListenerCallback callback) {
  machina::IpcRequestHandler *ipc_req_handler = NULL;
  for (auto it = ipchandlermap_.begin();
            it != ipchandlermap_.end(); ++it) {
    ipc_req_handler = it->second.get();
    if (ipc_req_handler->get_mailbox_vmid() == vmid) {
      ipc_req_handler->getIpcEventListernerPtr().Bind(std::move(handle));
      callback();
      return;
    }
  }
  for (auto it = ipchandlermap_.begin();
            it != ipchandlermap_.end(); ++it) {
    ipc_req_handler = it->second.get();
    if (ipc_req_handler->get_mailbox_vmid() == -1) {
      ipc_req_handler->set_mailbox_vmid(vmid);
      ipc_req_handler->getIpcEventListernerPtr().Bind(std::move(handle));
      callback();
      return;
    }
  }
}

void IpcServiceImpl::RegisterIpcEnumerator(
    uint16_t vmid,
    fidl::InterfaceHandle<IpcEnumerator> handle,
    RegisterIpcEnumeratorCallback callback) {
  auto it = enumeratormap_.find(vmid);

  if (it == enumeratormap_.end()) {
    FXL_CHECK(enumeratorsync_.size() > 0);
    auto &enumer = enumeratorsync_.front();
    FXL_CHECK(enumer != NULL);
    enumer->Bind(std::move(handle));
    enumeratormap_.insert(std::make_pair(vmid, std::move(enumer)));
    enumeratorsync_.erase(enumeratorsync_.begin());
  } else {
    auto enumer = it->second.get();
    enumer->Bind(std::move(handle));
  }
  callback();
}

void IpcServiceImpl::GetIpcEventListener(uint16_t vmid, GetIpcEventListenerCallback callback) {
  machina::IpcRequestHandler *ipc_req_handler = NULL;
  for (auto it = ipchandlermap_.begin();
            it != ipchandlermap_.end(); ++it) {
    ipc_req_handler = it->second.get();
    if (ipc_req_handler->get_mailbox_vmid() == vmid)
      break;
  }
  if (ipc_req_handler->get_mailbox_vmid() == vmid)
    callback(ipc_req_handler->getIpcEventener().NewBinding(loop_.async()));
  else
    FXL_LOG(ERROR) << "GetIpcEventListener vmid " << vmid << " not found";
}

}  // namespace machina
