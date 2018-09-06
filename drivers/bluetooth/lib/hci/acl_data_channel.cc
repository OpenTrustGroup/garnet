// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acl_data_channel.h"

#include <endian.h>

#include <lib/async/default.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/run_task_sync.h"
#include "lib/fxl/strings/string_printf.h"
#include "slab_allocators.h"
#include "transport.h"

namespace btlib {
namespace hci {

DataBufferInfo::DataBufferInfo(size_t max_data_length, size_t max_num_packets)
    : max_data_length_(max_data_length), max_num_packets_(max_num_packets) {}

DataBufferInfo::DataBufferInfo() : max_data_length_(0u), max_num_packets_(0u) {}

bool DataBufferInfo::operator==(const DataBufferInfo& other) const {
  return max_data_length_ == other.max_data_length_ &&
         max_num_packets_ == other.max_num_packets_;
}

ACLDataChannel::ACLDataChannel(Transport* transport,
                               zx::channel hci_acl_channel)
    : transport_(transport),
      channel_(std::move(hci_acl_channel)),
      channel_wait_(this, channel_.get(), ZX_CHANNEL_READABLE),
      is_initialized_(false),
      event_handler_id_(0u),
      io_dispatcher_(nullptr),
      rx_dispatcher_(nullptr),
      num_sent_packets_(0u),
      le_num_sent_packets_(0u) {
  // TODO(armansito): We'll need to pay attention to ZX_CHANNEL_WRITABLE as
  // well.
  ZX_DEBUG_ASSERT(transport_);
  ZX_DEBUG_ASSERT(channel_.is_valid());
}

ACLDataChannel::~ACLDataChannel() {
  // Do nothing. Since Transport is shared across threads, this can be called
  // from any thread and calling ShutDown() would be unsafe.
}

void ACLDataChannel::Initialize(const DataBufferInfo& bredr_buffer_info,
                                const DataBufferInfo& le_buffer_info) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(!is_initialized_);
  ZX_DEBUG_ASSERT(bredr_buffer_info.IsAvailable() ||
                  le_buffer_info.IsAvailable());

  bredr_buffer_info_ = bredr_buffer_info;
  le_buffer_info_ = le_buffer_info;

  auto setup_handler_task = [this] {
    zx_status_t status = channel_wait_.Begin(async_get_default_dispatcher());
    if (status != ZX_OK) {
      bt_log(ERROR, "hci", "failed channel setup %s",
             zx_status_get_string(status));
      channel_wait_.set_object(ZX_HANDLE_INVALID);
      return;
    }
    bt_log(TRACE, "hci", "started I/O handler");
  };

  io_dispatcher_ = transport_->io_dispatcher();
  common::RunTaskSync(setup_handler_task, io_dispatcher_);

  // TODO(jamuraa): return whether we successfully initialized?
  if (channel_wait_.object() == ZX_HANDLE_INVALID)
    return;

  event_handler_id_ = transport_->command_channel()->AddEventHandler(
      kNumberOfCompletedPacketsEventCode,
      std::bind(&ACLDataChannel::NumberOfCompletedPacketsCallback, this,
                std::placeholders::_1),
      io_dispatcher_);
  ZX_DEBUG_ASSERT(event_handler_id_);

  is_initialized_ = true;

  bt_log(INFO, "hci", "initialized");
}

void ACLDataChannel::ShutDown() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  if (!is_initialized_)
    return;

  bt_log(INFO, "hci", "shutting down");

  auto handler_cleanup_task = [this] {
    bt_log(TRACE, "hci", "removing I/O handler");
    zx_status_t status = channel_wait_.Cancel();
    if (status != ZX_OK) {
      bt_log(WARN, "hci", "couldn't cancel wait on channel: %s",
             zx_status_get_string(status));
    }
  };

  common::RunTaskSync(handler_cleanup_task, io_dispatcher_);

  transport_->command_channel()->RemoveEventHandler(event_handler_id_);

  is_initialized_ = false;

  {
    std::lock_guard<std::mutex> lock(send_mutex_);
    send_queue_.clear();
  }

