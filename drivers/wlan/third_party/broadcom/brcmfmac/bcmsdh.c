/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/* ****************** SDIO CARD Interface Functions **************************/

// TODO(cphoenix): Do we need sdio, completion, status, stdatomic, threads?
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/sdio.h>
#include <ddk/metadata.h>
#include <wifi/wifi-config.h>
#include <ddk/protocol/gpio.h>
#include <lib/sync/completion.h>
#include <zircon/status.h>

#include <pthread.h>
#include <stdatomic.h>
#ifndef _ALL_SOURCE
#define _ALL_SOURCE // Enables thrd_create_with_name in <threads.h>.
#endif
#include <threads.h>

#include "brcm_hw_ids.h"
#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "chip.h"
#include "chipcommon.h"
#include "common.h"
#include "core.h"
#include "debug.h"
#include "defs.h"
#include "device.h"
#include "linuxisms.h"
#include "netbuf.h"
#include "sdio.h"
#include "soc.h"

#define SDIOH_API_ACCESS_RETRY_LIMIT 2

#define DMA_ALIGN_MASK 0x03

#define SDIO_FUNC1_BLOCKSIZE 64
#define SDIO_FUNC2_BLOCKSIZE 512
/* Maximum milliseconds to wait for F2 to come up */
#define SDIO_WAIT_F2RDY 3000

#define BRCMF_DEFAULT_RXGLOM_SIZE 32 /* max rx frames in glom chain */

struct brcmf_sdiod_freezer {
    atomic_int freezing;
    atomic_int thread_count;
    uint32_t frozen_count;
    sync_completion_t thread_freeze;
    sync_completion_t resumed;
};

static void brcmf_sdiod_ib_irqhandler(struct brcmf_sdio_dev* sdiodev) {
    brcmf_dbg(INTR, "IB intr triggered\n");

    brcmf_sdio_isr(sdiodev->bus);
}

/* dummy handler for SDIO function 2 interrupt */
static void brcmf_sdiod_dummy_irqhandler(struct brcmf_sdio_dev* sdiodev) {}

zx_status_t brcmf_sdiod_configure_oob_interrupt(struct brcmf_sdio_dev* sdiodev,
                                                wifi_config_t *config) {
    platform_device_protocol_t pdev;
    zx_status_t ret = ZX_OK;

    if ((ret = device_get_protocol(sdiodev->dev.zxdev, ZX_PROTOCOL_PLATFORM_DEV, &pdev))
                                                                                != ZX_OK) {
        zxlogf(ERROR, "brcmf_sdiod_intr_register: ZX_PROTOCOL_PLATFORM_DEV not available\n");
        //TODO: This driver could be run on X64 platforms. When X64 platforms have parallel apis,
        // we need to check for those apis instead of returning an error.
        return ZX_ERR_INTERNAL;
    }

    pdev_device_info_t info;
    ret = pdev_get_device_info(&pdev, &info);
    if (ret != ZX_OK) {
        zxlogf(ERROR, "brcmf_sdiod_intr_register: pdev_get_device_info failed: %d\n", ret);
        return ret;
    }

    if (info.gpio_count < 1) {
        zxlogf(ERROR, "brcmf_sdiod_intr_register: OOB gpio not available\n");
        return ZX_ERR_INTERNAL;
    }
    if ((ret = pdev_get_protocol(&pdev, ZX_PROTOCOL_GPIO, WIFI_OOB_IRQ_GPIO_INDEX,
                                 &sdiodev->gpios[WIFI_OOB_IRQ_GPIO_INDEX])) != ZX_OK) {
        zxlogf(ERROR, "brcmf_sdiod_intr_register: GPIO WIFI_OOB_IRQ_GPIO_INDEX not available\n");
        return ret;
    }
    // Debug GPIO is optional.
    pdev_get_protocol(&pdev, ZX_PROTOCOL_GPIO, DEBUG_GPIO_INDEX, &sdiodev->gpios[DEBUG_GPIO_INDEX]);

    ret = gpio_config_in(&sdiodev->gpios[WIFI_OOB_IRQ_GPIO_INDEX], GPIO_NO_PULL);
    if (ret != ZX_OK) {
        zxlogf(ERROR, "brcmf_sdiod_intr_register: gpio_config failed: %d\n", ret);
        return ret;
    }

    ret = gpio_get_interrupt(&sdiodev->gpios[WIFI_OOB_IRQ_GPIO_INDEX],
                             config->oob_irq_mode,
                             &sdiodev->irq_handle);
    if (ret != ZX_OK) {
        zxlogf(ERROR, "brcmf_sdiod_intr_register: gpio_get_interrupt failed: %d\n", ret);
        return ret;
    }
    return ZX_OK;
}

