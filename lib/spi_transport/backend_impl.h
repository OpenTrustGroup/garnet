// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/grt-gpio.h>
#include <zircon/device/grt-spi.h>
#include "lib/nebula/base/logging.h"
#include "lib/nebula/base/unique_fd.h"

#include "transport.h"

class BackendImpl : public Backend {
 public:
  BackendImpl()
      : gpio_fd_(open(GRT_GPIO_CONTROL_DEVICE, O_RDWR)),
        spi_fd_(open(GRT_SPI_CONTROL_DEVICE, O_RDWR)) {}

  ~BackendImpl();
  zx_status_t Init();

 private:
  void RegisterRxRequestListener(std::function<void()> callback) override {
    rx_request_listener_ = callback;
  }

  bool ReadyToTransact() override;

  zx_status_t Transact(const uint8_t* tx_buf,
                       uint8_t* rx_buf,
                       size_t size,
                       size_t* actual_read) override;

  std::function<void()> rx_request_listener_;
  nebula::base::unique_fd gpio_fd_;
  nebula::base::unique_fd spi_fd_;

  zx::event wait_handle_;
  std::unique_ptr<std::thread> gpio_wait_thread_;
  bool should_exit_ = false;

  void *spi_tx_buf_;
  void *spi_rx_buf_;
  grt_spi_transfer spi_trans_;
};