  io_dispatcher_ = nullptr;
  event_handler_id_ = 0u;
  SetDataRxHandler(nullptr, nullptr);
}

void ACLDataChannel::SetDataRxHandler(DataReceivedCallback rx_callback,
                                      async_dispatcher_t* rx_dispatcher) {
  std::lock_guard<std::mutex> lock(rx_mutex_);
  rx_callback_ = std::move(rx_callback);
  rx_dispatcher_ = rx_dispatcher;
}

bool ACLDataChannel::SendPacket(ACLDataPacketPtr data_packet,
                                Connection::LinkType ll_type) {
  if (!is_initialized_) {
    bt_log(TRACE, "hci", "cannot send packets while uninitialized");
    return false;
  }

  ZX_DEBUG_ASSERT(data_packet);

  if (data_packet->view().payload_size() > GetBufferMTU(ll_type)) {
    bt_log(ERROR, "hci", "ACL data packet too large!");
    return false;
  }

  std::lock_guard<std::mutex> lock(send_mutex_);

  send_queue_.emplace_back(QueuedDataPacket(ll_type, std::move(data_packet)));

  TrySendNextQueuedPacketsLocked();

  return true;
}

bool ACLDataChannel::SendPackets(common::LinkedList<ACLDataPacket> packets,
                                 Connection::LinkType ll_type) {
  if (!is_initialized_) {
    bt_log(TRACE, "hci", "cannot send packets while uninitialized");
    return false;
  }

  if (packets.is_empty()) {
    bt_log(TRACE, "hci", "no packets to send!");
    return false;
  }

  // Make sure that all packets are within the MTU.
  for (const auto& packet : packets) {
    if (packet.view().payload_size() > GetBufferMTU(ll_type)) {
      bt_log(ERROR, "hci", "ACL data packet too large!");
      return false;
    }
  }

  std::lock_guard<std::mutex> lock(send_mutex_);

  while (!packets.is_empty()) {
    send_queue_.emplace_back(QueuedDataPacket(ll_type, packets.pop_front()));
  }

  TrySendNextQueuedPacketsLocked();

  return true;
}

bool ACLDataChannel::ClearLinkState(hci::ConnectionHandle handle) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  auto iter = pending_links_.find(handle);
  if (iter == pending_links_.end()) {
    bt_log(TRACE, "hci", "no pending packets on connection (handle: %#.4x)",
           handle);
    return false;
  }

  const PendingPacketData& data = iter->second;
  if (data.ll_type == Connection::LinkType::kLE) {
    DecrementLETotalNumPacketsLocked(data.count);
  } else {
    DecrementTotalNumPacketsLocked(data.count);
  }

  pending_links_.erase(iter);

  // Try sending the next batch of packets in case buffer space opened up.
  TrySendNextQueuedPacketsLocked();

  return true;
}

const DataBufferInfo& ACLDataChannel::GetBufferInfo() const {
  return bredr_buffer_info_;
}

const DataBufferInfo& ACLDataChannel::GetLEBufferInfo() const {
  return le_buffer_info_.IsAvailable() ? le_buffer_info_ : bredr_buffer_info_;
}

size_t ACLDataChannel::GetBufferMTU(Connection::LinkType ll_type) const {
  if (ll_type == Connection::LinkType::kACL)
    return bredr_buffer_info_.max_data_length();
  return GetLEBufferInfo().max_data_length();
}

