// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_channel.h"

#include <endian.h>

#include <lib/async/default.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/run_or_post.h"
#include "garnet/drivers/bluetooth/lib/common/run_task_sync.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/time/time_delta.h"

#include "slab_allocators.h"
#include "transport.h"

namespace btlib {
namespace hci {

namespace {

bool IsAsync(EventCode code) {
  return code != kCommandCompleteEventCode && code != kCommandStatusEventCode;
}

};  //  namespace

CommandChannel::QueuedCommand::QueuedCommand(
    std::unique_ptr<CommandPacket> command_packet,
    std::unique_ptr<TransactionData> transaction_data)
    : packet(std::move(command_packet)), data(std::move(transaction_data)) {
  ZX_DEBUG_ASSERT(data);
  ZX_DEBUG_ASSERT(packet);
}

CommandChannel::TransactionData::TransactionData(
    TransactionId id,
    OpCode opcode,
    EventCode complete_event_code,
    CommandCallback callback,
    async_dispatcher_t* dispatcher)
    : id_(id),
      opcode_(opcode),
      complete_event_code_(complete_event_code),
      callback_(std::move(callback)),
      dispatcher_(dispatcher),
      handler_id_(0u) {
  ZX_DEBUG_ASSERT(id != 0u);
  ZX_DEBUG_ASSERT(dispatcher_);
}

CommandChannel::TransactionData::~TransactionData() {
  if (!callback_ || !dispatcher_) {
    return;
  }

  bt_log(TRACE, "hci",
         "sending kUnspecifiedError for unfinished transaction %zu", id_);
  auto event = EventPacket::New(sizeof(CommandStatusEventParams));
  auto* header = event->mutable_view()->mutable_header();
  auto* params =
      event->mutable_view()->mutable_payload<CommandStatusEventParams>();
  header->event_code = kCommandStatusEventCode;
  header->parameter_total_size = sizeof(CommandStatusEventParams);
  params->status = kUnspecifiedError;
  params->command_opcode = opcode_;

  Complete(std::move(event));
}

void CommandChannel::TransactionData::Start(fit::closure timeout_cb,
                                            zx::duration timeout) {
  // Transactions should only ever be started once.
  ZX_DEBUG_ASSERT(!timeout_task_.is_pending());

  timeout_task_.set_handler(std::move(timeout_cb));
  timeout_task_.PostDelayed(async_get_default_dispatcher(), timeout);
}

void CommandChannel::TransactionData::Complete(
    std::unique_ptr<EventPacket> event) {
  timeout_task_.Cancel();
  if (!callback_) {
    return;
  }
  async::PostTask(dispatcher_,
      [ event = std::move(event),
        callback = std::move(callback_),
        transaction_id = id_]() mutable {
    callback(transaction_id, *event);
  });
  callback_ = nullptr;
}

CommandChannel::EventCallback CommandChannel::TransactionData::MakeCallback() {
  return
      [id = id_, cb = callback_.share()](const EventPacket& event) { cb(id, event); };
}

CommandChannel::CommandChannel(Transport* transport,
                               zx::channel hci_command_channel)
    : next_transaction_id_(1u),
      next_event_handler_id_(1u),
      transport_(transport),
      channel_(std::move(hci_command_channel)),
      channel_wait_(this, channel_.get(), ZX_CHANNEL_READABLE),
      is_initialized_(false),
      allowed_command_packets_(1u) {
  ZX_DEBUG_ASSERT(transport_);
  ZX_DEBUG_ASSERT(channel_.is_valid());
}

CommandChannel::~CommandChannel() {
  // Do nothing. Since Transport is shared across threads, this can be called
  // from any thread and calling ShutDown() would be unsafe.
}

void CommandChannel::Initialize() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(!is_initialized_);

  auto setup_handler_task = [this] {
    zx_status_t status = channel_wait_.Begin(async_get_default_dispatcher());
    if (status != ZX_OK) {
      bt_log(ERROR, "hci", "failed channel setup: %s",
             zx_status_get_string(status));
      channel_wait_.set_object(ZX_HANDLE_INVALID);
      return;
    }
    bt_log(TRACE, "hci", "started I/O handler");
  };

  io_dispatcher_ = transport_->io_dispatcher();
  common::RunTaskSync(setup_handler_task, io_dispatcher_);

