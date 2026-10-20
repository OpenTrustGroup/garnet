// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GPU_SCHEDULER_VCPU_SELECTION_H_
#define GARNET_LIB_MACHINA_GPU_SCHEDULER_VCPU_SELECTION_H_

#include <algorithm>
#include <cstdint>

#include <zircon/types.h>

namespace machina {

template <typename VcpuList>
bool IsGpuSchedVcpuSelected(const VcpuList& vcpus, uint64_t id) {
  return vcpus.size() != 0 &&
         std::find(vcpus.begin(), vcpus.end(), id) != vcpus.end();
}

template <typename VcpuList>
zx_status_t ValidateGpuSchedVcpus(const VcpuList& vcpus,
                                  uint32_t num_cpus) {
  for (auto vcpu = vcpus.begin(); vcpu != vcpus.end(); ++vcpu) {
    if (*vcpu >= num_cpus ||
        std::find(vcpus.begin(), vcpu, *vcpu) != vcpu) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  return ZX_OK;
}

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GPU_SCHEDULER_VCPU_SELECTION_H_
