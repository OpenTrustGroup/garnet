// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio_irq_service_impl.h"

#include "garnet/lib/machina/guest.h"

namespace machina {

AudioIrqServiceImpl::AudioIrqServiceImpl(
    component::ApplicationContext* application_context,
    const std::vector<AudioIrqSpecEx>& audio_irq_specs,
    Guest* guest)
    : audio_irq_specs_(audio_irq_specs), guest_(guest) {
  application_context->outgoing_services()->AddService<AudioIrqService>(
      [this](fidl::InterfaceRequest<AudioIrqService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void AudioIrqServiceImpl::RegisterAudioIrqListener(
    uint32_t vmid,
    fidl::InterfaceHandle<AudioIrqListener> handle,
    zx::channel chan,
    RegisterAudioIrqListenerCallback callback) {
  listener_map_[vmid].Bind(std::move(handle));
  cb_chan_map_[vmid] = std::move(chan);
  callback();
}

void AudioIrqServiceImpl::PopulateAudioIrq(uint32_t vmid,
                                           PopulateAudioIrqCallback callback) {
  if (!listener_map_[vmid].is_bound())
    return;

  for (auto& spec : audio_irq_specs_) {
    // TODO: currently only one UOS is assumed. Thus, bitmask not interested by
    // SOS will be assumed to be interested by UOS.
    listener_map_[vmid]->OnNewNoBitmaskAudioIrq(spec.vector);
  }
  callback();
}

void AudioIrqServiceImpl::UpdateAfeIrq(uint32_t irq_nr,
                                       uint32_t irq_status_offset,
                                       uint32_t irq_clear_offset,
                                       uint64_t interested_bitmask,
                                       uint32_t vmid,
                                       uint32_t index,
                                       UpdateAfeIrqCallback callback) {
  guest_->UpdateAudioIrqSpec(irq_nr, irq_status_offset, irq_clear_offset,
                             interested_bitmask, vmid, index);
  callback();
}

void AudioIrqServiceImpl::UpdateAfeIrqRegs(uint32_t vector,
                                           UpdateAfeIrqRegsCallback callback) {
  guest_->UpdateAfeIrqRegsMap(vector);
  callback();
}

zx_txid_t AudioIrqServiceImpl::GetNextTxid(uint32_t vmid) {
  zx_txid_t txid = 0;
  while (!txid) {
    txid = next_txid_map_[vmid].fetch_add(1, std::memory_order_relaxed);
  }
  return txid;
}

void AudioIrqServiceImpl::ForwardIrqEvent(uint16_t vector,
                                          uint64_t pending_bitmask,
                                          uint32_t vmid) {
  auto it = cb_chan_map_.find(vmid);
  if (it != cb_chan_map_.end()) {
    struct forward_ipi_call_arg arg;
    arg.txid = GetNextTxid(vmid);
    arg.vector = vector;
    arg.pending_bitmask = pending_bitmask;

    //  struct forward_ipi_call_rsp rsp;
    //  rsp.status = 0;

    //  zx_channel_call_args_t args;
    //  args.wr_bytes = &arg;
    //  args.wr_num_bytes = sizeof(arg);
    //  args.wr_handles = NULL;
    //  args.wr_num_handles = 0;
    //  args.rd_bytes = &rsp;
    //  args.rd_num_bytes = sizeof(rsp);
    //  args.rd_num_handles = 0;
    //  args.rd_handles = NULL;

    //  uint32_t bytes_read;
    //  uint32_t handles_read;
    //  zx_status_t read_status;
    it->second.write(0, &arg, sizeof(arg), NULL, 0);
  }
  //  cb_chan_.call(0, zx::time::infinite(), &args, &bytes_read,
  //      &handles_read, &read_status);
  //  if (!listener_.is_bound())
  //    return;
  //
  //  listener_->OnPendingIrqEvent(vector, irq_status_offset, irq_clear_offset,
  //  pending_bitmask);
}

}  // namespace machina