  if (channel_wait_.object() == ZX_HANDLE_INVALID)
    return;

  is_initialized_ = true;

  bt_log(INFO, "hci", "initialized");
}

void CommandChannel::ShutDown() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  if (!is_initialized_)
    return;

  bt_log(INFO, "hci", "shutting down");

  common::RunTaskSync([this] { ShutDownInternal(); }, io_dispatcher_);
  io_dispatcher_ = nullptr;
}

void CommandChannel::ShutDownInternal() {
  bt_log(TRACE, "hci", "removing I/O handler");

  // Prevent new command packets from being queued.
  is_initialized_ = false;

  // Stop listening for HCI events.
  zx_status_t status = channel_wait_.Cancel();
  if (status != ZX_OK) {
    bt_log(WARN, "hci", "could not cancel wait on channel: %s",
           zx_status_get_string(status));
  }

  // Drop all queued commands and event handlers. Pending HCI commands will be
  // resolved with an "UnspecifiedError" error code upon destruction.
  {
    std::lock_guard<std::mutex> lock(send_queue_mutex_);
    send_queue_ = std::list<QueuedCommand>();
  }
  {
    std::lock_guard<std::mutex> lock(event_handler_mutex_);
    event_handler_id_map_.clear();
    event_code_handlers_.clear();
    subevent_code_handlers_.clear();
    pending_transactions_.clear();
    async_cmd_handlers_.clear();
  }
}

CommandChannel::TransactionId CommandChannel::SendCommand(
    std::unique_ptr<CommandPacket> command_packet,
    async_dispatcher_t* dispatcher,
    CommandCallback callback,
    const EventCode complete_event_code) {
  if (!is_initialized_) {
    bt_log(TRACE, "hci", "can't send commands while uninitialized");
    return 0u;
  }

  if (complete_event_code == kLEMetaEventCode) {
    return 0u;
  }

  ZX_DEBUG_ASSERT(command_packet);

  if (IsAsync(complete_event_code)) {
    std::lock_guard<std::mutex> lock(event_handler_mutex_);
    auto it = event_code_handlers_.find(complete_event_code);
    // Cannot send an asynchronous command if there's an external event handler
    // registered for the completion event.
    if (it != event_code_handlers_.end() &&
        async_cmd_handlers_.count(complete_event_code) == 0) {
      bt_log(TRACE, "hci", "event handler already handling this event");
      return 0u;
    }
  }

  std::lock_guard<std::mutex> lock(send_queue_mutex_);

  if (next_transaction_id_ == 0u) {
    next_transaction_id_++;
  }

  TransactionId id = next_transaction_id_++;
  auto data = std::make_unique<TransactionData>(
      id, command_packet->opcode(), complete_event_code, std::move(callback),
      dispatcher);

  QueuedCommand command(std::move(command_packet), std::move(data));

  if (IsAsync(complete_event_code)) {
    std::lock_guard<std::mutex> event_lock(event_handler_mutex_);
    MaybeAddTransactionHandler(command.data.get());
  }

  send_queue_.push_back(std::move(command));
  async::PostTask(io_dispatcher_,
                  std::bind(&CommandChannel::TrySendQueuedCommands, this));

  return id;
}

CommandChannel::EventHandlerId CommandChannel::AddEventHandler(
    EventCode event_code,
    EventCallback event_callback,
    async_dispatcher_t* dispatcher) {
  if (event_code == kCommandStatusEventCode ||
      event_code == kCommandCompleteEventCode ||
      event_code == kLEMetaEventCode) {
    return 0u;
  }

  std::lock_guard<std::mutex> lock(event_handler_mutex_);
  auto it = async_cmd_handlers_.find(event_code);
  if (it != async_cmd_handlers_.end()) {
    bt_log(ERROR, "hci",
           "async event handler %zu already registered for event code %#.2x",
           it->second, event_code);
    return 0u;
  }

  auto id = NewEventHandler(event_code, false /* is_le_meta */,
                            std::move(event_callback), dispatcher);
  event_code_handlers_.emplace(event_code, id);
  return id;
}

CommandChannel::EventHandlerId CommandChannel::AddLEMetaEventHandler(
    EventCode subevent_code,
    EventCallback event_callback,
    async_dispatcher_t* dispatcher) {
  std::lock_guard<std::mutex> lock(event_handler_mutex_);

  auto id = NewEventHandler(subevent_code, true /* is_le_meta */,
                            std::move(event_callback), dispatcher);
  subevent_code_handlers_.emplace(subevent_code, id);
  return id;
}

