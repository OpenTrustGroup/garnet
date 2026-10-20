/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <stdint.h>

#include <zircon/device/ioctl.h>

#define GRT_VRPMB_IOCTL_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NEBULA, 0x21)
#define GRT_VRPMB_IOCTL_GET_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_NEBULA, 0x22)
#define GRT_VRPMB_IOCTL_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NEBULA, 0x23)

#define UFS_VIRTIO_RPMB_SHM_HANDLE_ID 0x00
#define UFS_VIRTIO_RPMB_EVENT_HANDLE_ID 0x10

#define UFS_VIRTIO_RPMB_MAX_GUESTS 4
#define UFS_VIRTIO_RPMB_QUEUE_DEPTH 1
#define UFS_VIRTIO_RPMB_MAX_FRAMES 256
#define UFS_VIRTIO_RPMB_PACKET_MAGIC 0x5652504d

#define RPMB_REGION_0 0x00
#define RPMB_REGION_1 0x01
#define RPMB_REGION_2 0x02
#define RPMB_REGION_3 0x03

typedef struct ufs_virtio_rpmb_start_param {
    uint16_t vmid;
} ufs_virtio_rpmb_start_param_t;

typedef struct ufs_virtio_rpmb_get_handle_param {
    uint16_t vmid;
    uint8_t handle_id;
} ufs_virtio_rpmb_get_handle_param_t;

typedef struct ufs_virtio_rpmb_frame {
    uint8_t stuff[196];
    uint8_t key_mac[32];
    uint8_t data[256];
    uint8_t nonce[16];
    uint32_t wr_cnt;
    uint16_t addr;
    uint16_t blk_cnt;
    uint16_t result;
    uint16_t req_resp;
} __attribute__((packed)) ufs_virtio_rpmb_frame_t;

typedef struct ufs_virtio_rpmb_vq_req {
    uint32_t slot_id;
    uint32_t region;
    uint32_t req_nfrm;
    uint32_t rsp_nfrm;
    uint32_t req_bytes;
    uint32_t rsp_bytes;
} __attribute__((packed)) ufs_virtio_rpmb_vq_req_t;

typedef struct ufs_virtio_rpmb_packet {
    uint32_t magic;
    int32_t status;
    int32_t rpmb_ret;
    int32_t rpmb_result;
    uint32_t region;
    uint32_t req_nfrm;
    uint32_t rsp_nfrm;
    uint32_t req_bytes;
    uint32_t rsp_bytes;
    ufs_virtio_rpmb_frame_t req_frame[UFS_VIRTIO_RPMB_MAX_FRAMES];
    ufs_virtio_rpmb_frame_t rsp_frame[UFS_VIRTIO_RPMB_MAX_FRAMES];
} ufs_virtio_rpmb_packet_t;
