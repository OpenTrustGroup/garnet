// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/SpiService.h>
#include <lib/async/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/app/cpp/application_context.h>
#include <sys/types.h>
#include <zircon/types.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

constexpr size_t kDummyFrameSize = 256;
constexpr size_t kSpiMaxBufferSize = 256;
constexpr uint32_t kMaxTxQueueSize = 64;

class Backend {
 public:
  virtual ~Backend() = default;

  virtual void RegisterRxRequestListener(std::function<void()> callback) = 0;

  virtual bool ReadyToTransact() = 0;

  virtual zx_status_t Transact(const uint8_t* tx_buf,
                               uint8_t* rx_buf,
                               size_t size,
                               size_t* actual_read) = 0;
};

class SpiTransport : public SpiService::Transport {
 public:
  SpiTransport(component::ApplicationContext* application_context,
               async_t* async,
               Backend* backend);
  ~SpiTransport();

  void Bind(fidl::InterfaceRequest<SpiService::Transport> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  size_t num_clients() const { return clients_.size(); }
  size_t num_consumers() const { return data_consumers_.size(); }

 private:
  class Client {
   public:
    Client(zx::channel chan, SpiTransport* transport, async_t* async);
    ~Client();

   private:
    async_wait_result_t OnDataReceived(async_t* async,
                                       zx_status_t status,
                                       const zx_packet_signal_t* signal);
    SpiTransport* transport_;
    async::WaitMethod<Client, &Client::OnDataReceived> waiter_;
    zx::channel chan_;
    async_t* async_;
  };

  class DataConsumer {
   public:
    DataConsumer(zx::channel chan, SpiTransport* transport, async_t* async);
    ~DataConsumer();

    zx_status_t Send(std::string& data);

   private:
    async_wait_result_t OnChannelClosed(async_t* async,
                                        zx_status_t status,
                                        const zx_packet_signal_t* signal);
    SpiTransport* transport_;
    async::WaitMethod<DataConsumer, &DataConsumer::OnChannelClosed> waiter_;
    zx::channel chan_;
    async_t* async_;
  };

  void SetRxChannel(zx::channel chan, SetRxChannelCallback callback) override;

  void GetTxChannel(GetTxChannelCallback callback) override;

  void RemoveClient(Client* client);
  void RemoveDataConsumer(DataConsumer* consumer);

  zx_status_t AppendTxBuffer(uint8_t* buffer, uint32_t size);

  bool has_pending_work() const;

  void ThreadLoop();
  void TriggerSpiTransaction(std::string& tx_buf, std::string& rx_buf);

  Backend* backend_;
  int16_t pending_rx_reqs_ = 0;
  bool should_exit_ = false;

  std::deque<std::string> tx_queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_thread_;
  std::vector<std::unique_ptr<Client>> clients_;
  std::vector<std::unique_ptr<DataConsumer>> data_consumers_;

  async_t* async_;
  fidl::BindingSet<SpiService::Transport> bindings_;
};