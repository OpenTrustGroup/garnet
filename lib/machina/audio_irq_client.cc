// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio_irq_client.h"

#include "garnet/lib/machina/audio_irq.h"
#include "garnet/lib/machina/guest.h"

namespace machina {

static constexpr uintptr_t kNormalPriority = 16;
static constexpr uintptr_t kLooperPriority = kNormalPriority + 2;
static constexpr uintptr_t kIRQPriority = kLooperPriority + 2;

AudioIrqClient::AudioIrqClient() : listener_(this) {}

void AudioIrqClient::FastCallLoop() {
  zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  zx_signals_t pending = 0;
  zx_status_t status;

  zx_thread_set_priority(kIRQPriority);

  while ((status = cb_chan_.wait_one(signals, zx::time::infinite(),
                                     &pending)) == ZX_OK) {
    if (pending & ZX_CHANNEL_READABLE) {
      struct forward_ipi_call_arg arg;
      //      struct forward_ipi_call_rsp rsp;
      uint32_t actual_bytes = 0;
      uint32_t msg_size = sizeof(arg);

      do {
        status = cb_chan_.read(0, &arg, msg_size, &actual_bytes, nullptr, 0,
                               nullptr);
        if (status == ZX_OK) {
          FXL_CHECK(actual_bytes == msg_size);
          OnPendingIrqEvent(arg.vector, arg.pending_bitmask);

          //          rsp.txid = arg.txid;
          //          rsp.status = ZX_OK;
          //          cb_chan_.write(0, &rsp, sizeof(rsp), NULL, 0);
        }
      } while (status == ZX_OK);
    } else if (pending & ZX_CHANNEL_PEER_CLOSED) {
      break;
    }
  }
}

static int fast_call_loop(void* ctx) {
  AudioIrqClient* client = (AudioIrqClient*)ctx;
  client->FastCallLoop();

  return 0;
}

void AudioIrqClient::Init(Guest* guest) {
  FXL_CHECK(loop_.StartThread() == ZX_OK);
  guest_ = guest;

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  FXL_CHECK(status == ZX_OK);
  cb_chan_ = std::move(local);

  thrd_t thread;
  std::stringstream tmp_name;
  tmp_name << "fastcall-audioirq";
  tmp_name << guest->vmid();

  std::string chan_name = tmp_name.str();

  int ret =
      thrd_create_with_name(&thread, fast_call_loop, this, chan_name.c_str());
  FXL_CHECK(ret == thrd_success);
  ret = thrd_detach(thread);
  FXL_CHECK(ret == thrd_success);

  svc_->RegisterAudioIrqListener(
      guest->vmid(), listener_.NewBinding(loop_.async()), std::move(remote));
  svc_->PopulateAudioIrq(guest->vmid());
}

void AudioIrqClient::UpdateAfeIrq(uint32_t irq_nr,
                                  uint32_t irq_status_offset,
                                  uint32_t irq_clear_offset,
                                  uint64_t interested_bitmask,
                                  uint32_t vmid,
                                  uint32_t index) {
  svc_->UpdateAfeIrq(irq_nr, irq_status_offset, irq_clear_offset,
                     interested_bitmask, vmid, index);
}

void AudioIrqClient::UpdateAfeIrqRegs(uint32_t vector) {
  svc_->UpdateAfeIrqRegs(vector);
}

void AudioIrqClient::OnNewNoBitmaskAudioIrq(uint32_t irq_nr) {
  std::unique_ptr<AudioIrq> audio_irq;
  AudioIrq::Create(irq_nr, guest_, &audio_irq);
  guest_->AddNewAudioIrq(std::move(audio_irq));
}

void AudioIrqClient::OnPendingIrqEvent(uint32_t irq_nr,
                                       uint64_t pending_bitmap) {
  guest_->AppendAudioIrqStatus(irq_nr, pending_bitmap);
}

}  // namespace machina
