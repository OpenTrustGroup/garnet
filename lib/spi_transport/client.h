// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/SpiService.h>
#include <lib/async/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>

#include <lib/fxl/logging.h>

#include <functional>
#include <thread>

using RxDataCallback = std::function<void(const char *data, size_t size)>;

class SpiTransportClient {
 public:
  SpiTransportClient() {
    auto status = loop_.StartThread();
    FXL_CHECK(status == ZX_OK);
  }

  ~SpiTransportClient() {
    tx_chan_.reset();
    rx_chan_.reset();

    if (rx_thread_ && rx_thread_->joinable()) {
      rx_thread_->join();
    }

    loop_.Quit();
    loop_.JoinThreads();
  }

  fidl::InterfaceRequest<SpiService::Transport> NewRequest() {
    return transport_sync_svc_.NewRequest();
  }

  zx_status_t Init();
  zx_status_t Send(char *data, size_t size);

  void ReigsterRxCallback(RxDataCallback callback) {
    rx_callback_ = std::move(callback);
  }

 private:
  async::Loop loop_;
  SpiService::TransportSyncPtr transport_sync_svc_;
  RxDataCallback rx_callback_;
  std::unique_ptr<std::thread> rx_thread_;

  zx::channel tx_chan_;
  zx::channel rx_chan_;
};