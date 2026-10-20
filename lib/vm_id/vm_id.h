// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <stdint.h>
#include <zircon/vmid.h>

#define GRT_SOS_VMID ZX_USER_VMID_SOS
#define GRT_ALPS_VMID ZX_USER_VMID_ALPS
#define GRT_TBOX_VMID ZX_USER_VMID_TBOX

#define GRT_BLOCK_BACKEND_VMID_OFFSET 1
#define GRT_BLOCK_SOS_BACKEND_VMID 0
#define GRT_BLOCK_ALPS_BACKEND_VMID 1
#define GRT_BLOCK_TBOX_BACKEND_VMID 2
#define GRT_BLOCK_MAX_BACKEND_VMIDS 3

#define GRT_VMID_INVALID INT32_MIN

static inline int32_t grt_guest_vmid_to_block_backend_vmid(int32_t vmid) {
  return vmid + GRT_BLOCK_BACKEND_VMID_OFFSET;
}

static inline int grt_is_block_backend_vmid(int32_t vmid) {
  return vmid >= GRT_BLOCK_SOS_BACKEND_VMID &&
         vmid < GRT_BLOCK_MAX_BACKEND_VMIDS;
}

static inline int grt_is_sos_vmid(int32_t vmid) {
  return vmid == GRT_SOS_VMID;
}

static inline int grt_is_alps_vmid(int32_t vmid) {
  return vmid == GRT_ALPS_VMID;
}

static inline int grt_is_tbox_vmid(int32_t vmid) {
  return vmid == GRT_TBOX_VMID;
}

#ifdef __cplusplus
namespace machina {

constexpr int32_t kSosVmid = GRT_SOS_VMID;
constexpr int32_t kAlpsVmid = GRT_ALPS_VMID;
constexpr int32_t kTboxVmid = GRT_TBOX_VMID;

constexpr int32_t kBlockSosBackendVmid = GRT_BLOCK_SOS_BACKEND_VMID;
constexpr int32_t kBlockAlpsBackendVmid = GRT_BLOCK_ALPS_BACKEND_VMID;
constexpr int32_t kBlockTboxBackendVmid = GRT_BLOCK_TBOX_BACKEND_VMID;
constexpr int32_t kBlockMaxBackendVmids = GRT_BLOCK_MAX_BACKEND_VMIDS;
constexpr int32_t kInvalidVmid = GRT_VMID_INVALID;

constexpr bool IsSosVmid(int32_t vmid) {
  return vmid == kSosVmid;
}

constexpr bool IsAlpsVmid(int32_t vmid) {
  return vmid == kAlpsVmid;
}

constexpr bool IsTboxVmid(int32_t vmid) {
  return vmid == kTboxVmid;
}

constexpr int32_t GuestVmidToBlockBackendVmid(int32_t vmid) {
  return vmid + GRT_BLOCK_BACKEND_VMID_OFFSET;
}

constexpr bool IsBlockBackendVmid(int32_t vmid) {
  return vmid >= kBlockSosBackendVmid && vmid < kBlockMaxBackendVmids;
}

constexpr const char* VmidToString(int32_t vmid) {
  return IsSosVmid(vmid) ? "SOS"
       : IsAlpsVmid(vmid) ? "ALPS"
       : IsTboxVmid(vmid) ? "TBOX"
                           : "UNKNOWN";
}

}  // namespace machina
#endif