zx_status_t brcmf_sdiod_intr_register(struct brcmf_sdio_dev* sdiodev) {
    struct brcmfmac_sdio_pd* pdata;
    zx_status_t ret = ZX_OK;
    uint8_t data;
    uint32_t addr, gpiocontrol;

    pdata = &sdiodev->settings->bus.sdio;
    pdata->oob_irq_supported = false;
    wifi_config_t config;
    size_t actual;
    ret = device_get_metadata(sdiodev->dev.zxdev, DEVICE_METADATA_PRIVATE,
                              &config, sizeof(wifi_config_t), &actual);
    if ((ret != ZX_OK && ret != ZX_ERR_NOT_FOUND) ||
        (ret == ZX_OK && actual != sizeof(wifi_config_t))) {
        zxlogf(ERROR, "brcmf_sdiod_intr_register: device_get_metadata failed\n");
        return ret;
    }

    // If there is metadata, OOB is supported.
    if (ret == ZX_OK) {
        brcmf_dbg(SDIO, "Enter, register OOB IRQ\n");
        ret = brcmf_sdiod_configure_oob_interrupt(sdiodev, &config);
        if (ret != ZX_OK) {
            return ret;
        }
        // TODO(cphoenix): Add error handling for thrd_create_with_name and
        // thrd_detach. Note that the thrd_ functions don't return zx_status_t; check for
        // thrd_success and maybe thrd_nomem. See zircon/third_party/ulib/musl/include/threads.h
        pdata->oob_irq_supported = true;
        // TODO(NET-1495): Get interrupts working.
        brcmf_dbg(TEMP, "* * * NOT starting oob_irqhandler! Depending on watchdog.* * *");
        //thrd_create_with_name(&sdiodev->isr_thread, brcmf_sdiod_oob_irqhandler, sdiodev,
        //                      "brcmf-sdio-isr");
        //thrd_detach(sdiodev->isr_thread);
        sdiodev->oob_irq_requested = true;
        ret = enable_irq_wake(sdiodev->irq_handle);
        if (ret != ZX_OK) {
            brcmf_err("enable_irq_wake failed %d\n", ret);
            return ret;
        }
        sdiodev->irq_wake = true;

        sdio_claim_host(sdiodev->func1);

        if (sdiodev->bus_if->chip == BRCM_CC_43362_CHIP_ID) {
            /* assign GPIO to SDIO core */
            addr = CORE_CC_REG(SI_ENUM_BASE, gpiocontrol);
            gpiocontrol = brcmf_sdiod_func1_rl(sdiodev, addr, &ret);
            gpiocontrol |= 0x2;
            brcmf_sdiod_func1_wl(sdiodev, addr, gpiocontrol, &ret);

            brcmf_sdiod_func1_wb(sdiodev, SBSDIO_GPIO_SELECT, 0xf, &ret);
            brcmf_sdiod_func1_wb(sdiodev, SBSDIO_GPIO_OUT, 0, &ret);
            brcmf_sdiod_func1_wb(sdiodev, SBSDIO_GPIO_EN, 0x2, &ret);
        }

        /* must configure SDIO_CCCR_INT_ENABLE to enable irq */
        data = brcmf_sdiod_func0_rb(sdiodev, SDIO_CCCR_INT_ENABLE, &ret);
        data |= SDIO_CCCR_IEN_FUNC1 | SDIO_CCCR_IEN_FUNC2 | SDIO_CCCR_IEN_FUNC0;
        brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_INT_ENABLE, data, &ret);

        /* redirect, configure and enable io for interrupt signal */
        data = SDIO_CCCR_BRCM_SEPINT_MASK | SDIO_CCCR_BRCM_SEPINT_OE;
        if (config.oob_irq_mode == ZX_INTERRUPT_MODE_LEVEL_HIGH) {
            data |= SDIO_CCCR_BRCM_SEPINT_ACT_HI;
        }
        brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_BRCM_SEPINT, data, &ret);
        // TODO(cphoenix): This pause is probably unnecessary.
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
        sdio_release_host(sdiodev->func1);
    } else {
        brcmf_dbg(SDIO, "Entering\n");
        sdio_claim_host(sdiodev->func1);
        sdio_enable_fn_intr(&sdiodev->sdio_proto, SDIO_FN_1);
        (void)brcmf_sdiod_ib_irqhandler; // TODO(cphoenix): If we use these, plug them in later.
        sdio_enable_fn_intr(&sdiodev->sdio_proto, SDIO_FN_2);
        (void)brcmf_sdiod_dummy_irqhandler;
        sdio_release_host(sdiodev->func1);
        sdiodev->sd_irq_requested = true;
    }

    return ZX_OK;
}

void brcmf_sdiod_intr_unregister(struct brcmf_sdio_dev* sdiodev) {
    brcmf_dbg(SDIO, "Entering oob=%d sd=%d\n", sdiodev->oob_irq_requested,
              sdiodev->sd_irq_requested);

    if (sdiodev->oob_irq_requested) {
        struct brcmfmac_sdio_pd* pdata;

        pdata = &sdiodev->settings->bus.sdio;
        sdio_claim_host(sdiodev->func1);
        brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_BRCM_SEPINT, 0, NULL);
        brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_INT_ENABLE, 0, NULL);
        sdio_release_host(sdiodev->func1);

        sdiodev->oob_irq_requested = false;
        if (sdiodev->irq_wake) {
            disable_irq_wake(sdiodev->irq_handle);
            sdiodev->irq_wake = false;
        }
        zx_handle_close(sdiodev->irq_handle);
        sdiodev->oob_irq_requested = false;
    }

    if (sdiodev->sd_irq_requested) {
        sdio_claim_host(sdiodev->func1);
        sdio_disable_fn_intr(&sdiodev->sdio_proto, SDIO_FN_2);
        sdio_disable_fn_intr(&sdiodev->sdio_proto, SDIO_FN_1);
        sdio_release_host(sdiodev->func1);
        sdiodev->sd_irq_requested = false;
    }
}

