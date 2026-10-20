// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>
#include <threads.h>
#define IOCTL_FAMILY_I2C  0x14   //define in ddk/zircon/system/public/zircon/device/ioctl.h

#ifdef __cplusplus
extern "C" {
#endif

extern const char* kI2cNodes[];
extern const int kI2cNodesCount;

#ifdef __cplusplus
}
#endif

#define IOCTL_GRT_I2C_CREATE_TRANSFER_HANDLES \
    IOCTL(IOCTL_KIND_SET_TWO_HANDLES, IOCTL_FAMILY_I2C, 0)

#define IOCTL_GRT_I2C_CLOSE_TRANSFER_HANDLES \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 1)

#define IOCTL_GRT_I2C_TRIGGER_TRANSFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 2)

#define IOCTL_GRT_I2C_GET_EVENT \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_I2C, 3)

#define IOCTL_GRT_STOP_I2C_DMA_TRANSFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 4)

/*
 * Batch transfer: a single ioctl carries N messages of the same I2C
 * transaction (same client, same speed, same bus). The shared input/output
 * buffer (created via IOCTL_GRT_I2C_CREATE_TRANSFER_HANDLES) is laid out as
 *
 *   input_buf  : msg[0].buf | msg[1].buf | ... | msg[N-1].buf  (write data
 *                                                              packed; read
 *                                                              slots are 0)
 *   output_buf : msg[0].buf | msg[1].buf | ... | msg[N-1].buf  (read data
 *                                                              filled by HW;
 *                                                              write slots
 *                                                              are 0)
 *
 * The msg_desc array immediately follows the batch_head in the ioctl in_buf.
 * The total wire size of in_buf is:
 *     sizeof(grt_i2c_batch_head) + msg_num * sizeof(grt_i2c_msg_desc)
 */

#define IOCTL_GRT_I2C_BATCH_TRANSFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 5)

/*
 * Wire-format limits (must match all 3 sides: FE / VMM / BE).
 * Picked conservatively; raise carefully if real workloads need more.
 */
#define GRT_I2C_BATCH_MAX_MSGS      16u
#define GRT_I2C_BATCH_MAX_TOTAL_LEN 4096u

/* Per-msg flags (kept stable with linux i2c_msg layout). */
#define GRT_I2C_MSG_FLAG_RD         0x0001u

	//host to i2c driver struct
typedef struct grt_i2c_transfer {
	zx_handle_t input_vmo;
    zx_handle_t output_vmo;
    int size;
} grt_i2c_transfer_t;

typedef struct i2c_transfer_head {
	uint16_t addr;
	uint16_t bus;
    uint32_t len;
	uint32_t flags;
    uint32_t speed;
    uint32_t msg_num;
    uint32_t seq_id;
} grt_i2c_transfer_head;

/* New batch protocol structures. */
typedef struct grt_i2c_msg_desc {
    uint16_t addr;       /* 7-bit slave address */
    uint16_t flags;      /* GRT_I2C_MSG_FLAG_* */
    uint32_t len;        /* length of this msg's buf */
    uint32_t buf_off;    /* offset within input/output_buf */
    uint32_t reserved;   /* keep 16-byte alignment, MBZ */
} grt_i2c_msg_desc_t;

typedef struct grt_i2c_batch_head {
    uint16_t bus;
    uint16_t msg_num;        /* number of valid msg_desc entries (<= GRT_I2C_BATCH_MAX_MSGS) */
    uint32_t total_len;      /* sum of all msg lens (<= GRT_I2C_BATCH_MAX_TOTAL_LEN) */
    uint32_t speed;          /* speed in hz */
    uint32_t seq_id;         /* opaque identifier for logging */
} grt_i2c_batch_head_t;

/* in_hdr returned from the BE for a batch transfer. */
typedef struct grt_i2c_batch_in_hdr {
    uint8_t  status;          /* VIRTIO_I2C_MSG_OK / _ERR */
    uint8_t  reserved[3];
    uint32_t completed_msgs;  /* number of msgs that finished successfully */
} grt_i2c_batch_in_hdr_t;

IOCTL_WRAPPER_IN(ioctl_grt_i2c_create_transfer_handles, IOCTL_GRT_I2C_CREATE_TRANSFER_HANDLES, grt_i2c_transfer_t);
IOCTL_WRAPPER(ioctl_grt_i2c_close_transfer_handles, IOCTL_GRT_I2C_CLOSE_TRANSFER_HANDLES);
IOCTL_WRAPPER_IN(ioctl_grt_i2c_trigger_transfer, IOCTL_GRT_I2C_TRIGGER_TRANSFER, grt_i2c_transfer_head);
IOCTL_WRAPPER_OUT(ioctl_grt_i2c_get_event, IOCTL_GRT_I2C_GET_EVENT, zx_handle_t);
IOCTL_WRAPPER(ioctl_grt_stop_i2c_dma_transfer, IOCTL_GRT_STOP_I2C_DMA_TRANSFER);
/*
 * Batch ioctl wrapper.
 *
 * The in_buf is variable-length (head + N msg_desc), the out_buf is the
 * fixed-size grt_i2c_batch_in_hdr_t. We therefore use the
 * VARIN_OUT-style wrapper. Caller computes in_len as
 *     sizeof(grt_i2c_batch_head_t) + msg_num * sizeof(grt_i2c_msg_desc_t)
 *
 * The first parameter (intype) is grt_i2c_batch_head_t purely so the
 * helper can express "pointer to the head" — fdio_ioctl does the actual
 * variable-length copy based on in_len.
 */
IOCTL_WRAPPER_VARIN_OUT(ioctl_grt_i2c_batch_transfer,
                        IOCTL_GRT_I2C_BATCH_TRANSFER,
                        grt_i2c_batch_head_t,
                        grt_i2c_batch_in_hdr_t);