void CommandChannel::RemoveEventHandler(EventHandlerId id) {
  std::lock_guard<std::mutex> lock(event_handler_mutex_);

  // Internal handler ids can't be removed.
  auto it = std::find_if(async_cmd_handlers_.begin(), async_cmd_handlers_.end(),
                         [id](auto&& p) { return p.second == id; });
  if (it != async_cmd_handlers_.end()) {
    return;
  }

  RemoveEventHandlerInternal(id);
}

void CommandChannel::RemoveEventHandlerInternal(EventHandlerId id) {
  auto iter = event_handler_id_map_.find(id);
  if (iter == event_handler_id_map_.end()) {
    return;
  }

  if (iter->second.event_code != 0) {
    auto* event_handlers = iter->second.is_le_meta_subevent
                               ? &subevent_code_handlers_
                               : &event_code_handlers_;

    auto range = event_handlers->equal_range(iter->second.event_code);
    for (auto it = range.first; it != range.second; ++it) {
      if (it->second == id) {
        event_handlers->erase(it);
        break;
      }
    }
  }
  event_handler_id_map_.erase(iter);
}

void CommandChannel::TrySendQueuedCommands() {
  if (!is_initialized_)
    return;

  ZX_DEBUG_ASSERT(async_get_default_dispatcher() == io_dispatcher_);

  if (allowed_command_packets_ == 0) {
    bt_log(DEBUG, "hci", "controller queue full, waiting");
    return;
  }

  std::lock_guard<std::mutex> lock(send_queue_mutex_);

  // Walk the waiting and see if any are sendable.
  for (auto it = send_queue_.begin();
       allowed_command_packets_ > 0 && it != send_queue_.end();) {
    std::lock_guard<std::mutex> event_lock(event_handler_mutex_);

    // Already a pending command with the same opcode, we can't send.
    if (pending_transactions_.count(it->data->opcode()) != 0) {
      ++it;
      continue;
    }

    // We can send this if we only expect one update, or if we aren't
    // waiting for another transaction to complete on the same event.
    // It is unlikely but possible to have commands with different opcodes
    // wait on the same completion event.
    EventCode complete_code = it->data->complete_event_code();
    if (!IsAsync(complete_code) || it->data->handler_id() != 0 ||
        event_code_handlers_.count(complete_code) == 0) {
      SendQueuedCommand(std::move(*it));
      it = send_queue_.erase(it);
      continue;
    }
    ++it;
  }
}

void CommandChannel::SendQueuedCommand(QueuedCommand&& cmd) {
  auto packet_bytes = cmd.packet->view().data();
  zx_status_t status =
      channel_.write(0, packet_bytes.data(), packet_bytes.size(), nullptr, 0);
  if (status < 0) {
    // TODO(armansito): We should notify the |status_callback| of the pending
    // command with a special error code in this case.
    bt_log(ERROR, "hci", "failed to send command: %s",
           zx_status_get_string(status));
    return;
  }
  allowed_command_packets_--;

  auto& transaction = cmd.data;

  transaction->Start(
      [this, id = cmd.data->id()] {
        bt_log(ERROR, "hci", "command %zu timed out, shutting down", id);
        ShutDownInternal();
        // TODO(jamuraa): Have Transport notice we've shutdown. (NET-620)
      },
      zx::msec(kCommandTimeoutMs));

  MaybeAddTransactionHandler(transaction.get());

  pending_transactions_.insert(
      std::make_pair(transaction->opcode(), std::move(transaction)));
}

void CommandChannel::MaybeAddTransactionHandler(TransactionData* data) {
  // We don't need to add a transaction handler for synchronous transactions.
  if (!IsAsync(data->complete_event_code())) {
    return;
  }
  // We already have a handler for this transaction, or another transaction
  // is already waiting and it will be queued.
  if (event_code_handlers_.count(data->complete_event_code())) {
    bt_log(SPEW, "hci", "async command %zu: already has handler", data->id());
    return;
  }

  // The handler hasn't been added yet.
  auto id = NewEventHandler(data->complete_event_code(), false,
                            data->MakeCallback(), data->dispatcher());
  ZX_DEBUG_ASSERT(id != 0u);
  data->set_handler_id(id);
  async_cmd_handlers_[data->complete_event_code()] = id;
  event_code_handlers_.emplace(data->complete_event_code(), id);
  bt_log(SPEW, "hci", "async command %zu assigned handler %zu", data->id(), id);
}

