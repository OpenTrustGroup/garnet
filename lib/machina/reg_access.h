// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>

#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/guest.h"

namespace machina {

class RegAccess : public IoHandler {
public:
  explicit RegAccess(Guest *guest) : guest_(guest) {}

  ~RegAccess() override;

  zx_status_t Init(const RegAccessSpec &spec);

  zx_status_t Read(uint64_t addr, IoValue *value) const override;
  zx_status_t Write(uint64_t addr, const IoValue &value) override;

private:
  Guest *guest_;
  uint64_t start_paddr_ = 0;
  uint64_t size_ = 0;
  zx::vmo vmo_;
  uint64_t offset_in_vmo_ = 0;
  uintptr_t vaddr_ = 0;
};

zx_status_t RegisterRegAccessHandlers(
    machina::Guest &guest, const std::vector<machina::RegAccessSpec> &specs,
    std::vector<std::unique_ptr<machina::RegAccess>> &handlers);

} // namespace machina
