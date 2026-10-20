// SPDX-License-Identifier: BSD-3-Clause

#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"
#include "smc_communication.h"

SmcCommunication* SmcCommunication::instance_ = nullptr;
std::mutex SmcCommunication::instance_mutex_;

SmcCommunication::SmcCommunication() {
  FXL_LOG(INFO) << "SmcCommunication created";
}

SmcCommunication::~SmcCommunication() {
  if (!initialized_) {
    return;
  }

  running_ = false;

  if (smc_communication_thread_.joinable()) {
    smc_communication_thread_.join();
  }

  initialized_ = false;
  FXL_LOG(INFO) << "SmcCommunication destroyed";
}

SmcCommunication* SmcCommunication::GetInstance() {
  if (instance_ == nullptr) {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (instance_ == nullptr) {
      instance_ = new SmcCommunication();
    }
  }
  return instance_;
}

zx_status_t SmcCommunication::Initialize() {
  if (initialized_) {
    FXL_LOG(INFO) << "SmcCommunication already initialized";
    return ZX_OK;
  }

  zx_status_t status = zx_smc_communication_init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to init smc_communication: " << status;
    return status;
  }

  running_ = true;
  smc_communication_thread_ = std::thread(&SmcCommunication::SmcCommunicationLoop, this);

  initialized_ = true;
  FXL_LOG(INFO) << "SmcCommunication initialized successfully";
  return ZX_OK;
}

zx_status_t SmcCommunication::TriggerSmcMessageRead() {
  FXL_LOG(INFO) << "Trigger smc message read";
  std::lock_guard<std::mutex> lock(message_mutex_);

  if (message_status_.load() != MESSAGE_EMPTY) {
    FXL_LOG(WARNING) << "Previous message not handled";
    return ZX_ERR_SHOULD_WAIT;
  }

  zx_status_t status = zx_smc_communication_message_read(&smc_message_, sizeof(smc_message_));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read smc message: " << status;
    return status;
  }
  message_status_.store(MESSAGE_READY);

  return ZX_OK;
}

zx_status_t SmcCommunication::ParseMessage(const uint8_t* message,
                                           size_t length,
                                           uint8_t& out_vmid,
                                           std::string& tgt_vm,
                                           std::string& cmd) {
  if (!message || length != SMC_DATA_SIZE) {
    FXL_LOG(ERROR) << "Invalid message or length";
    return ZX_ERR_INVALID_ARGS;
  }

  uint8_t src_vmid = message[SMC_VMID_OFFSET];
  if (src_vmid < YOCTO_VMID || src_vmid > TBOX_VMID) {
    FXL_LOG(ERROR) << "Invalid source vmid: " << static_cast<int>(src_vmid);
    return ZX_ERR_INVALID_ARGS;
  }
  out_vmid = src_vmid;

  uint8_t msg_len = message[SMC_COUNT_OFFSET];
  if (msg_len == 0 || msg_len > SMC_MSG_SIZE) {
    FXL_LOG(ERROR) << "Invalid message length: " << static_cast<int>(msg_len);
    return ZX_ERR_INVALID_ARGS;
  }

  std::string message_str;
  for (size_t i = 0; i < msg_len; i++) {     // Filter out control characters
    uint8_t ch = message[SMC_DATA_OFFSET + i];
    if (ch == 0) {
      break;
    }

    if (std::isprint(ch)) {
      message_str.push_back(static_cast<char>(ch));
    }
  }

  size_t comma_pos = message_str.find(',');
  if (comma_pos == std::string::npos) {
    FXL_LOG(ERROR) << "Invalid message format: no comma found";
    FXL_LOG(ERROR) << "Expected format: <vm>,<cmd>";
    return ZX_ERR_INVALID_ARGS;
  }

  tgt_vm = message_str.substr(0, comma_pos);
  cmd = message_str.substr(comma_pos + 1);

  if (tgt_vm.empty() || cmd.empty()) {
    FXL_LOG(ERROR) << "Invalid message: <vm> and <cmd> must be non-empty";
    return ZX_ERR_INVALID_ARGS;
  }

  return 0;
}

zx_status_t SmcCommunication::ParseVmid(const std::string& vm, uint8_t& vmid) {
  if (vm.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (vm == "yocto") {
    vmid = YOCTO_VMID;
    return ZX_OK;
  } else if (vm == "alps") {
    vmid = ALPS_VMID;
    return ZX_OK;
  } else if (vm == "tbox") {
    vmid = TBOX_VMID;
    return ZX_OK;
  }

  return ZX_ERR_INVALID_ARGS;
}

zx_status_t SmcCommunication::HandleCmd(const std::string& cmd,
                                        uint8_t src_vmid,
                                        uint8_t tgt_vmid) {
  zx_status_t status;

  if (cmd == "secure reboot") {
    status = zx_smc_communication_secure_reboot();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to secure reboot system, ret: " << status;
      return status;
    }
  } else if (cmd == "force reboot") {
    status = zx_smc_communication_force_reboot();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to force reboot system, ret: " << status;
      return status;
    }
  } else if (cmd == "vm poweroff") {
    status = zx_smc_communication_vm_poweroff(src_vmid, tgt_vmid);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to poweroff vm: " << tgt_vmid << ", ret: "<< status;
      return status;
    }
  } else {
    FXL_LOG(ERROR) << "Unknown cmd";
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t SmcCommunication::TriggerSmcMessageHandle() {
  FXL_LOG(INFO) << "Trigger smc message handle";
  std::lock_guard<std::mutex> lock(message_mutex_);

  if (message_status_.load() != MESSAGE_READY) {
    FXL_LOG(WARNING) << "No message ready to handle";
    return ZX_ERR_SHOULD_WAIT;
  }

  uint8_t src_vmid;
  std::string tgt_vm, cmd;
  zx_status_t status = ParseMessage(smc_message_, sizeof(smc_message_),
                                    src_vmid, tgt_vm, cmd);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to parse message";
    message_status_.store(MESSAGE_EMPTY);
    return status;
  }

  uint8_t tgt_vmid;
  status = ParseVmid(tgt_vm, tgt_vmid);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Invalid vm: " << tgt_vm;
    message_status_.store(MESSAGE_EMPTY);
    return status;
  }

  FXL_LOG(INFO) << "Processing message - source_vmid: " << static_cast<int>(src_vmid)
                << ", target_vm: " << tgt_vm
                << ", target_vmid: " << static_cast<int>(tgt_vmid)
                << ", cmd: " << cmd;
  status = HandleCmd(cmd, src_vmid, tgt_vmid);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to handle cmd";
    message_status_.store(MESSAGE_EMPTY);
    return status;
  }

  message_status_.store(MESSAGE_EMPTY);

  return ZX_OK;
}

void SmcCommunication::SmcCommunicationLoop(void) {
  zx_status_t status;

  FXL_LOG(INFO) << "SmcCommunicationLoop started";

  while (running_) {
      status = TriggerSmcMessageRead();
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to trigger smc message read: " << status;
        continue;
      }

      status = TriggerSmcMessageHandle();
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to trigger smc message handle: " << status;
        continue;
      }
  }

  FXL_LOG(INFO) << "SmcCommunicationLoop stopped";
}
