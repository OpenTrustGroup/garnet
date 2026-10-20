// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc_client.h"

#include "garnet/lib/machina/guest.h"
namespace machina {

static int fast_call_loop(void* ctx) {
  machina::IpcClient* client = (machina::IpcClient*)ctx;
  client->FastCallLoop();
  return 0;
}

IpcClient::IpcClient(Guest* guest)
    : IpcRequestHandler(guest), enumerator_(this) {}

void IpcClient::Init(uint16_t vmid) {
  svc_->RegisterIpcEventListener(
      vmid, event_listener_binding_.NewBinding(loop_.async()));

  svc_->GetIrqInfo(vmid, &rx_ready_irq_, &tx_done_irq_);
  FXL_CHECK(!guest_->GetInterruptController()->IsPassthrough(rx_ready_irq_))
            << "rx_ready_irq: " << rx_ready_irq_;
  FXL_CHECK(!guest_->GetInterruptController()->IsPassthrough(tx_done_irq_))
            << "tx_done_irq: " << tx_done_irq_;

  svc_->GetSchedBuf(vmid, &sched_mem_pa_, &sched_mem_sz_);
  SetSchedMem();

  fidl::InterfaceHandle<IpcEventListener> handle;
  svc_->GetIpcEventListener(vmid, &handle);
  peer_.Bind(std::move(handle));

  svc_->RegisterIpcEnumerator(vmid, enumerator_.NewBinding(loop_.async()));
  svc_->EnumerateIpcChannels(vmid);
  // block until channel enumeration is completed
  loop_.RunUntilIdle();

  zx::channel srv_fast_rx, srv_fast_tx;
  svc_->SetupFastCallChannel(vmid, &srv_fast_rx, &srv_fast_tx);
  fast_server_ = std::move(srv_fast_tx);
  fast_client_ = std::move(srv_fast_rx);

  int ret;
  thrd_t fast_call_thread;
  ret = thrd_create_with_name(&fast_call_thread, fast_call_loop, this,
                              "fastcall-client");
  FXL_CHECK(ret == thrd_success);
  ret = thrd_detach(fast_call_thread);
  FXL_CHECK(ret == thrd_success);
  zx::channel srv_vm_rx, srv_vm_tx;
  svc_->SetupVMCallChannel(vmid, &srv_vm_rx, &srv_vm_tx);
  vm_server_ = std::move(srv_vm_tx);
  vm_client_ = std::move(srv_vm_rx);
  // TODO release thread in deconstructor.
  FXL_CHECK(loop_.StartThread() == ZX_OK);
}

void IpcClient::OnNewIpcChannel(uint8_t chan_id,
                                zx::vmo tx_vmo,
                                zx::vmo rx_vmo) {
  std::unique_ptr<IpcChannel> chan;
  auto status =
      IpcChannel::Create(guest_, std::move(tx_vmo), std::move(rx_vmo), &chan);
  FXL_CHECK(status == ZX_OK);
  chans_[chan_id] = std::move(chan);
}

}  // namespace machina