void brcmf_sdiod_change_state(struct brcmf_sdio_dev* sdiodev, enum brcmf_sdiod_state state) {
    if (sdiodev->state == BRCMF_SDIOD_NOMEDIUM || state == sdiodev->state) {
        return;
    }

    brcmf_dbg(TRACE, "%d -> %d\n", sdiodev->state, state);
    switch (sdiodev->state) {
    case BRCMF_SDIOD_DATA:
        /* any other state means bus interface is down */
        brcmf_bus_change_state(sdiodev->bus_if, BRCMF_BUS_DOWN);
        break;
    case BRCMF_SDIOD_DOWN:
        /* transition from DOWN to DATA means bus interface is up */
        if (state == BRCMF_SDIOD_DATA) {
            brcmf_bus_change_state(sdiodev->bus_if, BRCMF_BUS_UP);
        }
        break;
    default:
        break;
    }
    sdiodev->state = state;
}

static zx_status_t brcmf_sdiod_transfer(struct brcmf_sdio_dev* sdiodev, uint8_t func,
                                           uint32_t addr, bool write, void* data, size_t size,
                                           bool fifo) {
    sdio_rw_txn_t txn;
    zx_status_t result;

    txn.addr = addr;
    txn.write = write;
    txn.virt = data;
    txn.data_size = size;
    txn.incr = !fifo;
    txn.fifo = fifo;
    txn.use_dma = false; // TODO(cphoenix): Decide when to use DMA
    txn.buf_offset = 0;

    result = sdio_do_rw_txn(&sdiodev->sdio_proto, func, &txn);
    if (result != ZX_OK) {
        brcmf_dbg(TEMP, "Why did this fail?? result %d %s", result, zx_status_get_string(result));
    }
    return result;
}

static uint8_t brcmf_sdiod_func_rb(struct brcmf_sdio_dev* sdiodev, uint8_t func, uint32_t addr,
                             zx_status_t* result_out) {
    uint8_t data;
    zx_status_t result;
    result = brcmf_sdiod_transfer(sdiodev, func, addr, false, &data, sizeof(data), false);
    if (result_out != NULL) {
        *result_out = result;
    }
    return data;
}

uint8_t brcmf_sdiod_func0_rb(struct brcmf_sdio_dev* sdiodev, uint32_t addr,
                             zx_status_t* result_out) {
    return brcmf_sdiod_func_rb(sdiodev, SDIO_FN_0, addr, result_out);
}

uint8_t brcmf_sdiod_func1_rb(struct brcmf_sdio_dev* sdiodev, uint32_t addr,
                             zx_status_t* result_out) {
    return brcmf_sdiod_func_rb(sdiodev, SDIO_FN_1, addr, result_out);
}

void brcmf_sdiod_func0_wb(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint8_t data,
                             zx_status_t* result_out) {
    zx_status_t result;
    result = brcmf_sdiod_transfer(sdiodev, SDIO_FN_0, addr, true, &data, sizeof(data), false);
    if (result_out != NULL) {
        *result_out = result;
    }
}

void brcmf_sdiod_func1_wb(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint8_t data,
                             zx_status_t* result_out) {
    zx_status_t result;
    result = brcmf_sdiod_transfer(sdiodev, SDIO_FN_1, addr, true, &data, sizeof(data), false);
    if (result_out != NULL) {
        *result_out = result;
    }
}

static zx_status_t brcmf_sdiod_set_backplane_window(struct brcmf_sdio_dev* sdiodev, uint32_t addr) {
    uint32_t v;
    uint32_t bar0 = addr & SBSDIO_SBWINDOW_MASK;
    zx_status_t err = ZX_OK;
    int i;

    if (bar0 == sdiodev->sbwad) {
        return ZX_OK;
    }

    v = bar0 >> 8;

    for (i = 0; i < 3 && err == ZX_OK; i++, v >>= 8) {
        brcmf_sdiod_func1_wb(sdiodev, SBSDIO_FUNC1_SBADDRLOW + i, v & 0xff, &err);
    }

    if (err == ZX_OK) {
        sdiodev->sbwad = bar0;
    }

    return err;
}

uint32_t brcmf_sdiod_func1_rl(struct brcmf_sdio_dev* sdiodev, uint32_t addr, zx_status_t* ret) {
    uint32_t data = 0;
    zx_status_t retval;

    retval = brcmf_sdiod_set_backplane_window(sdiodev, addr);
    if (retval == ZX_OK) {
        addr &= SBSDIO_SB_OFT_ADDR_MASK;
        addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;
        retval = brcmf_sdiod_transfer(sdiodev, SDIO_FN_1, addr, false, &data, sizeof(data), false);
    }
    if (ret) {
        *ret = retval;
    }

    return data;
}

void brcmf_sdiod_func1_wl(struct brcmf_sdio_dev* sdiodev, uint32_t addr, uint32_t data,
                          zx_status_t* ret) {
    zx_status_t retval;

    retval = brcmf_sdiod_set_backplane_window(sdiodev, addr);
    if (retval == ZX_OK) {
        addr &= SBSDIO_SB_OFT_ADDR_MASK;
        addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;
        retval = brcmf_sdiod_transfer(sdiodev, SDIO_FN_1, addr, true, &data, sizeof(data), false);
    }
    if (ret) {
        *ret = retval;
    }
}

// TODO(ZX-2598): This is a workaround for a low-level SDIO bug.
#define F2_XFER_SIZE 512