CommandChannel::EventHandlerId CommandChannel::NewEventHandler(
    EventCode event_code,
    bool is_le_meta,
    EventCallback event_callback,
    async_dispatcher_t* dispatcher) {
  ZX_DEBUG_ASSERT(event_code);
  ZX_DEBUG_ASSERT(event_callback);
  ZX_DEBUG_ASSERT(dispatcher);

  auto id = next_event_handler_id_++;
  EventHandlerData data;
  data.id = id;
  data.event_code = event_code;
  data.event_callback = std::move(event_callback);
  data.dispatcher = dispatcher;
  data.is_le_meta_subevent = is_le_meta;

  bt_log(SPEW, "hci", "adding event handler %zu for event code %#.2x", id,
         event_code);
  ZX_DEBUG_ASSERT(event_handler_id_map_.find(id) ==
                  event_handler_id_map_.end());
  event_handler_id_map_[id] = std::move(data);

  return id;
}

void CommandChannel::UpdateTransaction(std::unique_ptr<EventPacket> event) {
  hci::EventCode event_code = event->event_code();

  ZX_DEBUG_ASSERT(event_code == kCommandStatusEventCode ||
                  event_code == kCommandCompleteEventCode);

  OpCode matching_opcode;

  bool async_failed = false;
  if (event->event_code() == kCommandCompleteEventCode) {
    const auto& params = event->view().payload<CommandCompleteEventParams>();
    matching_opcode = le16toh(params.command_opcode);
    allowed_command_packets_ = params.num_hci_command_packets;
  } else {  // kComandStatusEventCode
    const auto& params = event->view().payload<CommandStatusEventParams>();
    matching_opcode = le16toh(params.command_opcode);
    allowed_command_packets_ = params.num_hci_command_packets;
    async_failed = params.status != StatusCode::kSuccess;
  }
  bt_log(DEBUG, "hci", "allowed packets update: %zu", allowed_command_packets_);

  if (matching_opcode == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(event_handler_mutex_);
  auto it = pending_transactions_.find(matching_opcode);
  if (it == pending_transactions_.end()) {
    bt_log(ERROR, "hci", "update for unexpected opcode: %#.4x",
           matching_opcode);
    return;
  }

  auto pending = std::move(it->second);
  pending_transactions_.erase(it);

  ZX_DEBUG_ASSERT(pending->opcode() == matching_opcode);

  pending->Complete(std::move(event));

  // If the command is synchronous, we are done.
  if (pending->handler_id() == 0u) {
    return;
  }

  // TODO(NET-770): Do not allow asynchronous commands to finish with Command
  // Complete.
  if (event_code == kCommandCompleteEventCode) {
    bt_log(WARN, "hci", "async command received CommandComplete");
    async_failed = true;
  }

  // If an asyncronous command failed, then remove it's event handler.
  if (async_failed) {
    RemoveEventHandlerInternal(pending->handler_id());
    async_cmd_handlers_.erase(pending->complete_event_code());
  }
}

void CommandChannel::NotifyEventHandler(std::unique_ptr<EventPacket> event) {
  EventCode event_code;
  const std::unordered_multimap<EventCode, EventHandlerId>* event_handlers;
  std::vector<std::pair<EventCallback, async_dispatcher_t*>> pending_callbacks;

  {
    std::lock_guard<std::mutex> lock(event_handler_mutex_);

    if (event->event_code() == kLEMetaEventCode) {
      event_code = event->view().payload<LEMetaEventParams>().subevent_code;
      event_handlers = &subevent_code_handlers_;
    } else {
      event_code = event->event_code();
      event_handlers = &event_code_handlers_;
    }

    auto range = event_handlers->equal_range(event_code);
    if (range.first == event_handlers->end()) {
      bt_log(TRACE, "hci", "event %#.2x received with no handler", event_code);
      return;
    }

    auto iter = range.first;
    while (iter != range.second) {
      EventCallback callback;
      async_dispatcher_t* dispatcher;
      EventHandlerId event_id = iter->second;
      bt_log(DEBUG, "hci", "notifying handler (id %zu) for event code %#.2x",
             event_id, event_code);
      auto handler_iter = event_handler_id_map_.find(event_id);
      ZX_DEBUG_ASSERT(handler_iter != event_handler_id_map_.end());

      auto& handler = handler_iter->second;
      ZX_DEBUG_ASSERT(handler.event_code == event_code);

      callback = handler.event_callback.share();
      dispatcher = handler.dispatcher;

      ++iter;  // Advance so we don't point to an invalid iterator.
      auto expired_it = async_cmd_handlers_.find(event_code);
      if (expired_it != async_cmd_handlers_.end()) {
        RemoveEventHandlerInternal(event_id);
        async_cmd_handlers_.erase(expired_it);
      }
      pending_callbacks.emplace_back(std::move(callback), dispatcher);
    }
  }
  // Process queue so callbacks can't add a handler if another queued command
  // finishes on the same event.
  TrySendQueuedCommands();

  auto it = pending_callbacks.begin();
  for (; it != pending_callbacks.end() - 1; ++it) {
    auto event_copy = EventPacket::New(event->view().payload_size());
    common::MutableBufferView buf = event_copy->mutable_view()->mutable_data();
    event->view().data().Copy(&buf);
    common::RunOrPost(
        [ev = std::move(event_copy), cb = std::move(it->first)]() { cb(*ev); },
        it->second);
  }
  // Don't copy for the last callback.
  common::RunOrPost(
      [ev = std::move(event), cb = std::move(it->first)]() { cb(*ev); },
      it->second);
}

void CommandChannel::OnChannelReady(
    async_dispatcher_t* dispatcher,
    async::WaitBase* wait,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  ZX_DEBUG_ASSERT(async_get_default_dispatcher() == io_dispatcher_);
  ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_READABLE);

  if (status != ZX_OK) {
    bt_log(TRACE, "hci", "channel error: %s", zx_status_get_string(status));
    return;
  }

  // Allocate a buffer for the event. Since we don't know the size beforehand we
  // allocate the largest possible buffer.
  // TODO(armansito): We could first try to read into a small buffer and retry
  // if the syscall returns ZX_ERR_BUFFER_TOO_SMALL. Not sure if the second
  // syscall would be worth it but investigate.

  for (size_t count = 0; count < signal->count; count++) {
    uint32_t read_size;
    auto packet = EventPacket::New(slab_allocators::kLargeControlPayloadSize);
    if (!packet) {
      bt_log(ERROR, "hci", "failed to allocate event packet!");
      return;
    }
    auto packet_bytes = packet->mutable_view()->mutable_data();
    zx_status_t read_status =
        channel_.read(0u, packet_bytes.mutable_data(), packet_bytes.size(),
                      &read_size, nullptr, 0, nullptr);
    if (read_status < 0) {
      bt_log(TRACE, "hci", "Failed to read event bytes: %s",
             zx_status_get_string(read_status));
      // Clear the handler so that we stop receiving events from it.
      // TODO(jamuraa): signal upper layers that we can't read the channel.
      return;
    }

    if (read_size < sizeof(EventHeader)) {
      bt_log(ERROR, "hci",
             "malformed data packet - expected at least %zu bytes, got %zu",
             sizeof(EventHeader), read_size);
      // TODO(armansito): Should this be fatal? Ignore for now.
      continue;
    }

    // Compare the received payload size to what is in the header.
    const size_t rx_payload_size = read_size - sizeof(EventHeader);
    const size_t size_from_header =
        packet->view().header().parameter_total_size;
    if (size_from_header != rx_payload_size) {
      bt_log(ERROR, "hci",
             "malformed packet - payload size from header (%zu) does not match"
             " received payload size: %zu",
             size_from_header, rx_payload_size);
      continue;
    }

    packet->InitializeFromBuffer();

    if (packet->event_code() == kCommandStatusEventCode ||
        packet->event_code() == kCommandCompleteEventCode) {
      UpdateTransaction(std::move(packet));
      TrySendQueuedCommands();
    } else {
      NotifyEventHandler(std::move(packet));
    }
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    bt_log(TRACE, "hci", "wait error: %s", zx_status_get_string(status));
  }
}

}  // namespace hci
}  // namespace btlib
