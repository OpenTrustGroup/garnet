/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _HIF_H_
#define _HIF_H_

#include "core.h"
#include "debug.h"

struct ath10k_hif_sg_item {
    uint16_t transfer_id;
    void* transfer_context; /* NULL = tx completion callback not called */
    void* vaddr; /* for debugging mostly */
    uint32_t paddr;
    uint16_t len;
};

struct ath10k_hif_ops {
    /* send a scatter-gather list to the target */
    zx_status_t (*tx_sg)(struct ath10k* ar, uint8_t pipe_id,
                         struct ath10k_hif_sg_item* items, int n_items);

    /* read firmware memory through the diagnose interface */
    zx_status_t (*diag_read)(struct ath10k* ar, uint32_t address, void* buf,
                             size_t buf_len);

    zx_status_t (*diag_write)(struct ath10k* ar, uint32_t address, const void* data,
                              int nbytes);

    /* Retrieve a bus transaction initiator handle */
    zx_status_t (*get_bti_handle)(struct ath10k* ar, zx_handle_t* bti_handle);

    /*
     * API to handle HIF-specific BMI message exchanges, this API is
     * synchronous and only allowed to be called from a context that
     * can block (sleep)
     */
    zx_status_t (*exchange_bmi_msg)(struct ath10k* ar,
                                    void* request, uint32_t request_len,
                                    void* response, uint32_t* response_len);

    /* Post BMI phase, after FW is loaded. Starts regular operation */
    zx_status_t (*start)(struct ath10k* ar);

    /* Clean up what start() did. This does not revert to BMI phase. If
     * desired so, call power_down() and power_up()
     */
    void (*stop)(struct ath10k* ar);

    zx_status_t (*map_service_to_pipe)(struct ath10k* ar, uint16_t service_id,
                                       uint8_t* ul_pipe, uint8_t* dl_pipe);

    void (*get_default_pipe)(struct ath10k* ar, uint8_t* ul_pipe, uint8_t* dl_pipe);

    /*
     * Check if prior sends have completed.
     *
     * Check whether the pipe in question has any completed
     * sends that have not yet been processed.
     * This function is only relevant for HIF pipes that are configured
     * to be polled rather than interrupt-driven.
     */
    void (*send_complete_check)(struct ath10k* ar, uint8_t pipe_id, int force);

    uint16_t (*get_free_queue_number)(struct ath10k* ar, uint8_t pipe_id);

    uint32_t (*read32)(struct ath10k* ar, uint32_t address);

    void (*write32)(struct ath10k* ar, uint32_t address, uint32_t value);

    /* Power up the device and enter BMI transfer mode for FW download */
    zx_status_t (*power_up)(struct ath10k* ar);

    /* Power down the device and free up resources. stop() must be called
     * before this if start() was called earlier
     */
    void (*power_down)(struct ath10k* ar);

    zx_status_t (*suspend)(struct ath10k* ar);
    zx_status_t (*resume)(struct ath10k* ar);

    /* fetch calibration data from target eeprom */
    zx_status_t (*fetch_cal_eeprom)(struct ath10k* ar, void** data,
                                    size_t* data_len);
};

static inline zx_status_t ath10k_hif_tx_sg(struct ath10k* ar, uint8_t pipe_id,
                                           struct ath10k_hif_sg_item* items,
                                           int n_items) {
    return ar->hif.ops->tx_sg(ar, pipe_id, items, n_items);
}

static inline zx_status_t ath10k_hif_diag_read(struct ath10k* ar, uint32_t address, void* buf,
                                               size_t buf_len) {
    return ar->hif.ops->diag_read(ar, address, buf, buf_len);
}

static inline zx_status_t ath10k_hif_diag_write(struct ath10k* ar, uint32_t address,
                                                const void* data, int nbytes) {
    if (!ar->hif.ops->diag_write) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->hif.ops->diag_write(ar, address, data, nbytes);
}

static inline zx_status_t ath10k_hif_get_bti_handle(struct ath10k* ar, zx_handle_t* bti_handle) {
    if (!ar->hif.ops->get_bti_handle) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->hif.ops->get_bti_handle(ar, bti_handle);
}

static inline zx_status_t ath10k_hif_exchange_bmi_msg(struct ath10k* ar,
                                                      void* request, uint32_t request_len,
                                                      void* response, uint32_t* response_len) {
    return ar->hif.ops->exchange_bmi_msg(ar, request, request_len,
                                         response, response_len);
}

static inline zx_status_t ath10k_hif_start(struct ath10k* ar) {
    return ar->hif.ops->start(ar);
}

static inline void ath10k_hif_stop(struct ath10k* ar) {
    return ar->hif.ops->stop(ar);
}

static inline zx_status_t ath10k_hif_map_service_to_pipe(struct ath10k* ar,
                                                         uint16_t service_id,
                                                         uint8_t* ul_pipe, uint8_t* dl_pipe) {
    return ar->hif.ops->map_service_to_pipe(ar, service_id,
                                            ul_pipe, dl_pipe);
}

static inline void ath10k_hif_get_default_pipe(struct ath10k* ar,
        uint8_t* ul_pipe, uint8_t* dl_pipe) {
    ar->hif.ops->get_default_pipe(ar, ul_pipe, dl_pipe);
}

static inline void ath10k_hif_send_complete_check(struct ath10k* ar,
        uint8_t pipe_id, int force) {
    ar->hif.ops->send_complete_check(ar, pipe_id, force);
}

static inline uint16_t ath10k_hif_get_free_queue_number(struct ath10k* ar,
        uint8_t pipe_id) {
    return ar->hif.ops->get_free_queue_number(ar, pipe_id);
}

static inline zx_status_t ath10k_hif_power_up(struct ath10k* ar) {
    return ar->hif.ops->power_up(ar);
}

static inline void ath10k_hif_power_down(struct ath10k* ar) {
    ar->hif.ops->power_down(ar);
}

static inline zx_status_t ath10k_hif_suspend(struct ath10k* ar) {
    if (!ar->hif.ops->suspend) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->hif.ops->suspend(ar);
}

static inline zx_status_t ath10k_hif_resume(struct ath10k* ar) {
    if (!ar->hif.ops->resume) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->hif.ops->resume(ar);
}

static inline uint32_t ath10k_hif_read32(struct ath10k* ar, uint32_t address) {
    if (!ar->hif.ops->read32) {
        ath10k_warn("hif read32 not supported\n");
        return 0xdeaddead;
    }

    return ar->hif.ops->read32(ar, address);
}

static inline void ath10k_hif_write32(struct ath10k* ar,
                                      uint32_t address, uint32_t data) {
    if (!ar->hif.ops->write32) {
        ath10k_warn("hif write32 not supported\n");
        return;
    }

    ar->hif.ops->write32(ar, address, data);
}

static inline zx_status_t ath10k_hif_fetch_cal_eeprom(struct ath10k* ar,
                                                      void** data,
                                                      size_t* data_len) {
    if (!ar->hif.ops->fetch_cal_eeprom) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->hif.ops->fetch_cal_eeprom(ar, data, data_len);
}

#endif /* _HIF_H_ */