zx_status_t brcmf_sdiod_read(struct brcmf_sdio_dev* sdiodev, uint8_t func, uint32_t addr,
                             void* data, size_t size) {
    zx_status_t err = ZX_OK;
    while (func == 2 && size > F2_XFER_SIZE) {
        err = brcmf_sdiod_transfer(sdiodev, func, addr, false, data, F2_XFER_SIZE, false);
        if (err != ZX_OK) {
            break;
        }
        data += F2_XFER_SIZE;
        size -= F2_XFER_SIZE;
        addr += F2_XFER_SIZE;
    }
    if (err == ZX_OK) {
        err = brcmf_sdiod_transfer(sdiodev, func, addr, false, data, size, false);
    }
    return err;
}

zx_status_t brcmf_sdiod_write(struct brcmf_sdio_dev* sdiodev, uint8_t func, uint32_t addr,
                              void* data, size_t size) {
    zx_status_t err = ZX_OK;
    while (func == 2 && size > F2_XFER_SIZE) {
        err = brcmf_sdiod_transfer(sdiodev, func, addr, true, data, F2_XFER_SIZE, false);
        if (err != ZX_OK) {
            break;
        }
        data += F2_XFER_SIZE;
        size -= F2_XFER_SIZE;
        addr += F2_XFER_SIZE;
    }
    if (err == ZX_OK) {
        err = brcmf_sdiod_transfer(sdiodev, func, addr, true, data, size, false);
    }
    return err;
}

zx_status_t brcmf_sdiod_read_fifo(struct brcmf_sdio_dev* sdiodev, uint8_t func, uint32_t addr,
                                  void* data, size_t size) {
    zx_status_t err = ZX_OK;
    while (func == 2 && size > F2_XFER_SIZE) {
        err = brcmf_sdiod_transfer(sdiodev, func, addr, false, data, F2_XFER_SIZE, true);
        if (err != ZX_OK) {
            break;
        }
        data += F2_XFER_SIZE;
        size -= F2_XFER_SIZE;
    }
    if (err == ZX_OK) {
        err = brcmf_sdiod_transfer(sdiodev, func, addr, false, data, size, true);
    }
    return err;
}

static zx_status_t brcmf_sdiod_netbuf_read(struct brcmf_sdio_dev* sdiodev, uint32_t func,
                                           uint32_t addr, struct brcmf_netbuf* netbuf) {
    unsigned int req_sz;
    zx_status_t err;

    /* Single netbuf use the standard mmc interface */
    req_sz = netbuf->len + 3;
    req_sz &= (uint)~3;

    switch (func) {
    case SDIO_FN_1:
        err = brcmf_sdiod_read(sdiodev, func, addr, netbuf->data, req_sz);
        break;
    case SDIO_FN_2:
        err = brcmf_sdiod_read_fifo(sdiodev, func, addr, netbuf->data, req_sz);
        break;
    default:
        /* bail out as things are really fishy here */
        WARN(1, "invalid sdio function number %d\n");
        err = ZX_ERR_IO_REFUSED;
    };

    if (err == ZX_ERR_IO_REFUSED) {
        brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
    }

    return err;
}

static zx_status_t brcmf_sdiod_netbuf_write(struct brcmf_sdio_dev* sdiodev, uint32_t func,
                                            uint32_t addr, struct brcmf_netbuf* netbuf) {
    unsigned int req_sz;
    zx_status_t err;

    /* Single netbuf use the standard mmc interface */
    req_sz = netbuf->len + 3;
    req_sz &= (uint)~3;

    err = brcmf_sdiod_write(sdiodev, func, addr, netbuf->data, req_sz);

    if (err == ZX_ERR_IO_REFUSED) {
        brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
    }

    return err;
}

zx_status_t brcmf_sdiod_recv_buf(struct brcmf_sdio_dev* sdiodev, uint8_t* buf, uint nbytes) {
    struct brcmf_netbuf* mypkt;
    zx_status_t err;

    mypkt = brcmu_pkt_buf_get_netbuf(nbytes);
    if (!mypkt) {
        brcmf_err("brcmu_pkt_buf_get_netbuf failed: len %d\n", nbytes);
        return ZX_ERR_NO_MEMORY;
    }

    err = brcmf_sdiod_recv_pkt(sdiodev, mypkt);
    if (err == ZX_OK) {
        memcpy(buf, mypkt->data, nbytes);
    }

    brcmu_pkt_buf_free_netbuf(mypkt);
    return err;
}

zx_status_t brcmf_sdiod_recv_pkt(struct brcmf_sdio_dev* sdiodev, struct brcmf_netbuf* pkt) {
    uint32_t addr = sdiodev->cc_core->base;
    zx_status_t err = ZX_OK;

    err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
    if (err != ZX_OK) {
        goto done;
    }

    addr &= SBSDIO_SB_OFT_ADDR_MASK;
    addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

    err = brcmf_sdiod_netbuf_read(sdiodev, SDIO_FN_2, addr, pkt);

done:
    return err;
}

