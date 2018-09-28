// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_net.h"

#include <fcntl.h>
#include <string.h>
#include <atomic>

#include <trace-engine/types.h>
#include <trace/event.h>
#include <zircon/device/ethernet.h>
#include <zx/fifo.h>

#include "lib/fxl/logging.h"

namespace machina {

constexpr size_t kMaxPacketSize = 2048;

VirtioNet::Stream::Stream(const PhysMem& phys_mem,
                          async_dispatcher_t* dispatcher, VirtioQueue* queue,
                          std::atomic<trace_async_id_t>* trace_flow_id,
                          IoBuffer* io_buf)
    : phys_mem_(phys_mem),
      dispatcher_(dispatcher),
      queue_(queue),
      trace_flow_id_(trace_flow_id),
      io_buf_(io_buf),
      queue_wait_(dispatcher, queue,
                  fit::bind_member(this, &VirtioNet::Stream::OnQueueReady)) {}

zx_status_t VirtioNet::Stream::Start(zx_handle_t fifo, size_t fifo_max_entries,
                                     bool rx) {
  fifo_ = fifo;
  rx_ = rx;
  fifo_entries_.resize(fifo_max_entries);
  fifo_num_entries_ = 0;
  fifo_entries_write_index_ = 0;

  fifo_readable_wait_.set_object(fifo_);
  fifo_readable_wait_.set_trigger(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED);
  fifo_writable_wait_.set_object(fifo_);
  fifo_writable_wait_.set_trigger(ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED);

  // One async job will pipe buffers from the queue into the FIFO.
  zx_status_t status = WaitOnQueue();
  if (status == ZX_OK) {
    // A second async job will return buffers from the FIFO to the queue.
    status = WaitOnFifoReadable();
  }
  return status;
}

zx_status_t VirtioNet::Stream::WaitOnQueue() { return queue_wait_.Begin(); }

virtio_net_hdr_t* VirtioNet::Stream::ReadPacketInfo(uint16_t index,
                                                    uintptr_t* offset,
                                                    uintptr_t* length) {
  VirtioDescriptor desc;

  zx_status_t status = queue_->ReadDesc(index, &desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read descriptor from queue";
    return nullptr;
  }

  auto header = reinterpret_cast<virtio_net_hdr_t*>(desc.addr);
  if (!desc.has_next) {
    *offset = phys_mem_.offset(header + 1);
    *length = static_cast<uint16_t>(desc.len - sizeof(*header));
  } else if (desc.len == sizeof(virtio_net_hdr_t)) {
    status = queue_->ReadDesc(desc.next, &desc);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to read chained descriptor from queue";
      return nullptr;
    }
    *offset = phys_mem_.offset(desc.addr, desc.len);
    *length = static_cast<uint16_t>(desc.len);
  }

  if (desc.has_next) {
    FXL_LOG(ERROR) << "Packet data must be on a single buffer";
    return nullptr;
  }

  return header;
}

void VirtioNet::Stream::OnQueueReady(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

  // Attempt to correlate the processing of descriptors with a previous
  // notification. As noted in virtio_device.cc this should be considered
  // best-effort only.
  const trace_async_id_t flow_id = trace_flow_id_->load();
  TRACE_DURATION("machina", "virtio_net_packet_read_from_queue", "direction",
                 TA_STRING_LITERAL(rx_ ? "RX" : "TX"), "flow_id", flow_id);
  if (flow_id != 0) {
    TRACE_FLOW_STEP("machina", "queue_signal", flow_id);
  }

  FXL_DCHECK(fifo_num_entries_ == 0);
  fifo_num_entries_ = 0;
  fifo_entries_write_index_ = 0;
  do {
    uintptr_t packet_offset;
    uintptr_t packet_length;
    virtio_net_hdr_t* header =
        ReadPacketInfo(index, &packet_offset, &packet_length);
    if (header == nullptr) {
      return;
    }

    if (packet_length > kMaxPacketSize) {
      FXL_LOG(ERROR) << "Packet may not be longer than " << kMaxPacketSize;
      return;
    }

    uintptr_t io_offset = 0;
    status = io_buf_->Allocate(&io_offset);
    // We should have sized our buffer so that failure cannot happen here, but
    // if somehow this is not true let us check to avoid any hard to find bugs.
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to allocate buffer.";
    }
    if (rx_) {
      // Section 5.1.6.4.1 Device Requirements: Processing of Incoming Packets

      // If VIRTIO_NET_F_MRG_RXBUF has not been negotiated, the device MUST
      // set num_buffers to 1.
      header->num_buffers = 1;

      // If none of the VIRTIO_NET_F_GUEST_TSO4, TSO6 or UFO options have been
      // negotiated, the device MUST set gso_type to VIRTIO_NET_HDR_GSO_NONE.
      header->gso_type = VIRTIO_NET_HDR_GSO_NONE;

      // If VIRTIO_NET_F_GUEST_CSUM is not negotiated, the device MUST set
      // flags to zero and SHOULD supply a fully checksummed packet to the
      // driver.
      header->flags = 0;
    } else {
      status =
          io_buf_->vmo().write(phys_mem_.as<void>(packet_offset, packet_length),
                               io_offset, packet_length);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to write to Ethernet VMO";
        return;
      }
    }

    FXL_DCHECK(fifo_num_entries_ < fifo_entries_.size());
    fifo_entries_[fifo_num_entries_++] = {
        .offset = static_cast<uint32_t>(io_offset),
        .length = static_cast<uint16_t>(packet_length),
        .flags = 0,
        .cookie = reinterpret_cast<void*>(index),
    };
  } while (fifo_num_entries_ < fifo_entries_.size() &&
           queue_->NextAvail(&index) == ZX_OK);

  status = WaitOnFifoWritable();
  if (status != ZX_OK) {
    FXL_LOG(INFO) << "Failed to wait on fifo writable: " << status;
  }
}

