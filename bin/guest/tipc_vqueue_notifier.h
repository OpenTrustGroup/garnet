// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <zircon/types.h>

class TipcVqueueNotifier {
 public:
  TipcVqueueNotifier(uint32_t irq, int32_t vmid) {
    zx_status_t status = zx_vqueue_notifier_create(irq, vmid, &notifier_);
    FXL_CHECK(status == ZX_OK) << "Failed to create vqueue notifier";
  }

  ~TipcVqueueNotifier() { zx_handle_close(notifier_); }

  void BindVcpu(uint64_t id, machina::Vcpu* vcpu) {
    zx_status_t status =
        zx_vqueue_notifier_bind_vcpu(notifier_, id, vcpu->object());
    FXL_CHECK(status == ZX_OK) << "Failed to bind vcpu with vqueue notifier";
  }

  zx_handle_t getZxHandle(void) {
    return notifier_;
  }

 private:
  zx_handle_t notifier_;
};
