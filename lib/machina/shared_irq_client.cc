// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared_irq_client.h"
#include <trusty_std.h>
#include <uapi/err.h>

#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/shared_irq.h"

namespace machina {

// static
std::unique_ptr<SharedIrqClient> SharedIrqClient::instance_;
// static
fbl::Mutex SharedIrqClient::mutex_;

SharedIrqClient::SharedIrqClient() : listener_(this) {}

void SharedIrqClient::Init(Guest* guest) {
  FXL_CHECK(loop_.StartThread() == ZX_OK);

  svc_->RegisterSharedIrqListener(listener_.NewBinding(loop_.async()));
  svc_->PopulateSharedIrq();

  guest_ = guest;
}

void SharedIrqClient::UpdatePreference(uint32_t irq_nr,
                                       uint32_t interested_bitmask) {
  svc_->UpdatePreference(irq_nr, interested_bitmask);
}

void SharedIrqClient::OnNewSharedIrq(uint32_t irq_nr,
                                     uint32_t interested_bitmask) {
  std::unique_ptr<SharedIrq> shared_irq;
  SharedIrq::Create(irq_nr, interested_bitmask, guest_, &shared_irq);
  guest_->AddNewSharedIrq(std::move(shared_irq));
}

void SharedIrqClient::OnPendingIrqEvent(uint32_t irq_nr,
                                        uint32_t pending_bitmap) {
  guest_->AppendSharedIrqStatus(irq_nr, pending_bitmap);
}

}  // namespace machina