zx_status_t VirtioNet::Stream::WaitOnFifoWritable() {
  return fifo_writable_wait_.Begin(dispatcher_);
}

void VirtioNet::Stream::OnFifoWritable(async_dispatcher_t* dispatcher,
                                       async::WaitBase* wait,
                                       zx_status_t status,
                                       const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(INFO) << "Async wait failed on fifo writable: " << status;
    return;
  }

  // Attempt to correlate the processing of packets with an existing flow.
  const trace_async_id_t flow_id = trace_flow_id_->load();
  TRACE_DURATION("machina", "virtio_net_packet_pipe_to_fifo", "direction",
                 TA_STRING_LITERAL(rx_ ? "RX" : "TX"), "flow_id", flow_id);
  if (flow_id != 0) {
    TRACE_FLOW_STEP("machina", "queue_signal", flow_id);
  }

  size_t num_entries_written = 0;
  status = zx_fifo_write(
      fifo_, sizeof(fifo_entries_[0]),
      static_cast<const void*>(&fifo_entries_[fifo_entries_write_index_]),
      fifo_num_entries_, &num_entries_written);
  fifo_entries_write_index_ += num_entries_written;
  fifo_num_entries_ -= num_entries_written;
  if (status == ZX_ERR_SHOULD_WAIT ||
      (status == ZX_OK && fifo_num_entries_ > 0)) {
    status = wait->Begin(dispatcher);
    if (status != ZX_OK) {
      FXL_LOG(INFO) << "Async wait failed on fifo writable: " << status;
    }
    return;
  }
  if (status == ZX_OK) {
    status = WaitOnQueue();
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed write entries to fifo: " << status;
  }
}

zx_status_t VirtioNet::Stream::WaitOnFifoReadable() {
  return fifo_readable_wait_.Begin(dispatcher_);
}

void VirtioNet::Stream::OnFifoReadable(async_dispatcher_t* dispatcher,
                                       async::WaitBase* wait,
                                       zx_status_t status,
                                       const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(INFO) << "Async wait failed on fifo readable: " << status;
    return;
  }

  // Attempt to correlate the processing of packets with an existing flow.
  const trace_async_id_t flow_id = trace_flow_id_->exchange(0);
  TRACE_DURATION("machina", "virtio_net_packet_return_to_queue", "direction",
                 TA_STRING_LITERAL(rx_ ? "RX" : "TX"), "flow_id", flow_id);
  if (flow_id != 0) {
    TRACE_FLOW_END("machina", "queue_signal", flow_id);
  }

  // Dequeue entries for the Ethernet device.
  size_t num_entries_read;
  eth_fifo_entry_t entries[fifo_entries_.size()];
  status = zx_fifo_read(fifo_, sizeof(entries[0]), entries, countof(entries),
                        &num_entries_read);
  if (status == ZX_ERR_SHOULD_WAIT) {
    status = wait->Begin(dispatcher);
    if (status != ZX_OK) {
      FXL_LOG(INFO) << "Async wait failed on fifo readable: " << status;
    }
    return;
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read from fifo: " << status;
    return;
  }

  for (size_t i = 0; i < num_entries_read; i++) {
    auto head = reinterpret_cast<uintptr_t>(entries[i].cookie);
    auto io_offset = entries[i].offset;
    if (rx_) {
      // Reread the original descriptor so we can perform the copy. A malicious
      // guest could have changed the descriptor under us so we reverify it is
      // valid just to protect ourselves
      uintptr_t packet_offset;
      uintptr_t packet_length;
      if (ReadPacketInfo(head, &packet_offset, &packet_length) == nullptr) {
        return;
      }
      // entries[i].length is the actual size of the packet received by the
      // ethdriver and to minimize copying we use this in preference to
      // packet_length. As packet_length was what we originally gave as our
      // buffer size to the Ethernet FIFO we are guaranteed that
      // entries[i].length <= packet_length.
      status = io_buf_->vmo().read(
          phys_mem_.as<void>(packet_offset, entries[i].length), io_offset,
          entries[i].length);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read from Ethernet VMO";
        return;
      }
    }
    io_buf_->Free(io_offset);
    auto length = entries[i].length + sizeof(virtio_net_hdr_t);
    status = queue_->Return(head, length);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to return descriptor to the queue " << status;
      return;
    }
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FXL_LOG(INFO) << "Async wait failed on fifo readable: " << status;
  }
}

