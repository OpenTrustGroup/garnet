// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transport.h"

#include <lib/async/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/nebula/base/logging.h>

SpiTransport::SpiTransport(component::ApplicationContext* application_context,
                           async_t* async,
                           Backend* backend)
    : backend_(backend), async_(async) {
  backend_->RegisterRxRequestListener([this]() {
    std::unique_lock<std::mutex> lk(mutex_);
    pending_rx_reqs_++;
    if (pending_rx_reqs_ > 0)
      cv_.notify_one();
  });

  worker_thread_ = std::thread([this]() { ThreadLoop(); });

  if (application_context)
    application_context->outgoing_services()->AddService<SpiService::Transport>
        ([this](fidl::InterfaceRequest<SpiService::Transport> request) {
          bindings_.AddBinding(this, std::move(request));
        });
}

void SpiTransport::ThreadLoop() {
  while (true) {
    std::unique_lock<std::mutex> lk(mutex_);
    cv_.wait(lk, [this]() { return has_pending_work() || should_exit_; });

    if (should_exit_) {
      break;
    }

    do {
      std::string tx_buf;

      if (!tx_queue_.empty()) {
        tx_buf = std::move(tx_queue_.front());
        tx_queue_.pop_front();
      } else {
        tx_buf.resize(kDummyFrameSize);
        // TODO: need to fill tx_buf with dummy data
      }

      lk.unlock();
      std::string rx_buf;
      rx_buf.resize(tx_buf.size());
      TriggerSpiTransaction(tx_buf, rx_buf);
      lk.lock();

      if ((!rx_buf.empty())) {
        for (auto& consumer : data_consumers_) {
          if (consumer->Send(rx_buf) != ZX_OK) {
            LOG(ERROR) << "Failed to send data to consumer";
          }
        }
        pending_rx_reqs_--;
      }
    } while (has_pending_work());
  }
}

void SpiTransport::TriggerSpiTransaction(std::string& tx_buf,
                                         std::string& rx_buf) {
  while (!backend_->ReadyToTransact())
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  size_t actual_read = 0;
  auto rx_buf_data = const_cast<char*>(rx_buf.data());
  backend_->Transact(reinterpret_cast<const uint8_t*>(tx_buf.data()),
                     reinterpret_cast<uint8_t*>(rx_buf_data), tx_buf.size(),
                     &actual_read);

  if (actual_read >= 0) {
    rx_buf.resize(actual_read);
    rx_buf.shrink_to_fit();
  }
}

SpiTransport::~SpiTransport() {
  {
    std::unique_lock<std::mutex> lk(mutex_);
    should_exit_ = true;
    cv_.notify_one();
  }
  worker_thread_.join();
}

void SpiTransport::SetRxChannel(zx::channel chan,
                                SetRxChannelCallback callback) {
  auto consumer = std::make_unique<DataConsumer>(std::move(chan), this, async_);
  data_consumers_.push_back(std::move(consumer));
  callback();
}

void SpiTransport::GetTxChannel(GetTxChannelCallback callback) {
  zx::channel t1, t2;
  zx_status_t status = zx::channel::create(0, &t1, &t2);
  if (status != ZX_OK) {
    LOG(ERROR) << "Failed to create TX channel: " << status;
    callback(zx::channel());
    return;
  }

  auto client = std::make_unique<Client>(std::move(t1), this, async_);
  clients_.push_back(std::move(client));
  callback(std::move(t2));
}

zx_status_t SpiTransport::AppendTxBuffer(uint8_t* buffer, uint32_t size) {
  std::unique_lock<std::mutex> lk(mutex_);
  if (tx_queue_.size() >= kMaxTxQueueSize) {
    LOG(ERROR) << "TX queue is full, dropping data";
    std::string err_buf = "Queue overflow";
    for (auto& consumer : data_consumers_) {
      if (consumer->Send(err_buf) != ZX_OK) {
        LOG(ERROR) << "Failed to send data to consumer";
      }
    }
    tx_queue_.clear();
    return ZX_ERR_SHOULD_WAIT;
  }
  tx_queue_.push_back(std::string(reinterpret_cast<char*>(buffer), size));
  cv_.notify_one();
  return ZX_OK;
}

bool SpiTransport::has_pending_work() const {
  return !tx_queue_.empty() || pending_rx_reqs_ > 0;
}

void SpiTransport::RemoveClient(Client* client) {
  std::unique_lock<std::mutex> lk(mutex_);
  auto it = std::remove_if(
      clients_.begin(), clients_.end(),
      [client](const std::unique_ptr<Client>& c) { return c.get() == client; });
  if (it != clients_.end()) {
    clients_.erase(it, clients_.end());
  }
}

void SpiTransport::RemoveDataConsumer(DataConsumer* consumer) {
  std::unique_lock<std::mutex> lk(mutex_);
  auto it = std::remove_if(data_consumers_.begin(), data_consumers_.end(),
                           [consumer](const std::unique_ptr<DataConsumer>& c) {
                             return c.get() == consumer;
                           });
  if (it != data_consumers_.end()) {
    data_consumers_.erase(it, data_consumers_.end());
  }
}

SpiTransport::Client::Client(zx::channel chan,
                             SpiTransport* transport,
                             async_t* async)
    : transport_(transport),
      waiter_(this),
      chan_(std::move(chan)),
      async_(async) {
  waiter_.set_object(chan_.get());
  waiter_.set_trigger(ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE);
  auto status = waiter_.Begin(async);
  if (status != ZX_OK)
    LOG(WARNING) << "Failed to wait on channel: status=" << status;
}

SpiTransport::Client::~Client() {
  waiter_.Cancel(async_);
}

async_wait_result_t SpiTransport::Client::OnDataReceived(
    async_t* async,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    async::PostTask(async, [this]() { transport_->RemoveClient(this); });
    return ASYNC_WAIT_FINISHED;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    uint8_t buffer[kSpiMaxBufferSize];
    uint32_t actual_bytes = 0;
    zx_status_t read_status = chan_.read(0, buffer, sizeof(buffer),
                                         &actual_bytes, nullptr, 0, nullptr);
    if (read_status == ZX_OK && actual_bytes > 0) {
      if (transport_->AppendTxBuffer(buffer, actual_bytes) != ZX_OK) {
        LOG(ERROR) << "Failed to append data to TX buffer";
        //return ASYNC_WAIT_AGAIN;
      }
    } else {
      LOG(ERROR) << "Failed to read data: " << read_status;
    }
  }

  return ASYNC_WAIT_AGAIN;
}

SpiTransport::DataConsumer::DataConsumer(zx::channel chan,
                                         SpiTransport* transport,
                                         async_t* async)
    : transport_(transport),
      waiter_(this),
      chan_(std::move(chan)),
      async_(async) {
  waiter_.set_object(chan_.get());
  waiter_.set_trigger(ZX_CHANNEL_PEER_CLOSED);
  auto status = waiter_.Begin(async);
  if (status != ZX_OK)
    LOG(WARNING) << "Failed to wait on channel: status=" << status;
}

SpiTransport::DataConsumer::~DataConsumer() {
  waiter_.Cancel(async_);
}

async_wait_result_t SpiTransport::DataConsumer::OnChannelClosed(
    async_t* async,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    async::PostTask(async, [this]() { transport_->RemoveDataConsumer(this); });
    return ASYNC_WAIT_FINISHED;
  }

  return ASYNC_WAIT_AGAIN;
}

zx_status_t SpiTransport::DataConsumer::Send(std::string& data) {
  if (!chan_.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }
  return chan_.write(0, data.data(), data.size(), nullptr, 0);
}