zx_status_t brcmf_sdiod_recv_chain(struct brcmf_sdio_dev* sdiodev, struct brcmf_netbuf_list* pktq,
                                   uint totlen) {
    struct brcmf_netbuf* glom_netbuf = NULL;
    struct brcmf_netbuf* netbuf;
    uint32_t addr = sdiodev->cc_core->base;
    zx_status_t err = ZX_OK;

    brcmf_dbg(SDIO, "addr = 0x%x, size = %d\n", addr, brcmf_netbuf_list_length(pktq));

    err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
    if (err != ZX_OK) {
        goto done;
    }

    addr &= SBSDIO_SB_OFT_ADDR_MASK;
    addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

    if (brcmf_netbuf_list_length(pktq) == 1) {
        err = brcmf_sdiod_netbuf_read(sdiodev, SDIO_FN_2, addr, brcmf_netbuf_list_peek_head(pktq));
    } else {
        glom_netbuf = brcmu_pkt_buf_get_netbuf(totlen);
        if (!glom_netbuf) {
            return ZX_ERR_NO_MEMORY;
        }
        err = brcmf_sdiod_netbuf_read(sdiodev, SDIO_FN_2, addr, glom_netbuf);
        if (err != ZX_OK) {
            goto done;
        }

        brcmf_netbuf_list_for_every(pktq, netbuf) {
            memcpy(netbuf->data, glom_netbuf->data, netbuf->len);
            brcmf_netbuf_shrink_head(glom_netbuf, netbuf->len);
        }
    }

done:
    brcmu_pkt_buf_free_netbuf(glom_netbuf);
    return err;
}

zx_status_t brcmf_sdiod_send_buf(struct brcmf_sdio_dev* sdiodev, uint8_t* buf, uint nbytes) {
    struct brcmf_netbuf* mypkt;
    uint32_t addr = sdiodev->cc_core->base;
    zx_status_t err;

    mypkt = brcmu_pkt_buf_get_netbuf(nbytes);

    if (!mypkt) {
        brcmf_err("brcmu_pkt_buf_get_netbuf failed: len %d\n", nbytes);
        return ZX_ERR_IO;
    }

    memcpy(mypkt->data, buf, nbytes);

    err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
    if (err != ZX_OK) {
        return err;
    }

    addr &= SBSDIO_SB_OFT_ADDR_MASK;
    addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

    if (err == ZX_OK) {
        err = brcmf_sdiod_netbuf_write(sdiodev, SDIO_FN_2, addr, mypkt);
    }

    brcmu_pkt_buf_free_netbuf(mypkt);

    return err;
}

zx_status_t brcmf_sdiod_send_pkt(struct brcmf_sdio_dev* sdiodev, struct brcmf_netbuf_list* pktq) {
    struct brcmf_netbuf* netbuf;
    uint32_t addr = sdiodev->cc_core->base;
    zx_status_t err;

    //brcmf_dbg(SDIO, "addr = 0x%x, size = %d\n", addr, brcmf_netbuf_list_length(pktq));

    err = brcmf_sdiod_set_backplane_window(sdiodev, addr);
    if (err != ZX_OK) {
        return err;
    }

    addr &= SBSDIO_SB_OFT_ADDR_MASK;
    addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

    brcmf_netbuf_list_for_every(pktq, netbuf) {
        err = brcmf_sdiod_netbuf_write(sdiodev, SDIO_FN_2, addr, netbuf);
        if (err != ZX_OK) {
            break;
        }
    }

    return err;
}

zx_status_t brcmf_sdiod_ramrw(struct brcmf_sdio_dev* sdiodev, bool write, uint32_t address,
                              uint8_t* data, uint size) {
    zx_status_t err = ZX_OK;
    struct brcmf_netbuf* pkt;
    uint32_t this_transfer_address;
    uint this_transfer_size;

#define MAX_XFER_SIZE 0x100 // TODO(cphoenix): Remove when SDIO bug (?) is fixed.

    uint packet_size = min_t(uint, MAX_XFER_SIZE, size);
    pkt = brcmf_netbuf_allocate(packet_size);
    if (!pkt) {
        brcmf_err("brcmf_netbuf_allocate failed: len %d\n", packet_size);
        return ZX_ERR_IO;
    }
    pkt->priority = 0;

    /* Determine initial transfer parameters */
    this_transfer_address = address & SBSDIO_SB_OFT_ADDR_MASK;
    uint32_t low_address_bits = this_transfer_address & (MAX_XFER_SIZE - 1);
    if (low_address_bits) {
        this_transfer_size = min(packet_size, MAX_XFER_SIZE - low_address_bits);
    } else {
        this_transfer_size = packet_size;
    }
    sdio_claim_host(sdiodev->func1);

    /* Do the transfer(s) */
    while (size) {

        /* Set the backplane window to include the start address */
        err = brcmf_sdiod_set_backplane_window(sdiodev, address);

        if (err != ZX_OK) {
            break;
        }

        this_transfer_address &= SBSDIO_SB_OFT_ADDR_MASK;
        this_transfer_address |= SBSDIO_SB_ACCESS_2_4B_FLAG;

        brcmf_netbuf_grow_tail(pkt, this_transfer_size);

        if (write) {
            memcpy(pkt->data, data, this_transfer_size);
            err = brcmf_sdiod_netbuf_write(sdiodev, SDIO_FN_1, this_transfer_address, pkt);
        } else {
            err = brcmf_sdiod_netbuf_read(sdiodev, SDIO_FN_1, this_transfer_address, pkt);
        }

        if (err != ZX_OK) {
            brcmf_err("membytes transfer failed\n");
            break;
        }
        if (!write) {
            memcpy(data, pkt->data, this_transfer_size);
        }
        brcmf_netbuf_reduce_length_to(pkt, 0);

        /* Adjust for next transfer (if any) */
        size -= this_transfer_size;
        if (size) {
            data += this_transfer_size;
            address += this_transfer_size;
            this_transfer_address += this_transfer_size;
            this_transfer_size = min_t(uint32_t, MAX_XFER_SIZE, size);
        }
    }

    brcmf_netbuf_free(pkt);

    sdio_release_host(sdiodev->func1);

    return err;
}

