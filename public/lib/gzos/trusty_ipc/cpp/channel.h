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

#include "trusty_ipc.h"

namespace trusty_ipc {

static constexpr uint32_t kTipcChanMaxBufItems = 32;
static constexpr uint32_t kTipcChanMaxBufSize = 4096;

class TipcChannelImpl
    : public TipcChannel,
      public TipcObject,
      public fbl::DoublyLinkedListable<fbl::RefPtr<TipcChannelImpl>> {
 public:
  using Callback = std::function<void()>;

  TipcChannelImpl()
      : initialized_(false),
        peer_shared_items_ready_(false),
        no_free_item_(false),
        binding_(this),
        ready_(false) {}

  zx_status_t Init(uint32_t num_items, size_t item_size);

  // |TipcObject|
  void Close() override;
  uint32_t ReadEvent() override;

  auto GetInterfaceHandle() {
    fidl::InterfaceHandle<TipcChannel> handle;
    binding_.Bind(handle.NewRequest());
    return handle;
  }

  void Bind(fidl::InterfaceHandle<TipcChannel> handle);
  void UnBind();
  bool IsBound();

  void SetReadyCallback(Callback callback) {
    fbl::AutoLock lock(&lock_);
    ready_callback_ = std::move(callback);
  }
  void SetHupCallback(Callback callback) {
    fbl::AutoLock lock(&lock_);
    hup_callback_ = std::move(callback);
  }
  void SetMessageCallback(Callback callback) {
    fbl::AutoLock lock(&lock_);
    message_callback_ = std::move(callback);
  }
  void SetCloseCallback(Callback callback) {
    fbl::AutoLock lock(&lock_);
    close_callback_ = std::move(callback);
  }

  zx_status_t SendMessage(void* buf, size_t buf_size);
  zx_status_t SendMessage(ipc_msg_t* msg, size_t& actual_send);
  zx_status_t GetMessage(uint32_t* msg_id, size_t* len);
  zx_status_t ReadMessage(uint32_t msg_id, uint32_t offset, void* buf,
                          size_t* buf_size);
  zx_status_t ReadMessage(uint32_t msg_id, uint32_t offset, ipc_msg_t* msg,
                          size_t& actual_read);
  zx_status_t PutMessage(uint32_t msg_id);
  void NotifyReady();

  bool is_bound() { return peer_.is_bound(); }
  bool is_ready() {
    fbl::AutoLock lock(&lock_);
    return ready_;
  }

 protected:
  ObjectType get_type() override { return ObjectType::CHANNEL; }

  // |TipcChannel|
  void Hup(HupCallback callback) override;
  void Ready() override;
  void RequestSharedMessageItems(
      RequestSharedMessageItemsCallback callback) override;
  void GetFreeMessageItem(GetFreeMessageItemCallback callback) override;
  void NotifyMessageItemIsFilled(uint32_t msg_id, uint64_t msg_size) override;
  void NotifyFreeItemAvailable() override;

 private:
  using MsgList = fbl::DoublyLinkedList<fbl::unique_ptr<MessageItem>>;
  fbl::Mutex lock_;
  MsgList free_list_ FXL_GUARDED_BY(lock_);
  MsgList outgoing_list_ FXL_GUARDED_BY(lock_);
  MsgList filled_list_ FXL_GUARDED_BY(lock_);
  MsgList read_list_ FXL_GUARDED_BY(lock_);
  bool initialized_ FXL_GUARDED_BY(lock_);
  bool peer_shared_items_ready_ FXL_GUARDED_BY(lock_);
  bool no_free_item_ FXL_GUARDED_BY(lock_);

  fidl::Binding<TipcChannel> binding_;

  TipcChannelSyncPtr peer_;
  std::vector<fbl::unique_ptr<MessageItem>> peer_shared_items_;

  bool ready_ FXL_GUARDED_BY(lock_);

  void UnBindLocked()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  zx_status_t PopulatePeerSharedItemsLocked()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  zx_status_t CheckMessageParameter(ipc_msg_t* msg);

  Callback ready_callback_ FXL_GUARDED_BY(lock_);
  Callback hup_callback_ FXL_GUARDED_BY(lock_);
  Callback message_callback_ FXL_GUARDED_BY(lock_);
  Callback close_callback_ FXL_GUARDED_BY(lock_);
};

}  // namespace trusty_ipc
