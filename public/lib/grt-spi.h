// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

// #define GRT_SPI_CONTROL_DEVICE "/dev/sys/platform/spi0/mtk_spi"
static const char* kSpiNodes[] = {
    "/dev/sys/platform/spi0/mtk_spi",
    "/dev/sys/platform/spi1/mtk_spi",
    "/dev/sys/platform/spi2/mtk_spi",
    "/dev/sys/platform/spi3/mtk_spi",
    "/dev/sys/platform/spi4/mtk_spi",
    "/dev/sys/platform/spi5/mtk_spi",
    "/dev/sys/platform/spi6/mtk_spi",
    "/dev/sys/platform/spi7/mtk_spi",
};
const int kSpiNodesCount = sizeof(kSpiNodes) / sizeof(kSpiNodes[0]);

typedef struct spi_transfer_head {
    uint32_t spi_clk_hz;
    uint32_t bus;
    uint32_t mode;
    uint32_t speed_hz;
    uint32_t word_delay_usecs;
    uint32_t len;
    uint16_t delay_usecs;
    uint8_t chip_select;
    uint8_t bits_per_word;
    uint8_t cs_change;
} grt_spi_transfer_head;

typedef struct spi_cs {
	bool enable;
	uint32_t bus;
} grt_spi_cs_t;

#define IOCTL_GRT_SPI_GET_EVENT \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_SPI, 0)

#define IOCTL_GRT_SPI_SET_CS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SPI, 1)

#define IOCTL_GRT_SPI_DMA_TRANSFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SPI, 2)

#define IOCTL_GRT_STOP_SPI_DMA_TRANSFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SPI, 3)

 typedef struct grt_spi_transfer {
    zx_handle_t tx_vmo;
    zx_handle_t rx_vmo;
    int size;
} grt_spi_transfer_t;

typedef struct grt_spi_dma_transfer {
    grt_spi_transfer_head trans_head;
    uint64_t tx;
    uint64_t rx;
    int size;
} grt_spi_dma_transfer_t;

IOCTL_WRAPPER_OUT(ioctl_grt_spi_get_event, IOCTL_GRT_SPI_GET_EVENT, zx_handle_t);
IOCTL_WRAPPER_IN(ioctl_grt_spi_set_cs, IOCTL_GRT_SPI_SET_CS, grt_spi_cs_t);
IOCTL_WRAPPER_IN(ioctl_grt_spi_dma_transfer, IOCTL_GRT_SPI_DMA_TRANSFER, grt_spi_dma_transfer_t);
IOCTL_WRAPPER(ioctl_grt_stop_spi_dma_transfer, IOCTL_GRT_STOP_SPI_DMA_TRANSFER);