zx_status_t brcmf_sdiod_abort(struct brcmf_sdio_dev* sdiodev, uint32_t func) {
    brcmf_dbg(SDIO, "Enter\n");

    /* Issue abort cmd52 command through F0 */
    brcmf_sdiod_func0_wb(sdiodev, SDIO_CCCR_ABORT_RESET, func, NULL);

    brcmf_dbg(SDIO, "Exit\n");
    return ZX_OK;
}

#ifdef CONFIG_PM_SLEEP
static zx_status_t brcmf_sdiod_freezer_attach(struct brcmf_sdio_dev* sdiodev) {
    sdiodev->freezer = calloc(1, sizeof(*sdiodev->freezer));
    if (!sdiodev->freezer) {
        return ZX_ERR_NO_MEMORY;
    }
    atomic_store(&sdiodev->freezer->thread_count, 0);
    atomic_store(&sdiodev->freezer->freezing, 0);
    sdiodev->freezer->thread_freeze = SYNC_COMPLETION_INIT;
    sdiodev->freezer->resumed = SYNC_COMPLETION_INIT;
    return ZX_OK;
}

static void brcmf_sdiod_freezer_detach(struct brcmf_sdio_dev* sdiodev) {
    if (sdiodev->freezer) {
        WARN_ON(atomic_load(&sdiodev->freezer->freezing));
        free(sdiodev->freezer);
    }
}

static zx_status_t brcmf_sdiod_freezer_on(struct brcmf_sdio_dev* sdiodev) {
    zx_status_t res = ZX_OK;

    sdiodev->freezer->frozen_count = 0;
    sync_completion_reset(&sdiodev->freezer->resumed);
    sync_completion_reset(&sdiodev->freezer->thread_freeze);
    atomic_store(&sdiodev->freezer->freezing, 1);
    brcmf_sdio_trigger_dpc(sdiodev->bus);
    sync_completion_wait(&sdiodev->freezer->thread_freeze, ZX_TIME_INFINITE);
    sdio_claim_host(sdiodev->func1);
    res = brcmf_sdio_sleep(sdiodev->bus, true);
    sdio_release_host(sdiodev->func1);
    return res;
}

static void brcmf_sdiod_freezer_off(struct brcmf_sdio_dev* sdiodev) {
    sdio_claim_host(sdiodev->func1);
    brcmf_sdio_sleep(sdiodev->bus, false);
    sdio_release_host(sdiodev->func1);
    atomic_store(&sdiodev->freezer->freezing, 0);
    sync_completion_signal(&sdiodev->freezer->resumed);
}

bool brcmf_sdiod_freezing(struct brcmf_sdio_dev* sdiodev) {
    return atomic_load(&sdiodev->freezer->freezing);
}

void brcmf_sdiod_try_freeze(struct brcmf_sdio_dev* sdiodev) {
    if (!brcmf_sdiod_freezing(sdiodev)) {
        return;
    }
    sdiodev->freezer->frozen_count++;
    if (atomic_load(&sdiodev->freezer->thread_count) == sdiodev->freezer->frozen_count) {
        sync_completion_signal(&sdiodev->freezer->thread_freeze);
    }
    sync_completion_wait(&sdiodev->freezer->resumed, ZX_TIME_INFINITE);
}

void brcmf_sdiod_freezer_count(struct brcmf_sdio_dev* sdiodev) {
    atomic_fetch_add(&sdiodev->freezer->thread_count, 1);
}

void brcmf_sdiod_freezer_uncount(struct brcmf_sdio_dev* sdiodev) {
    atomic_fetch_sub(&sdiodev->freezer->thread_count, 1);
}
#else
static zx_status_t brcmf_sdiod_freezer_attach(struct brcmf_sdio_dev* sdiodev) {
    return ZX_OK;
}

static void brcmf_sdiod_freezer_detach(struct brcmf_sdio_dev* sdiodev) {}
#endif /* CONFIG_PM_SLEEP */

static zx_status_t brcmf_sdiod_remove(struct brcmf_sdio_dev* sdiodev) {
    sdiodev->state = BRCMF_SDIOD_DOWN;
    if (sdiodev->bus) {
        brcmf_sdio_remove(sdiodev->bus);
        sdiodev->bus = NULL;
    }

    brcmf_sdiod_freezer_detach(sdiodev);

    /* Disable Function 2 */
    sdio_claim_host(sdiodev->func2);
    sdio_disable_fn(&sdiodev->sdio_proto, SDIO_FN_2);
    sdio_release_host(sdiodev->func2);

    /* Disable Function 1 */
    sdio_claim_host(sdiodev->func1);
    sdio_disable_fn(&sdiodev->sdio_proto, SDIO_FN_1);
    sdio_release_host(sdiodev->func1);

    sdiodev->sbwad = 0;

    // TODO(cphoenix): Power management stuff
    //pm_runtime_allow(sdiodev->func1->card->host->parent);
    return ZX_OK;
}

// TODO(cphoenix): Power management stuff
#ifdef POWER_MANAGEMENT
static void brcmf_sdiod_host_fixup(struct mmc_host* host) {
    /* runtime-pm powers off the device */
    pm_runtime_forbid(host->parent);
    /* avoid removal detection upon resume */
    host->caps |= MMC_CAP_NONREMOVABLE; // Defined outside this driver's codebase
}
#endif // POWER_MANAGEMENT

