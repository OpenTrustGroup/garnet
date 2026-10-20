// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <mutex>
#include <threads.h>

#include <fuchsia/cpp/machina.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/zx/event.h>
#include <lib/zx/port.h>

#include "garnet/lib/machina/arch/arm64/gic_its.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/phys_mem.h"

#include "garnet/lib/machina/vhm_service_impl.h"

#define SIDE_CHAN_BUSY 1

struct acrn_io_request_buffer;
struct acrn_io_request;

namespace machina {

typedef struct cmvd_cmd {
  uint64_t cmd;
  uint64_t args[3];
} cmvd_cmd_t;

class VhmDevice : public IoHandler,
                  public InterruptListener,
                  public MappingListener {
 public:
  static constexpr zx_signals_t kSignalVmDump = ZX_USER_SIGNAL_3;
  VhmDevice(uint16_t vmid);
  virtual ~VhmDevice();

  fidl::InterfaceRequest<VhmService> NewRequest() {
    return vhm_client_.NewRequest();
  }

  zx_status_t Init(Guest* guest,
                   InterruptController* interrupt_controller,
                   GicIts* gic_its);
  void Shutdown();

  void FetchVmConfig();
  void RegisterMessageListener(fidl::InterfaceHandle<MessageListener> listener);

  const std::string& cfg() const { return config_; }

#ifdef _VHM_USE_CHANNEL_
  void HandleVhmChannelMsgLoop();
  const zx::channel& get_cli_chan() const { return vhm_cli_chan_; }
  int HandleExceptionDumpLoop();
#endif
  Guest* GetGuest() {
    return guest_;
  }

 protected:
  // override |IoHandler|
  virtual zx_status_t Read(uint64_t addr, IoValue* value) const override;
  virtual zx_status_t Write(uint64_t addr, const IoValue& value) override;

  // override |InterruptListener|
  virtual void OnInterrupt(uint32_t irq) override;

  // override |MappingListener|
  virtual void OnNewMapping(uint64_t base,
                            uint64_t size,
                            uint8_t trap_type) override;

  void InitInternal();
  zx_status_t ReadInternal(uint8_t vcpu_id,
                           uintptr_t addr,
                           IoValue* value) const;
  zx_status_t WriteInternal(uint8_t vcpu_id,
                            uintptr_t addr,
                            const IoValue& value);
#ifdef _VHM_USE_CHANNEL_
  zx_status_t WriteInternalAsync(uint8_t vcpu_id,
                                 uintptr_t addr,
                                 const IoValue& value);
#endif

  static size_t GetChannel();

 private:
  void make_io_request(acrn_io_request* req,
                       uint8_t direction,
                       uint8_t size,
                       uintptr_t addr) const;
#ifdef _VHM_USE_CHANNEL_
  zx_txid_t GetNextTxid() const;
  zx_status_t KickAndWaitIoRequest(uint8_t vcpu_id, uintptr_t addr) const;
  zx_status_t KickIoRequestAsync(uint8_t vcpu_id,
                                 uintptr_t addr,
                                 acrn_io_request* req,
                                 const IoValue& value) const;
  void NotifySidebandWaiters();
  void JoinThread(thrd_t thread,
                  bool* started,
                  const char* thread_name);
  zx_status_t QueueExceptionPortShutdown();
#endif

  machina::VhmServiceSyncPtr vhm_client_;

  zx::vmo ioreq_vmo_;
  acrn_io_request_buffer* ioreq_buf_ = nullptr;
  std::string config_;

  uint16_t vmid_ = 0;

  InterruptController* interrupt_controller_ = nullptr;
  GicIts* gic_its_ = nullptr;

  fidl::Binding<InterruptListener> binding_;
  Guest* guest_ = nullptr;
  async::Loop loop_;
  bool loop_started_ = false;
  std::atomic_bool shutdown_started_{false};

#ifdef _VHM_USE_CHANNEL_
  zx::channel vhm_cli_chan_;
  zx::channel vhm_srv_chan_;
  thrd_t vhm_thread_ = {};
  thrd_t edump_thread_ = {};
  bool vhm_thread_started_ = false;
  bool edump_thread_started_ = false;
  zx::event vhm_shutdown_event_;
  zx::port exception_port_;
  std::atomic_bool exception_port_bound_{false};
  std::unique_ptr<std::atomic<zx_txid_t>> next_txid_;
  std::unique_ptr<uint8_t[]> side_chan_state_;
  std::unique_ptr<std::mutex[]> side_chan_mutex_;
  std::unique_ptr<std::condition_variable[]> side_chan_cv_;
#endif
};

}  // namespace machina
