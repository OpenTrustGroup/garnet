// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_
#define GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_

#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <fuchsia/cpp/virtualization.h>

#include "lib/fxl/functional/closure.h"
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <virtio/virtio_ids.h>
#include <virtio/vsock.h>
#include "lib/fxl/logging_rate_limiter.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue_waiter.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

using virtualization::HostVsockConnector;

namespace machina {

static constexpr uint16_t kVirtioVsockNumQueues = 3;
static constexpr uint32_t kVirtioVsockHostCid = 2;

class VirtioVsock : public VirtioDeviceBase<VIRTIO_ID_VSOCK,
                                            kVirtioVsockNumQueues,
                                            virtio_vsock_config_t> {
 public:
  struct ConnectionKey {
    uint32_t local_cid;
    uint32_t local_port;
    uint32_t remote_cid;
    uint32_t remote_port;
    bool operator==(const ConnectionKey& key) const {
      return local_cid == key.local_cid && local_port == key.local_port &&
             remote_cid == key.remote_cid && remote_port == key.remote_port;
    }
  };

  struct ConnectionHash {
    size_t operator()(const ConnectionKey& key) const {
      // Use the golden ratio constant (0x9e3779b9) to mix bits.
      // This helps to reduce collisions by distributing hash values uniformly.
      uint32_t hash = key.local_cid;

      // Mix in local_port
      hash ^= key.local_port + 0x9e3779b9 + (hash << 6) + (hash >> 2);

      // Mix in remote_cid (Fixes the critical bug where this field was ignored)
      hash ^= key.remote_cid + 0x9e3779b9 + (hash << 6) + (hash >> 2);

      // Mix in remote_port
      hash ^= key.remote_port + 0x9e3779b9 + (hash << 6) + (hash >> 2);

      return static_cast<size_t>(hash);
    }
  };

  VirtioVsock(const PhysMem&, async_t* async);
  ~VirtioVsock();

  uint32_t guest_cid() const;

  // Check whether a connection exists. The connection is identified by a local
  // tuple, local_cid/local_port, and a remote tuple, guest_cid/remote_port. The
  // local tuple identifies the host-side of the connection, and the remote
  // tuple identifies the guest-side of the connection.
  bool HasConnection(uint32_t src_cid,
                     uint32_t src_port,
                     uint32_t dst_cid,
                     uint32_t dst_port) const;

  VirtioQueue* rx_queue() { return queue(0); }
  VirtioQueue* tx_queue() { return queue(1); }

  void Start(
      fidl::InterfaceHandle<virtualization::HostVsockConnector> connector);
  void Stop(void);

  class Connection;
  class NullConnection;
  class SocketConnection;
  class ChannelConnection;

 private:
  using ConnectionMap = std::
      unordered_map<ConnectionKey, std::unique_ptr<Connection>, ConnectionHash>;
  using ConnectionSet = std::unordered_set<ConnectionKey, ConnectionHash>;

  using StreamFunc = void (VirtioVsock::*)(zx_status_t, uint16_t);
  template <StreamFunc F>
  class Stream {
   public:
    Stream(async_t* async, VirtioQueue* queue, VirtioVsock* device);
    ~Stream();

    zx_status_t WaitOnQueue();
   private:
    VirtioQueueWaiter waiter_;
  };

  fbl::atomic<uint32_t> ConnectClientNums{0};
  void ConnectCallback(ConnectionKey key,
                       zx_status_t status,
                       zx::handle handle,
                       uint32_t buf_alloc,
                       uint32_t fwd_cnt);

  zx_status_t AddConnectionLocked(ConnectionKey key,
                                  std::unique_ptr<Connection> conn)
      __TA_REQUIRES(mutex_);
  Connection* GetConnectionLocked(ConnectionKey key) __TA_REQUIRES(mutex_);
  bool EraseOnErrorLocked(ConnectionKey key, zx_status_t status)
      __TA_REQUIRES(mutex_);
  void WaitOnQueueLocked(ConnectionKey key) __TA_REQUIRES(mutex_);

  void Mux(zx_status_t status, uint16_t index);
  void Demux(zx_status_t status, uint16_t index);

  async_t* const async_;
  Stream<&VirtioVsock::Mux> rx_stream_;
  Stream<&VirtioVsock::Demux> tx_stream_;

  mutable std::mutex mutex_;
  ConnectionMap connections_ __TA_GUARDED(mutex_);
  ConnectionSet readable_ __TA_GUARDED(mutex_);

  virtualization::HostVsockConnectorPtr connector_;
};

class VirtioVsock::Connection {
 public:
  Connection(async_t* async,
             virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
             fxl::Closure queue_callback);
  virtual ~Connection();
  virtual zx_status_t Init() = 0;