void ACLDataChannel::NumberOfCompletedPacketsCallback(
    const EventPacket& event) {
  ZX_DEBUG_ASSERT(async_get_default_dispatcher() == io_dispatcher_);
  ZX_DEBUG_ASSERT(event.event_code() == kNumberOfCompletedPacketsEventCode);

  const auto& payload =
      event.view().payload<NumberOfCompletedPacketsEventParams>();
  size_t total_comp_packets = 0;
  size_t le_total_comp_packets = 0;

  std::lock_guard<std::mutex> lock(send_mutex_);

  for (uint8_t i = 0; i < payload.number_of_handles; ++i) {
    const NumberOfCompletedPacketsEventData* data = payload.data + i;

    auto iter = pending_links_.find(le16toh(data->connection_handle));
    if (iter == pending_links_.end()) {
      bt_log(WARN, "hci",
             "controller reported sent packets on unknown connection handle!");
      continue;
    }

    uint16_t comp_packets = le16toh(data->hc_num_of_completed_packets);

    ZX_DEBUG_ASSERT(iter->second.count);
    if (iter->second.count < comp_packets) {
      bt_log(WARN, "hci",
             "packet tx count mismatch! (handle: %#.4x, expected: %zu, "
             "actual : %u)",
             le16toh(data->connection_handle), iter->second.count,
             comp_packets);

      iter->second.count = 0u;

      // On debug builds it's better to assert and crash so that we can catch
      // controller bugs. On release builds we log the warning message above and
      // continue.
      ZX_PANIC("controller reported incorrect packet count!");
    } else {
      iter->second.count -= comp_packets;
    }

    if (iter->second.ll_type == Connection::LinkType::kACL) {
      total_comp_packets += comp_packets;
    } else {
      le_total_comp_packets += comp_packets;
    }

    if (!iter->second.count) {
      pending_links_.erase(iter);
    }
  }

  DecrementTotalNumPacketsLocked(total_comp_packets);
  DecrementLETotalNumPacketsLocked(le_total_comp_packets);
  TrySendNextQueuedPacketsLocked();
}

void ACLDataChannel::TrySendNextQueuedPacketsLocked() {
  if (!is_initialized_)
    return;

  size_t avail_bredr_packets = GetNumFreeBREDRPacketsLocked();
  size_t avail_le_packets = GetNumFreeLEPacketsLocked();

  // Based on what we know about controller buffer availability, we process
  // packets that are currently in |send_queue_|. The packets that can be sent
  // are added to |to_send|. Packets that cannot be sent remain in
  // |send_queue_|.
  DataPacketQueue to_send;
  for (auto iter = send_queue_.begin(); iter != send_queue_.end();) {
    if (!avail_bredr_packets && !avail_le_packets)
      break;

    if (send_queue_.front().ll_type == Connection::LinkType::kACL &&
        avail_bredr_packets) {
      --avail_bredr_packets;
    } else if (send_queue_.front().ll_type == Connection::LinkType::kLE &&
               avail_le_packets) {
      --avail_le_packets;
    } else {
      // Cannot send packet yet, so skip it.
      ++iter;
      continue;
    }

    to_send.push_back(std::move(*iter));
    send_queue_.erase(iter++);
  }

  if (to_send.empty())
    return;

  size_t bredr_packets_sent = 0;
  size_t le_packets_sent = 0;
  while (!to_send.empty()) {
    const QueuedDataPacket& packet = to_send.front();

    auto packet_bytes = packet.packet->view().data();
    zx_status_t status =
        channel_.write(0, packet_bytes.data(), packet_bytes.size(), nullptr, 0);
    if (status < 0) {
      bt_log(ERROR, "hci",
             "failed to send data packet to HCI driver (%s) - dropping packet",
             zx_status_get_string(status));
      to_send.pop_front();
      continue;
    }

    if (packet.ll_type == Connection::LinkType::kACL) {
      ++bredr_packets_sent;
    } else {
      ++le_packets_sent;
    }

    auto iter = pending_links_.find(packet.packet->connection_handle());
    if (iter == pending_links_.end()) {
      pending_links_[packet.packet->connection_handle()] =
          PendingPacketData(packet.ll_type);
    } else {
      iter->second.count++;
    }

    to_send.pop_front();
  }

  IncrementTotalNumPacketsLocked(bredr_packets_sent);
  IncrementLETotalNumPacketsLocked(le_packets_sent);
}

size_t ACLDataChannel::GetNumFreeBREDRPacketsLocked() const {
  ZX_DEBUG_ASSERT(bredr_buffer_info_.max_num_packets() >= num_sent_packets_);
  return bredr_buffer_info_.max_num_packets() - num_sent_packets_;
}

