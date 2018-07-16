// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <trusty_ipc/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/gzos/trusty_ipc/cpp/msg_item.h"
#include "lib/gzos/trusty_ipc/cpp/object.h"
#include "lib/gzos/trusty_ipc/cpp/object_manager.h"

namespace trusty_ipc {

static constexpr uint32_t kTipcChanMaxBufItems = 32;
static constexpr uint32_t kTipcChanMaxBufSize = 4096;

class TipcChannelImpl
    : public TipcChannel,
      public TipcObject,
      public fbl::DoublyLinkedListable<fbl::RefPtr<TipcChannelImpl>> {
 public:
  using Callback = std::function<void()>;

  static zx_status_t Create(uint32_t num_items, size_t item_size,
                            fbl::RefPtr<TipcChannelImpl>* out);

  auto GetInterfaceHandle() {
    fidl::InterfaceHandle<TipcChannel> handle;
    binding_.Bind(handle.NewRequest());
    return handle;
  }

  void BindPeerInterfaceHandle(fidl::InterfaceHandle<TipcChannel> handle) {
    peer_.Bind(std::move(handle));
  }

  void SetReadyCallback(Callback callback) {
    ready_callback_ = std::move(callback);
  }
  void SetCloseCallback(Callback callback) {
    close_callback_ = std::move(callback);
  }
  void SetMessageInCallback(Callback callback) {
    message_in_callback_ = std::move(callback);
  }

  zx_status_t SendMessage(void* msg, size_t msg_size);
  zx_status_t GetMessage(uint32_t* msg_id, size_t* len);
  zx_status_t ReadMessage(uint32_t msg_id, uint32_t offset, void* buf,
                          size_t* buf_size);
  zx_status_t PutMessage(uint32_t msg_id);
  virtual void Shutdown() override;
  void NotifyReady();

  bool is_bound() { return peer_.is_bound(); }
  bool is_ready() {
    fbl::AutoLock lock(&ready_lock_);
    return ready_;
  }

 protected:
  ObjectType get_type() override { return ObjectType::CHANNEL; }

  void Close() override;
  void Ready() override;
  void RequestSharedMessageItems(
      RequestSharedMessageItemsCallback callback) override;
  void GetFreeMessageItem(GetFreeMessageItemCallback callback) override;
  void NotifyMessageItemIsFilled(
      uint32_t msg_id, uint64_t msg_size,
      NotifyMessageItemIsFilledCallback callback) override;

 private:
  TipcChannelImpl() : binding_(this), ready_(false),
                      peer_shared_items_ready_(false) {}

  using MsgList = fbl::DoublyLinkedList<fbl::unique_ptr<MessageItem>>;
  fbl::Mutex msg_list_lock_;
  MsgList free_list_ FXL_GUARDED_BY(msg_list_lock_);
  MsgList outgoing_list_ FXL_GUARDED_BY(msg_list_lock_);
  MsgList filled_list_ FXL_GUARDED_BY(msg_list_lock_);
  MsgList read_list_ FXL_GUARDED_BY(msg_list_lock_);

  fidl::Binding<TipcChannel> binding_;
  size_t num_items_;

  TipcChannelSyncPtr peer_;
  std::vector<fbl::unique_ptr<MessageItem>> peer_shared_items_;

  fbl::Mutex ready_lock_;
  bool ready_ FXL_GUARDED_BY(ready_lock_);

  fbl::Mutex request_shared_items_lock_;
  bool peer_shared_items_ready_ FXL_GUARDED_BY(request_shared_items_lock_);
  zx_status_t PopulatePeerSharedItemsLocked()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(request_shared_items_lock_);

  Callback ready_callback_;
  Callback close_callback_;
  Callback message_in_callback_;
};

}  // namespace trusty_ipc
