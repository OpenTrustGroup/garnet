// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/spi_transport/cpp/client.h"

#include <lib/zx/channel.h>

constexpr size_t kMaxRxBufferSize = 256;

zx_status_t SpiTransportClient::Init() {
  bool success = transport_sync_svc_->GetTxChannel(&tx_chan_);
  if (!success) {
    FXL_LOG(ERROR) << "Failed to get Tx channel";
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (!tx_chan_.is_valid()) {
    FXL_LOG(ERROR) << "Tx channel is not valid";
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx::channel chan;
  auto status = zx::channel::create(0, &rx_chan_, &chan);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create Rx channel: " << status;
    return status;
  }

  success = transport_sync_svc_->SetRxChannel(std::move(chan));
  if (!success) {
    FXL_LOG(ERROR) << "Failed to set Rx channel";
    return ZX_ERR_NOT_SUPPORTED;
  }

  rx_thread_ = std::make_unique<std::thread>([this]() {
    while (true) {
      zx_signals_t signals;
      rx_chan_.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                        zx::time::infinite(), &signals);
      if (signals & ZX_CHANNEL_PEER_CLOSED) {
        FXL_LOG(INFO) << "Rx channel closed";
        break;
      }

      char buffer[kMaxRxBufferSize];

      uint32_t actual_bytes = 0;
      zx_status_t status = rx_chan_.read(0, buffer, sizeof(buffer),
                                         &actual_bytes, nullptr, 0, nullptr);
      if (status == ZX_OK && actual_bytes > 0) {
        if (rx_callback_)
          rx_callback_(buffer, actual_bytes);
      } else {
        FXL_LOG(ERROR) << "Failed to read spi data: " << status;
        continue;
      }
    }
  });

  return ZX_OK;
}

zx_status_t SpiTransportClient::Send(char* data, size_t size) {
  if (!tx_chan_.is_valid()) {
    FXL_LOG(ERROR) << "Tx channel is not valid";
    return ZX_ERR_BAD_STATE;
  }
  return tx_chan_.write(0, data, size, nullptr, 0);
}