size_t ACLDataChannel::GetNumFreeLEPacketsLocked() const {
  if (!le_buffer_info_.IsAvailable())
    return GetNumFreeBREDRPacketsLocked();

  ZX_DEBUG_ASSERT(le_buffer_info_.max_num_packets() >= le_num_sent_packets_);
  return le_buffer_info_.max_num_packets() - le_num_sent_packets_;
}

void ACLDataChannel::DecrementTotalNumPacketsLocked(size_t count) {
  ZX_DEBUG_ASSERT(num_sent_packets_ >= count);
  num_sent_packets_ -= count;
}

void ACLDataChannel::DecrementLETotalNumPacketsLocked(size_t count) {
  if (!le_buffer_info_.IsAvailable()) {
    DecrementTotalNumPacketsLocked(count);
    return;
  }

  ZX_DEBUG_ASSERT(le_num_sent_packets_ >= count);
  le_num_sent_packets_ -= count;
}

void ACLDataChannel::IncrementTotalNumPacketsLocked(size_t count) {
  ZX_DEBUG_ASSERT(num_sent_packets_ + count <=
                  bredr_buffer_info_.max_num_packets());
  num_sent_packets_ += count;
}

void ACLDataChannel::IncrementLETotalNumPacketsLocked(size_t count) {
  if (!le_buffer_info_.IsAvailable()) {
    IncrementTotalNumPacketsLocked(count);
    return;
  }

  ZX_DEBUG_ASSERT(le_num_sent_packets_ + count <=
                  le_buffer_info_.max_num_packets());
  le_num_sent_packets_ += count;
}

void ACLDataChannel::OnChannelReady(
    async_dispatcher_t* dispatcher,
    async::WaitBase* wait,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    bt_log(ERROR, "hci", "channel error: %s", zx_status_get_string(status));
    return;
  }

  if (!is_initialized_) {
    return;
  }

  ZX_DEBUG_ASSERT(async_get_default_dispatcher() == io_dispatcher_);
  ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_READABLE);

  std::lock_guard<std::mutex> lock(rx_mutex_);
  if (!rx_callback_) {
    return;
  }

  for (size_t count = 0; count < signal->count; count++) {
    // Allocate a buffer for the event. Since we don't know the size beforehand
    // we allocate the largest possible buffer.
    auto packet = ACLDataPacket::New(slab_allocators::kLargeACLDataPayloadSize);
    if (!packet) {
      bt_log(ERROR, "hci",
             "failed to allocate buffer received ACL data packet!");
      return;
    }
    uint32_t read_size;
    auto packet_bytes = packet->mutable_view()->mutable_data();
    zx_status_t read_status =
        channel_.read(0u, packet_bytes.mutable_data(), packet_bytes.size(),
                      &read_size, nullptr, 0, nullptr);
    if (read_status < 0) {
      bt_log(TRACE, "hci", "failed to read RX bytes: %s",
             zx_status_get_string(status));
      // Clear the handler so that we stop receiving events from it.
      // TODO(jamuraa): signal failure to the consumer so it can do something.
      return;
    }

    if (read_size < sizeof(ACLDataHeader)) {
      bt_log(ERROR, "hci",
             "malformed data packet - expected at least %zu bytes, got %zu",
             sizeof(ACLDataHeader), read_size);
      // TODO(jamuraa): signal stream error somehow
      continue;
    }

    const size_t rx_payload_size = read_size - sizeof(ACLDataHeader);
    const size_t size_from_header =
        le16toh(packet->view().header().data_total_length);
    if (size_from_header != rx_payload_size) {
      bt_log(ERROR, "hci",
             "malformed packet - payload size from header (%zu) does not match"
             " received payload size: %zu",
             size_from_header, rx_payload_size);
      // TODO(jamuraa): signal stream error somehow
      continue;
    }

    packet->InitializeFromBuffer();

    ZX_DEBUG_ASSERT(rx_dispatcher_);

    async::PostTask(rx_dispatcher_,
                    [cb = rx_callback_.share(), packet = std::move(packet)]() mutable {
                      cb(std::move(packet));
                    });
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    bt_log(ERROR, "hci", "wait error: %s", zx_status_get_string(status));
  }
}

}  // namespace hci
}  // namespace btlib