static zx_status_t brcmf_sdiod_probe(struct brcmf_sdio_dev* sdiodev) {
    zx_status_t ret = ZX_OK;

    ret = sdio_update_block_size(&sdiodev->sdio_proto, SDIO_FN_1, SDIO_FUNC1_BLOCKSIZE, false);
    if (ret != ZX_OK) {
        brcmf_err("Failed to set F1 blocksize\n");
        goto out;
    }
    ret = sdio_update_block_size(&sdiodev->sdio_proto, SDIO_FN_2, SDIO_FUNC2_BLOCKSIZE, false);
    if (ret != ZX_OK) {
        brcmf_err("Failed to set F2 blocksize\n");
        goto out;
    }

    /* increase F2 timeout */
    // TODO(cphoenix): SDIO doesn't use timeout yet
    //sdiodev->func2->enable_timeout = SDIO_WAIT_F2RDY;

    /* Enable Function 1 */
    ret = sdio_enable_fn(&sdiodev->sdio_proto, SDIO_FN_1);
    if (ret != ZX_OK) {
        brcmf_err("Failed to enable F1: err=%d\n", ret);
        goto out;
    }

    ret = brcmf_sdiod_freezer_attach(sdiodev);
    if (ret != ZX_OK) {
        goto out;
    }

    /* try to attach to the target device */
    sdiodev->bus = brcmf_sdio_probe(sdiodev);
    if (!sdiodev->bus) {
        ret = ZX_ERR_IO_NOT_PRESENT;
        goto out;
    }
    //brcmf_sdiod_host_fixup(sdiodev->func2->card->host);
out:
    if (ret != ZX_OK) {
        brcmf_sdiod_remove(sdiodev);
    }

    return ret;
}

#ifdef TODO_ADD_SDIO_IDS // Put some of these into binding.c
#define BRCMF_SDIO_DEVICE(dev_id) \
    { SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, dev_id) }

/* devices we support, null terminated */
static const struct sdio_device_id brcmf_sdmmc_ids[] = {
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43143),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43241),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4329),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4330),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4334),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43340),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43341),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43362),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4335_4339),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4339),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43430),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4345),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_43455),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4354),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4356),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_CYPRESS_4373),
    BRCMF_SDIO_DEVICE(SDIO_DEVICE_ID_BROADCOM_4359),
    {/* end: all zeroes */}
};
#endif // TODO_ADD_SDIO_IDS

static void brcmf_sdiod_acpi_set_power_manageable(struct brcmf_device* dev, int val) {
#if IS_ENABLED(CONFIG_ACPI)
    struct acpi_device* adev;

    adev = ACPI_COMPANION(dev);
    if (adev) {
        adev->flags.power_manageable = 0;
    }
#endif
}

zx_status_t brcmf_sdio_register(zx_device_t* zxdev, sdio_protocol_t* sdio_proto) {
    zx_status_t err;
    struct brcmf_device* dev;

    struct brcmf_bus* bus_if = NULL;
    struct sdio_func* func1 = NULL;
    struct sdio_func* func2 = NULL;
    struct brcmf_sdio_dev* sdiodev = NULL;

    brcmf_dbg(SDIO, "Enter\n");
    sdio_hw_info_t devinfo;
    sdio_get_dev_hw_info(sdio_proto, &devinfo);
    if (devinfo.dev_hw_info.num_funcs < 3) {
        brcmf_err("Not enough SDIO funcs (need 3, have %d)", devinfo.dev_hw_info.num_funcs);
        return ZX_ERR_IO;
    }

    pthread_mutexattr_t mutex_attr;
    if (pthread_mutexattr_init(&mutex_attr)) {
        return ZX_ERR_INTERNAL;
    }
    if (pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE)) {
        err = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    // We don't get "class" info in the current API.
    //brcmf_dbg(SDIO, "Class=%x\n", func->class);
    brcmf_dbg(SDIO, "sdio vendor ID: 0x%04x\n", devinfo.funcs_hw_info[SDIO_FN_1].manufacturer_id);
    brcmf_dbg(SDIO, "sdio device ID: 0x%04x\n", devinfo.funcs_hw_info[SDIO_FN_1].product_id);
    // Linux calls sdio_register once per SDIO function; we only get called once per chipset.
    //brcmf_dbg(SDIO, "Function#: %d\n", func);

    // TODO(cphoenix): Reexamine this when SDIO is more mature - do we need to support "quirks" in
    // Fuchsia? (MMC_QUIRK_LENIENT_FN0 is defined outside this driver.)
    /* Set MMC_QUIRK_LENIENT_FN0 for this card */
    //func->card->quirks |= MMC_QUIRK_LENIENT_FN0;

    /* prohibit ACPI power management for this device */
    // TODO(cphoenix): Linux power management stuff
    brcmf_sdiod_acpi_set_power_manageable(NULL, 0);

    bus_if = calloc(1, sizeof(struct brcmf_bus));
    if (!bus_if) {
        err = ZX_ERR_NO_MEMORY;
        goto fail;
    }
    func1 = calloc(1, sizeof(struct sdio_func));
    if (!func1) {
        err = ZX_ERR_NO_MEMORY;
        goto fail;
    } else {
        if (pthread_mutex_init(&func1->lock, &mutex_attr)) {
            free(func1);
            func1 = NULL;
            err = ZX_ERR_INTERNAL;
            goto fail;
        }
    }
    func2 = calloc(1, sizeof(struct sdio_func));
    if (!func2) {
        err = ZX_ERR_NO_MEMORY;
        goto fail;
    } else {
        if (pthread_mutex_init(&func2->lock, &mutex_attr)) {
            free(func2);
            func2 = NULL;
            err = ZX_ERR_INTERNAL;
            goto fail;
        }
    }
    sdiodev = calloc(1, sizeof(struct brcmf_sdio_dev));
    if (!sdiodev) {
        err = ZX_ERR_NO_MEMORY;
        goto fail;
    }
    dev = &sdiodev->dev;
    dev->zxdev = zxdev;
    memcpy(&sdiodev->sdio_proto, sdio_proto, sizeof(sdiodev->sdio_proto));

    sdiodev->bus_if = bus_if;
    sdiodev->func1 = func1;
    sdiodev->func2 = func2;
    bus_if->bus_priv.sdio = sdiodev;
    bus_if->proto_type = BRCMF_PROTO_BCDC;
    dev->bus = bus_if;

    sdiodev->manufacturer_id = devinfo.funcs_hw_info[SDIO_FN_1].manufacturer_id;
    sdiodev->product_id = devinfo.funcs_hw_info[SDIO_FN_1].product_id;

    brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_DOWN);

    brcmf_dbg(SDIO, "F2 found, calling brcmf_sdiod_probe...\n");
    err = brcmf_sdiod_probe(sdiodev);
    if (err != ZX_OK) {
        brcmf_err("F2 error, probe failed %d...\n", err);
        goto fail;
    }

    pthread_mutexattr_destroy(&mutex_attr);
    brcmf_dbg(SDIO, "F2 init completed...\n");
    return ZX_OK;

