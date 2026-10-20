// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <thread>
#include <mutex>
#include <atomic>

#include <zircon/types.h>
#include <zircon/vmid.h>

class SmcCommunication {
 public:
  static SmcCommunication* GetInstance();
  zx_status_t Initialize();

  ~SmcCommunication();

  bool IsInitialized() const { return initialized_; }
  bool IsRunning() const { return running_; }

 private:
  SmcCommunication();

  void SmcCommunicationLoop();
  zx_status_t TriggerSmcMessageRead();
  zx_status_t TriggerSmcMessageHandle();
  zx_status_t ParseMessage(const uint8_t* message,
                           size_t length,
                           uint8_t& out_vmid,
                           std::string& tgt_vm,
                           std::string& cmd);
  zx_status_t ParseVmid(const std::string& vm, uint8_t& vmid);
  zx_status_t HandleCmd(const std::string& cmd,
                        uint8_t src_vmid,
                        uint8_t tgt_vmid);

  static constexpr uint8_t YOCTO_VMID = ZX_GUEST_VMID_SOS;
  static constexpr uint8_t ALPS_VMID = ZX_GUEST_VMID_ALPS;
  static constexpr uint8_t TBOX_VMID = ZX_GUEST_VMID_TBOX;

  static constexpr size_t SMC_DATA_SIZE = 32;
  static constexpr size_t SMC_MSG_SIZE = 30;
  static constexpr size_t SMC_VMID_OFFSET = 0;
  static constexpr size_t SMC_COUNT_OFFSET = 1;
  static constexpr size_t SMC_DATA_OFFSET = 2;

  static constexpr uint8_t MESSAGE_EMPTY = 0x00;
  static constexpr uint8_t MESSAGE_READY = 0x01;

  uint8_t smc_message_[SMC_DATA_SIZE]{};
  std::atomic<uint8_t> message_status_{MESSAGE_EMPTY};
  std::mutex message_mutex_;

  std::thread smc_communication_thread_;
  std::atomic<bool> initialized_{false};
  std::atomic<bool> running_{false};

  static SmcCommunication* instance_;
  static std::mutex instance_mutex_;
};
