#pragma once

#include <zircon/types.h>

#include <acrn/acrn_common.h>

#define VHM_IO_REQUEST                1      // uos io request
#define VHM_IO_REQUEST_ASYNC          2      // uos io async request
#define VHM_IO_REQUEST_ASYNC_RESPONSE 3      // uos io asyn response
#define VHM_INTR_INJECT               4      // uos interrupt inject

#define VCPU_ID_SIDEBAND_CHANNEL(vcpu) (ACRN_IO_REQUEST_MAX - 1 - (vcpu))
#define SIDEBAND_CHANNEL_MAX_VCPU (ACRN_IO_REQUEST_MAX - 1)
#define SIDEBAND_CHANNEL_MIN_VCPU (SIDEBAND_CHANNEL_MAX_VCPU - NUM_VHM_SIDEBAND_CHANNELS - 1)

struct vhm_io_request {
  uintptr_t addr;
  uint8_t vcpu_id;
};

struct vhm_io_async_response {
  uint8_t vcpu_id;
};

struct vhm_intr_inject {
  uint64_t dev_id;
  uint64_t evt_id;
};

struct vhm_chan_request {
  zx_txid_t txid;
  uint32_t cmd;
  union {
    struct vhm_io_request io_req;
    struct vhm_intr_inject intr_inject;
  } param;
};

struct vhm_chan_response {
  zx_txid_t txid;
  uint32_t cmd;
};

static inline bool is_sideband_chan(uint8_t vcpu_id) {
  return vcpu_id >= SIDEBAND_CHANNEL_MIN_VCPU &&
         vcpu_id <= SIDEBAND_CHANNEL_MAX_VCPU;
}