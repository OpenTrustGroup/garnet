// SPDX-License-Identifier: BSD-3-Clause

#ifndef GARNET_LIB_MACHINA_PV_BLOCK_H_
#define GARNET_LIB_MACHINA_PV_BLOCK_H_

#include <atomic>
#include <threads.h>

#include <fbl/mutex.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/event.h>
#include <lib/zx/time.h>
#include <zircon/device/pvblk.h>

#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/phys_mem.h"

namespace machina {

constexpr uint64_t kPvBlockMmioBase = 0xa10000000UL;
constexpr uint32_t kPvBlockIrqBase = 307U;
constexpr uint32_t kPvBlockDeviceCount = 3U;
constexpr uint32_t kPvBlockQueueCount = 1U;
constexpr uint32_t kPvBlockQueueDepth = 256U;

class PvBlockDevice : public IoHandler {
 public:
  static zx_status_t Create(const char* path, Guest* guest,
                            InterruptController* interrupt_controller,
                            uint64_t mmio_base, uint32_t irq,
                            fbl::unique_ptr<PvBlockDevice>* out);
  // Takes ownership of |fd| after validating the remaining arguments.
  static zx_status_t CreateFromFd(int fd, Guest* guest,
                                  InterruptController* interrupt_controller,
                                  uint64_t mmio_base, uint32_t irq,
                                  fbl::unique_ptr<PvBlockDevice>* out);

  ~PvBlockDevice() override;

  zx_status_t Init();
  zx_status_t Stop();

  uint64_t mmio_base() const { return mmio_base_; }
  uint32_t irq() const { return irq_; }
  const struct pvblk_config& config() const { return config_; }

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

 private:
  PvBlockDevice(fbl::unique_fd fd, Guest* guest,
                InterruptController* interrupt_controller, uint16_t backend_vmid,
                uint64_t mmio_base, uint32_t irq);

  zx_status_t StartBackend();
  zx_status_t AttachEvent();
  zx_status_t AttachRingLocked();
  zx_status_t DoorbellLocked(uint16_t qid);
  zx_status_t ControlIoctlLocked();
  zx_status_t ReadReg(uint64_t addr, uint32_t* out) const;
  zx_status_t WriteReg(uint64_t addr, uint32_t value);
  zx_status_t GetConfig();
  const struct pvblk_queue_ctrl* QueueCtrlLocked() const;
  uint32_t SqBacklogLocked() const;
  bool HasPendingCompletions() const;
  bool WaitForGuestCompletionDrain() const;
  void NotifyGuest();

  static int IrqThreadEntry(void* arg);
  int IrqThread();

  mutable fbl::Mutex mutex_;
  fbl::unique_fd fd_;
  Guest* guest_ = nullptr;
  InterruptController* interrupt_controller_ = nullptr;
  uint16_t backend_vmid_ = 0;
  uint64_t mmio_base_ = 0;
  uint32_t irq_ = 0;
  struct pvblk_config config_ = {};
  struct pvblk_ring_param ring_param_ = {};
  struct pvblk_doorbell_param doorbell_param_ = {};
  uint32_t control_request_ = 0;
  uint64_t control_arg_gpa_ = 0;
  uint32_t control_arg_len_ = 0;
  uint32_t control_result_ = 0;
  zx_status_t control_status_ = 0;
  zx::event completion_event_;
  std::atomic<bool> stopping_{false};
  std::atomic<uint32_t> doorbell_log_count_{0};
  std::atomic<uint32_t> notify_log_count_{0};
  std::atomic<uint32_t> skipped_irq_log_count_{0};
  std::atomic<uint64_t> doorbell_count_{0};
  std::atomic<uint64_t> doorbell_empty_count_{0};
  std::atomic<uint64_t> doorbell_backlog_total_{0};
  std::atomic<uint64_t> notify_count_{0};
  std::atomic<uint64_t> skipped_irq_count_{0};
  thrd_t irq_thread_ = 0;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_PV_BLOCK_H_