fail:
    free(sdiodev);
    if (func2) {
        pthread_mutex_destroy(&func2->lock);
        free(func2);
    }
    if (func1) {
        pthread_mutex_destroy(&func1->lock);
        free(func1);
    }
    free(bus_if);
    pthread_mutexattr_destroy(&mutex_attr);
    return err;
}

static void brcmf_ops_sdio_remove(struct brcmf_sdio_dev* sdiodev) {
    struct brcmf_bus* bus_if;

    brcmf_dbg(SDIO, "Enter\n");
    if (sdiodev == NULL) {
        return;
    }
    brcmf_dbg(SDIO, "sdio vendor ID: 0x%04x\n", sdiodev->manufacturer_id);
    brcmf_dbg(SDIO, "sdio device ID: 0x%04x\n", sdiodev->product_id);

    bus_if = dev_to_bus(&sdiodev->dev);
    if (bus_if) {
        /* start by unregistering irqs */
        brcmf_sdiod_intr_unregister(sdiodev);

        brcmf_sdiod_remove(sdiodev);

        free(bus_if);
        if (sdiodev->func1) {
            pthread_mutex_destroy(&sdiodev->func1->lock);
            free(sdiodev->func1);
        }
        if (sdiodev->func2) {
            pthread_mutex_destroy(&sdiodev->func2->lock);
            free(sdiodev->func2);
        }
        free(sdiodev);
    }

    brcmf_dbg(SDIO, "Exit\n");
}

void brcmf_sdio_wowl_config(struct brcmf_device* dev, bool enabled) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;

    brcmf_dbg(SDIO, "Configuring WOWL, enabled=%d\n", enabled);
    sdiodev->wowl_enabled = enabled;
}

#ifdef CONFIG_PM_SLEEP
static zx_status_t brcmf_ops_sdio_suspend(struct brcmf_sdio_dev* sdiodev, uint32_t func) {
    struct brcmf_bus* bus_if;
    mmc_pm_flag_t sdio_flags;
    struct brcmf_device* dev = sdiodev->dev;

    brcmf_dbg(SDIO, "Enter: F%d\n", func);
    if (func != SDIO_FN_1) {
        return ZX_OK;
    }

    bus_if = dev_to_bus(dev);
    sdiodev = bus_if->bus_priv.sdio;

    brcmf_sdiod_freezer_on(sdiodev);
    brcmf_sdio_wd_timer(sdiodev->bus, false);

    sdio_flags = MMC_PM_KEEP_POWER;
    if (sdiodev->wowl_enabled) {
        if (sdiodev->settings->bus.sdio.oob_irq_supported) {
            enable_irq_wake(sdiodev->irq_handle);
        } else {
            sdio_flags |= MMC_PM_WAKE_SDIO_IRQ;
        }
    }
    if (sdio_set_host_pm_flags(sdiodev, SDIO_FN_1, sdio_flags)) {
        brcmf_err("Failed to set pm_flags %x\n", sdio_flags);
    }
    return ZX_OK;
}

static zx_status_t brcmf_ops_sdio_resume(struct brcmf_device* dev) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_sdio_dev* sdiodev = bus_if->bus_priv.sdio;

    brcmf_dbg(SDIO, "Enter");

    brcmf_sdiod_freezer_off(sdiodev);
    return ZX_OK;
}

static const struct dev_pm_ops brcmf_sdio_pm_ops = {
    .suspend = brcmf_ops_sdio_suspend,
    .resume = brcmf_ops_sdio_resume,
};
#endif /* CONFIG_PM_SLEEP */

void brcmf_sdio_exit(void) {
    brcmf_dbg(SDIO, "Enter\n");

    // TODO(cphoenix): Hook up the actual remove pathway.
    brcmf_ops_sdio_remove(NULL);
}
