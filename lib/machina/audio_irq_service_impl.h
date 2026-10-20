// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <stdint.h>
#include <sys/types.h>
#include <memory>
#include <thread>

#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/guest_config.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"

namespace machina {

class AudioIrqServiceImpl : public AudioIrqService {
 public:
  AudioIrqServiceImpl(component::ApplicationContext* application_context,
                      const std::vector<AudioIrqSpecEx>& audio_irq_specs,
                      Guest* guest);

  // |AudioIrqService|
  virtual void RegisterAudioIrqListener(
      uint32_t vmid,
      fidl::InterfaceHandle<AudioIrqListener> listener,
      zx::channel chan,
      RegisterAudioIrqListenerCallback callback) override;
  virtual void PopulateAudioIrq(uint32_t vmid,
                                PopulateAudioIrqCallback callback) override;
  virtual void UpdateAfeIrq(uint32_t irq_nr,
                            uint32_t irq_status_offset,
                            uint32_t irq_clear_offset,
                            uint64_t interested_bitmask,
                            uint32_t vmid,
                            uint32_t index,
                            UpdateAfeIrqCallback callback) override;
  virtual void UpdateAfeIrqRegs(uint32_t vector,
                                UpdateAfeIrqRegsCallback callback) override;
  zx_txid_t GetNextTxid(uint32_t vmid);
  void ForwardIrqEvent(uint16_t vector,
                       uint64_t pending_bitmask,
                       uint32_t vmid);

  const std::vector<AudioIrqSpecEx>& audio_irq_specs() {
    return audio_irq_specs_;
  }

 private:
  fidl::BindingSet<AudioIrqService> bindings_;
  std::unordered_map<uint32_t, AudioIrqListenerSyncPtr> listener_map_;
  std::unordered_map<uint32_t, std::atomic<zx_txid_t>> next_txid_map_;
  std::unordered_map<uint32_t, zx::channel> cb_chan_map_;
  const std::vector<AudioIrqSpecEx>& audio_irq_specs_;
  Guest* guest_;  // for host
};

}  // namespace machina
