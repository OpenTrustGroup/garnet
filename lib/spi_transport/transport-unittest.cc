// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/spi_transport/transport.h"

#include <gtest/gtest.h>
#include <condition_variable>
#include <mutex>

#include <lib/nebula/base/logging.h>

class McuFake : public Backend {
 public:
  void SetBusy(bool busy) { busy_ = busy; }

  zx_status_t Receive(std::string* out_data) {
    if (rx_queue_.empty()) {
      return ZX_ERR_SHOULD_WAIT;
    }
    *out_data = std::move(rx_queue_.front());
    rx_queue_.pop_front();
    return ZX_OK;
  }

  void Wait() {
    std::unique_lock<std::mutex> lk(mutex_);
    auto timeout =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    cv_.wait_until(lk, timeout, [this]() { return !rx_queue_.empty(); });
  }

  void Send(std::string& data) {
    tx_buffer_ = data;
    if (rx_request_listener_)
      rx_request_listener_();
  }

 private:
  void RegisterRxRequestListener(std::function<void()> callback) override {
    rx_request_listener_ = callback;
  }

  bool ReadyToTransact() override { return !busy_; }

  zx_status_t Transact(const uint8_t* tx_buf,
                       uint8_t* rx_buf,
                       size_t size,
                       size_t* actual_read) override {
    std::string tx_data(reinterpret_cast<const char*>(tx_buf), size);
    {
      std::unique_lock<std::mutex> lk(mutex_);
      rx_queue_.push_back(tx_data);
      cv_.notify_one();
    }

    if (!tx_buffer_.empty()) {
      memcpy(rx_buf, tx_buffer_.data(), tx_buffer_.size());
      *actual_read = tx_buffer_.size();
    } else {
      *actual_read = 0;
    }

    return ZX_OK;
  }

  std::function<void()> rx_request_listener_;
  bool busy_ = false;

  std::deque<std::string> rx_queue_;
  std::string tx_buffer_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

class SpiTransportTest : public ::testing::Test {
 protected:
  McuFake mcu_fake_;

  SpiTransportTest()
      : loop_(&kAsyncLoopConfigMakeDefault),
        transport_(nullptr, loop_.async(), &mcu_fake_) {}

  void SetUp() override {
    auto request = transport_svc_.NewRequest();
    transport_.Bind(std::move(request));

    request = transport_sync_svc_.NewRequest();
    transport_.Bind(std::move(request));
  }