zx_status_t VirtioNet::IoBuffer::Init(size_t count, size_t elem_size) {
  size_t vmo_size = count * elem_size;
  zx_status_t status = zx::vmo::create(vmo_size, ZX_VMO_NON_RESIZABLE, &vmo_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create VMO for buffer";
    return status;
  }

  elem_size_ = elem_size;

  free_list_.reserve(count);
  for (size_t i = 0; i < count; i++) {
    // push them in reverse order just for convenience of the initial
    // allocations
    // of buffers from Allocate progressing forwards instead of backwards.
    free_list_.push_back(count - i - 1);
  }
  return ZX_OK;
}

zx_status_t VirtioNet::IoBuffer::Allocate(uintptr_t* offset) {
  if (free_list_.empty()) {
    return ZX_ERR_NO_MEMORY;
  }
  uint16_t elem = free_list_.back();
  *offset = static_cast<uintptr_t>(elem) * elem_size_;
  free_list_.pop_back();
  return ZX_OK;
}

void VirtioNet::IoBuffer::Free(uintptr_t offset) {
  free_list_.push_back(offset / elem_size_);
}

VirtioNet::VirtioNet(const PhysMem& phys_mem, async_dispatcher_t* dispatcher)
    // TODO(abdulla): Support VIRTIO_NET_F_STATUS via IOCTL_ETHERNET_GET_STATUS.
    : VirtioInprocessDevice(phys_mem, VIRTIO_NET_F_MAC),
      rx_stream_(phys_mem, dispatcher, rx_queue(), rx_trace_flow_id(),
                 &io_buf_),
      tx_stream_(phys_mem, dispatcher, tx_queue(), tx_trace_flow_id(),
                 &io_buf_) {
  config_.status = VIRTIO_NET_S_LINK_UP;
  config_.max_virtqueue_pairs = 1;
}

VirtioNet::~VirtioNet() {
  zx_handle_close(fifos_.tx_fifo);
  zx_handle_close(fifos_.rx_fifo);
}

zx_status_t VirtioNet::InitIoBuffer(size_t count, size_t elem_size) {
  return io_buf_.Init(count, elem_size);
}

zx_status_t VirtioNet::Start(const char* path) {
  net_fd_.reset(open(path, O_RDONLY));
  if (!net_fd_) {
    return ZX_ERR_IO;
  }

  eth_info_t info;
  ssize_t ret = ioctl_ethernet_get_info(net_fd_.get(), &info);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to get Ethernet device info";
    return ret;
  }
  // TODO(abdulla): Use a different MAC address from the host.
  memcpy(config_.mac, info.mac, sizeof(config_.mac));

  ret = ioctl_ethernet_get_fifos(net_fd_.get(), &fifos_);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to get FIFOs from Ethernet device";
    return ret;
  }

  // We make some assumptions on sizing our IO buf based on how the ethernet
  // FIFOs work. Essentially we need to ensure that we have enough buffers such
  // that we can potentially fully fill the RX FIFO, whilst still having enough
  // buffers that we can efficiently do TX. We would also like to ensure that
  // being able to place an item into either RX or TX FIFO should imply that we
  // have a free buffer. In the worst case we could have rx_depth enqueued in
  // the RX FIFO, tx_depth enqueued in the TX FIFO and tx_depth currently in
  // flight on the hardware. This yields the below calculation and with current
  // FIFO depths of 256 will yield a 1.5MiB vmo.
  zx_status_t status = InitIoBuffer((fifos_.rx_depth + fifos_.tx_depth * 2), kMaxPacketSize);
  if (status != ZX_OK) {
    return status;
  }
  zx::vmo vmo_dup;
  status = io_buf_.vmo().duplicate(ZX_RIGHTS_IO | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER, &vmo_dup);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate VMO for ethernet";
    return status;
  }
  zx_handle_t vmo = vmo_dup.release();
  ret = ioctl_ethernet_set_iobuf(net_fd_.get(), &vmo);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to set VMO for Ethernet device";
    zx_handle_close(vmo);
    return ret;
  }
  ret = ioctl_ethernet_set_client_name(net_fd_.get(), "machina", 7);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to set client name for Ethernet device";
    return ret;
  }
  ret = ioctl_ethernet_start(net_fd_.get());
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to start communication with Ethernet device";
    return ret;
  }

  FXL_LOG(INFO) << "Polling device " << path << " for Ethernet frames";
  return WaitOnFifos(fifos_);
}

zx_status_t VirtioNet::WaitOnFifos(const eth_fifos_t& fifos) {
  zx_status_t status = rx_stream_.Start(fifos.rx_fifo, fifos.rx_depth, true);
  if (status == ZX_OK) {
    status = tx_stream_.Start(fifos.tx_fifo, fifos.tx_depth, false);
  }
  return status;
}

}  // namespace machina