  uint32_t flags() const { return flags_; }
  uint16_t op() const {
    std::lock_guard<std::mutex> lock(op_update_mutex_);
    return op_;
  }

  zx_status_t Accept();
  void UpdateOp(uint16_t op);

  uint32_t PeerFree() const;
  void ReadCredit(virtio_vsock_hdr_t* header);
  void SetCredit(uint32_t buf_alloc, uint32_t fwd_cnt);
  virtual zx_status_t WriteCredit(virtio_vsock_hdr_t* header) = 0;

  virtual zx_status_t Shutdown(uint32_t flags) = 0;
  virtual zx_status_t Read(VirtioQueue* queue,
                           virtio_vsock_hdr_t* header,
                           virtio_desc_t* desc,
                           uint32_t* used) = 0;
  virtual zx_status_t Write(VirtioQueue* queue,
                            virtio_vsock_hdr_t* header,
                            virtio_desc_t* desc) = 0;

  zx_status_t WaitOnTransmit(zx_status_t status);
  zx_status_t WaitOnReceive(zx_status_t status);

 protected:
  uint32_t flags_ = 0;
  uint32_t rx_cnt_ = 0;
  uint32_t tx_cnt_ = 0;
  uint32_t peer_buf_alloc_ = 0;
  uint32_t peer_fwd_cnt_ = 0;
  uint16_t op_ __TA_GUARDED(op_update_mutex_) = VIRTIO_VSOCK_OP_REQUEST;
  mutable std::mutex op_update_mutex_;

  async_t* async_;
  async::Wait rx_wait_;
  async::Wait tx_wait_;

  virtualization::GuestVsockAcceptor::AcceptCallback accept_callback_;
  fxl::Closure queue_callback_;
};

class VirtioVsock::NullConnection final : public VirtioVsock::Connection {
 public:
  NullConnection() : Connection(nullptr, nullptr, nullptr) {}

  zx_status_t Init() override {
    UpdateOp(VIRTIO_VSOCK_OP_RST);
    return ZX_OK;
  }
  zx_status_t WriteCredit(virtio_vsock_hdr_t* header) override {
    header->buf_alloc = 0;
    header->fwd_cnt = 0;
    return ZX_OK;
  }

  zx_status_t Shutdown(uint32_t flags) override { return ZX_OK; }
  zx_status_t Read(VirtioQueue* queue,
                   virtio_vsock_hdr_t* header,
                   virtio_desc_t* desc,
                   uint32_t* used) override {
    *used = 0;
    return ZX_OK;
  }
  zx_status_t Write(VirtioQueue* queue,
                    virtio_vsock_hdr_t* header,
                    virtio_desc_t* desc) override {
    return ZX_OK;
  }
};

class VirtioVsock::SocketConnection final : public VirtioVsock::Connection {
 public:
  SocketConnection(
      zx::handle handle,
      async_t* async,
      virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
      fxl::Closure queue_callback);
  ~SocketConnection() override;

  zx_status_t Init() override;
  zx_status_t WriteCredit(virtio_vsock_hdr_t* header) override;

  zx_status_t Shutdown(uint32_t flags) override;
  zx_status_t Read(VirtioQueue* queue,
                   virtio_vsock_hdr_t* header,
                   virtio_desc_t* desc,
                   uint32_t* used) override;
  zx_status_t Write(VirtioQueue* queue,
                    virtio_vsock_hdr_t* header,
                    virtio_desc_t* desc) override;

 private:
  void OnReady(zx_status_t status, const zx_packet_signal_t* signal);
  size_t reported_buf_avail_ = 0;
  zx::socket socket_;
};

class VirtioVsock::ChannelConnection final : public VirtioVsock::Connection {
 public:
  ChannelConnection(
      zx::handle handle,
      async_t* async,
      virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
      fxl::Closure queue_callback);
  ~ChannelConnection() override;

  zx_status_t Init() override;
  zx_status_t WriteCredit(virtio_vsock_hdr_t* header) override;

  zx_status_t Shutdown(uint32_t flags) override;
  zx_status_t Read(VirtioQueue* queue,
                   virtio_vsock_hdr_t* header,
                   virtio_desc_t* desc,
                   uint32_t* used) override;
  zx_status_t Write(VirtioQueue* queue,
                    virtio_vsock_hdr_t* header,
                    virtio_desc_t* desc) override;

 private:
  void OnReady(zx_status_t status, const zx_packet_signal_t* signal);
  zx::channel channel_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_