  async::Loop loop_;
  SpiService::TransportPtr transport_svc_;
  SpiService::TransportSyncPtr transport_sync_svc_;
  SpiTransport transport_;
};

TEST_F(SpiTransportTest, SingleClient) {
  zx::channel tx_chan;
  transport_svc_->GetTxChannel(
      [&tx_chan](zx::channel chan) { tx_chan = std::move(chan); });

  loop_.RunUntilIdle();
  EXPECT_TRUE(tx_chan.is_valid());
  EXPECT_EQ(transport_.num_clients(), 1U);

  tx_chan.reset();
  loop_.RunUntilIdle();
  EXPECT_EQ(transport_.num_clients(), 0U);
}

TEST_F(SpiTransportTest, SingleConsumer) {
  zx::channel chan1, chan2;
  zx_status_t status = zx::channel::create(0, &chan1, &chan2);
  ASSERT_EQ(status, ZX_OK);

  transport_svc_->SetRxChannel(std::move(chan2), [] {});

  loop_.RunUntilIdle();
  EXPECT_EQ(transport_.num_consumers(), 1U);

  chan1.reset();
  loop_.RunUntilIdle();
  EXPECT_EQ(transport_.num_clients(), 0U);
}

TEST_F(SpiTransportTest, MultiClients) {
  zx::channel tx_chan1;
  transport_svc_->GetTxChannel(
      [&tx_chan1](zx::channel chan) { tx_chan1 = std::move(chan); });

  zx::channel tx_chan2;
  transport_svc_->GetTxChannel(
      [&tx_chan2](zx::channel chan) { tx_chan2 = std::move(chan); });

  loop_.RunUntilIdle();
  EXPECT_TRUE(tx_chan1.is_valid());
  EXPECT_TRUE(tx_chan2.is_valid());
  EXPECT_EQ(transport_.num_clients(), 2U);

  tx_chan1.reset();
  loop_.RunUntilIdle();
  EXPECT_EQ(transport_.num_clients(), 1U);

  tx_chan2.reset();
  loop_.RunUntilIdle();
  EXPECT_EQ(transport_.num_clients(), 0U);
}

TEST_F(SpiTransportTest, MultiConsumers) {
  zx::channel chan1, chan2;
  zx_status_t status = zx::channel::create(0, &chan1, &chan2);
  ASSERT_EQ(status, ZX_OK);

  zx::channel chan3, chan4;
  status = zx::channel::create(0, &chan3, &chan4);
  ASSERT_EQ(status, ZX_OK);

  transport_svc_->SetRxChannel(std::move(chan2), [] {});

  loop_.RunUntilIdle();
  EXPECT_EQ(transport_.num_consumers(), 1U);

  transport_svc_->SetRxChannel(std::move(chan4), [] {});

  loop_.RunUntilIdle();
  EXPECT_EQ(transport_.num_consumers(), 2U);

  chan1.reset();
  loop_.RunUntilIdle();
  EXPECT_EQ(transport_.num_consumers(), 1U);

  chan3.reset();
  loop_.RunUntilIdle();
  EXPECT_EQ(transport_.num_consumers(), 0U);
}

TEST_F(SpiTransportTest, SingleClientSendReceive) {
  auto status = loop_.StartThread();
  EXPECT_EQ(status, ZX_OK);

  zx::channel tx_chan;
  transport_sync_svc_->GetTxChannel(&tx_chan);
  EXPECT_TRUE(tx_chan.is_valid());

  mcu_fake_.SetBusy(true);
  std::string tx_data = "Hello, SPI!";
  status = tx_chan.write(0, tx_data.data(), tx_data.size(), nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  mcu_fake_.Wait();

  std::string rx_data;
  status = mcu_fake_.Receive(&rx_data);
  EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT);

  mcu_fake_.SetBusy(false);
  mcu_fake_.Wait();

  status = mcu_fake_.Receive(&rx_data);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(rx_data, tx_data);

  loop_.Quit();
  loop_.JoinThreads();
}

TEST_F(SpiTransportTest, MultiClientSendReceive) {
  auto status = loop_.StartThread();
  EXPECT_EQ(status, ZX_OK);

  zx::channel tx_chan1;
  transport_sync_svc_->GetTxChannel(&tx_chan1);
  EXPECT_TRUE(tx_chan1.is_valid());

  zx::channel tx_chan2;
  transport_sync_svc_->GetTxChannel(&tx_chan2);
  EXPECT_TRUE(tx_chan2.is_valid());

  mcu_fake_.SetBusy(true);
  std::string tx_data1 = "Hello, SPI 1!";
  status = tx_chan1.write(0, tx_data1.data(), tx_data1.size(), nullptr, 0);
  EXPECT_EQ(status, ZX_OK);

  std::string tx_data2 = "Hello, SPI 2!";
  status = tx_chan2.write(0, tx_data2.data(), tx_data2.size(), nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  mcu_fake_.Wait();

  std::string rx_data;
  status = mcu_fake_.Receive(&rx_data);
  EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT);

  mcu_fake_.SetBusy(false);
  mcu_fake_.Wait();

  status = mcu_fake_.Receive(&rx_data);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(rx_data, tx_data1);

  mcu_fake_.Wait();
  status = mcu_fake_.Receive(&rx_data);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(rx_data, tx_data2);

  loop_.Quit();
  loop_.JoinThreads();
}

static zx_status_t get_rx_data(zx::channel& rx_chan, std::string& rx_data) {
  zx_status_t status = rx_chan.wait_one(
      ZX_CHANNEL_READABLE, zx::deadline_after(zx::msec(10)), nullptr);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t bytes_read = 0;
  rx_data.resize(kSpiMaxBufferSize, 0);
  status = rx_chan.read(0, const_cast<char *>(rx_data.data()), kSpiMaxBufferSize,
                        &bytes_read, nullptr, 0, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  rx_data.resize(bytes_read);
  rx_data.shrink_to_fit();
  return ZX_OK;
}

TEST_F(SpiTransportTest, McuSend) {
  auto status = loop_.StartThread();
  EXPECT_EQ(status, ZX_OK);

  zx::channel rx_chan1, chan1;
  status = zx::channel::create(0, &rx_chan1, &chan1);
  EXPECT_EQ(status, ZX_OK);

  zx::channel rx_chan2, chan2;
  status = zx::channel::create(0, &rx_chan2, &chan2);
  EXPECT_EQ(status, ZX_OK);

  transport_sync_svc_->SetRxChannel(std::move(chan1));
  transport_sync_svc_->SetRxChannel(std::move(chan2));

  mcu_fake_.SetBusy(true);
  std::string tx_data = "Hello, from MCU!";
  mcu_fake_.Send(tx_data);
  mcu_fake_.Wait();

  std::string rx_data;
  status = mcu_fake_.Receive(&rx_data);
  EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT);

  mcu_fake_.SetBusy(false);
  mcu_fake_.Wait();

  status = mcu_fake_.Receive(&rx_data);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(rx_data.size(), kDummyFrameSize);

  status = get_rx_data(rx_chan1, rx_data);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(rx_data, tx_data);

  status = get_rx_data(rx_chan2, rx_data);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(rx_data, tx_data);

  loop_.Quit();
  loop_.JoinThreads();
}