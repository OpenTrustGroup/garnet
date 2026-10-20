// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend_impl.h"

#include <string.h>

#include <lib/fxl/logging.h>

constexpr uint32_t kReqGpioPin = 147;
constexpr uint32_t kMapFlags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;

zx_status_t BackendImpl::Init() {
  constexpr int max_retries = 500;
  constexpr std::chrono::milliseconds retry_interval(10);
  int retry_count = 0;
  uint32_t enable = 1;

  while ((!gpio_fd_.ok() || !spi_fd_.ok()) && retry_count < max_retries) {
    if (!gpio_fd_.ok()) {
      gpio_fd_.reset(open(GRT_GPIO_CONTROL_DEVICE, O_RDWR));
      if (!gpio_fd_.ok()) {
        LOG(WARNING) << "Failed to open GPIO device (retry "
                     << retry_count + 1 << "/" << max_retries << ")";
      }
    }

    if (!spi_fd_.ok()) {
      spi_fd_.reset(open(GRT_SPI_CONTROL_DEVICE, O_RDWR));
      if (!spi_fd_.ok()) {
        LOG(WARNING) << "Failed to open SPI device (retry "
                     << retry_count + 1 << "/" << max_retries << ")";
      }
    }

    retry_count++;

    if ((!gpio_fd_.ok() || !spi_fd_.ok())) {
      std::this_thread::sleep_for(retry_interval);
    }
  }
  if (!gpio_fd_.ok() || !spi_fd_.ok()) {
    LOG(ERROR) << "Failed to open GPIO or SPI device";
    return ZX_ERR_IO;
  }

  ioctl_grt_gpio_get_event(gpio_fd_, wait_handle_.reset_and_get_address());
  if (!wait_handle_.is_valid()) {
    LOG(ERROR) << "Failed to get GPIO event handle";
    return ZX_ERR_IO;
  }

  /* enable gpio irq */
  ioctl_grt_gpio_enable_irq(gpio_fd_, &enable);

  gpio_wait_thread_ = std::make_unique<std::thread>([this]() {
    while (true) {
      zx_status_t status = wait_handle_.wait_one(ZX_USER_SIGNAL_0,
                                                 zx::time::infinite(), nullptr);
      if (status != ZX_OK) {
        LOG(ERROR) << "wait rx request failed: " << status;
        continue;
      }

      if (should_exit_) {
        break;
      }

      if (rx_request_listener_) {
        rx_request_listener_();
      }
      wait_handle_.signal(ZX_USER_SIGNAL_0, 0);
    }
  });

  zx::vmo rx_vmo;
  auto status = zx::vmo::create(kSpiMaxBufferSize, 0, &rx_vmo);
  if (status != ZX_OK) {
    LOG(ERROR) << "Failed to create TX VMO: " << status;
    return status;
  }

  zx::vmo tx_vmo;
  status = zx::vmo::create(kSpiMaxBufferSize, 0, &tx_vmo);
  if (status != ZX_OK) {
    LOG(ERROR) << "Failed to create RX VMO: " << status;
    return status;
  }

  status = zx::vmar::root_self().map(/*vmar_offset=*/0, tx_vmo,
                                     /*vmo_offset=*/0, kSpiMaxBufferSize,
                                     kMapFlags, (uintptr_t*)&spi_tx_buf_);
  if (status != ZX_OK) {
    LOG(ERROR) << "Failed to map TX VMO: " << status;
    return status;
  }

  status = zx::vmar::root_self().map(/*vmar_offset=*/0, rx_vmo,
                                     /*vmo_offset=*/0, kSpiMaxBufferSize,
                                     kMapFlags, (uintptr_t*)&spi_rx_buf_);
  if (status != ZX_OK) {
    LOG(ERROR) << "Failed to map RX VMO: " << status;
    return status;
  }

  spi_trans_.rx_vmo = rx_vmo.release();
  spi_trans_.tx_vmo = tx_vmo.release();
  spi_trans_.size = kSpiMaxBufferSize;
  ioctl_grt_spi_create_transfer_handles(spi_fd_, &spi_trans_);

  return ZX_OK;
}

BackendImpl::~BackendImpl() {
  if (gpio_wait_thread_ && gpio_wait_thread_->joinable()) {
    wait_handle_.signal(0, ZX_EVENT_SIGNALED);
    gpio_wait_thread_->join();
  }

  if (spi_tx_buf_) {
    zx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(spi_tx_buf_),
                                kSpiMaxBufferSize);
  }
  if (spi_rx_buf_) {
    zx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(spi_rx_buf_),
                                kSpiMaxBufferSize);
  }

  ioctl_grt_spi_close_transfer_handles(spi_fd_);
  zx_handle_close(spi_trans_.rx_vmo);
  zx_handle_close(spi_trans_.tx_vmo);
}

constexpr char kEmptyFrame[] = "EMPTY_FRAME";

bool BackendImpl::ReadyToTransact() {
  auto pin = kReqGpioPin;
  uint32_t level;
  ioctl_grt_get_gpio_level(gpio_fd_, &pin, &level);
  return level == 0;
}

zx_status_t BackendImpl::Transact(const uint8_t* tx_buf,
                                  uint8_t* rx_buf,
                                  size_t size,
                                  size_t* actual_read) {
  if (size > kSpiMaxBufferSize)
    return ZX_ERR_INVALID_ARGS;

  memcpy(spi_tx_buf_, tx_buf, size);
  auto status = ioctl_grt_spi_trigger_transfer(spi_fd_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "ioctl_grt_spi_transfer failed: " << status;
    return status;
  }

  if (strncmp((char*)spi_rx_buf_, kEmptyFrame, strlen(kEmptyFrame)) == 0) {
    *actual_read = 0;
  } else {
    memcpy(rx_buf, spi_rx_buf_, size);
    *actual_read = size;
  }

  return ZX_OK;
}
