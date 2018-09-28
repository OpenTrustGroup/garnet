// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "l2cap.h"

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/task_domain.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"

#include "garnet/drivers/bluetooth/lib/l2cap/channel_manager.h"
#include "garnet/drivers/bluetooth/lib/l2cap/socket_factory.h"

namespace btlib {
namespace l2cap {
namespace {

class Impl final : public L2CAP, public common::TaskDomain<Impl, L2CAP> {
 public:
  Impl(fxl::RefPtr<hci::Transport> hci, std::string thread_name)
      : L2CAP(),
        common::TaskDomain<Impl, L2CAP>(this, std::move(thread_name)),
        hci_(hci) {
    ZX_DEBUG_ASSERT(hci_);
  }

  void Initialize() override {
    PostMessage([this] {
      // This can only run once during initialization.
      ZX_DEBUG_ASSERT(!chanmgr_);
      chanmgr_ = std::make_unique<ChannelManager>(hci_, dispatcher());
      socket_factory_ = std::make_unique<SocketFactory>();
      bt_log(TRACE, "l2cap", "initialized");
    });
  }

  void ShutDown() override {
    common::TaskDomain<Impl, L2CAP>::ScheduleCleanUp();
  }

  // Called on the L2CAP runner as a result of ScheduleCleanUp().
  void CleanUp() {
    AssertOnDispatcherThread();
    bt_log(TRACE, "l2cap", "shutting down");
    chanmgr_ = nullptr;
  }

  void AddACLConnection(hci::ConnectionHandle handle,
                        hci::Connection::Role role,
                        LinkErrorCallback link_error_callback,
                        async_dispatcher_t* dispatcher) override {
    PostMessage([this, handle, role, lec = std::move(link_error_callback),
                 dispatcher]() mutable {
      if (chanmgr_) {
        chanmgr_->RegisterACL(handle, role, std::move(lec), dispatcher);
      }
    });
  }

  void AddLEConnection(hci::ConnectionHandle handle, hci::Connection::Role role,
                       LEConnectionParameterUpdateCallback conn_param_callback,
                       LinkErrorCallback link_error_callback,
                       AddLEConnectionCallback channel_callback,
                       async_dispatcher_t* dispatcher) override {
    PostMessage([this, handle, role, cp_cb = std::move(conn_param_callback),
                 link_err_cb = std::move(link_error_callback),
                 chan_cb = std::move(channel_callback), dispatcher]() mutable {
      if (chanmgr_) {
        chanmgr_->RegisterLE(handle, role, std::move(cp_cb),
                             std::move(link_err_cb), dispatcher);

        auto att = chanmgr_->OpenFixedChannel(handle, kATTChannelId);
        auto smp = chanmgr_->OpenFixedChannel(handle, kLESMPChannelId);
        ZX_DEBUG_ASSERT(att);
        ZX_DEBUG_ASSERT(smp);
        async::PostTask(dispatcher, [att = std::move(att), smp = std::move(smp),
                                     cb = std::move(chan_cb)]() mutable {
          cb(std::move(att), std::move(smp));
        });
      }
    });
  }

  void RemoveConnection(hci::ConnectionHandle handle) override {
    PostMessage([this, handle] {
      if (chanmgr_) {
        chanmgr_->Unregister(handle);
      }
    });
  }

  void OpenChannel(hci::ConnectionHandle handle, PSM psm, ChannelCallback cb,
                   async_dispatcher_t* dispatcher) override {
    ZX_DEBUG_ASSERT(dispatcher);

    PostMessage([this, handle, psm, cb = std::move(cb), dispatcher]() mutable {
      if (chanmgr_) {
        chanmgr_->OpenChannel(handle, psm, std::move(cb), dispatcher);
      }
    });
  }

  void RegisterService(PSM psm, ChannelCallback channel_callback,
                       async_dispatcher_t* dispatcher) override {
    PostMessage([this, psm, channel_callback = std::move(channel_callback),
                 dispatcher]() mutable {
      if (chanmgr_) {
        const bool result = chanmgr_->RegisterService(
            psm, std::move(channel_callback), dispatcher);

        ZX_DEBUG_ASSERT(result);
      } else {
        // RegisterService could be called early in host initialization, so log
        // cases where L2CAP isn't ready for a service handler.
        bt_log(WARN, "l2cap",
               "failed to register handler for PSM %#.4x while uninitialized",
               psm);
      }
    });
  }

  void RegisterService(PSM psm, SocketCallback socket_callback,
                       async_dispatcher_t* cb_dispatcher) override {
    RegisterService(
        psm,
        [this, psm, cb = std::move(socket_callback),
         cb_dispatcher](auto channel) mutable {
          zx::socket s = socket_factory_->MakeSocketForChannel(channel);
          // Called every time the service is connected, cb must be shared.
          async::PostTask(cb_dispatcher,
                          [s = std::move(s), cb = cb.share(),
                           handle = channel->link_handle()]() mutable {
                            cb(std::move(s), handle);
                          });
        },
        dispatcher());
  }

  void UnregisterService(PSM psm) override {
    PostMessage([this, psm] {
      if (chanmgr_) {
        chanmgr_->UnregisterService(psm);
      }
    });
  }

 private:
  fxl::RefPtr<hci::Transport> hci_;

  // This must be only accessed on the L2CAP task runner.
  std::unique_ptr<ChannelManager> chanmgr_;

  // Makes sockets for RegisterService, should only be used on the L2CAP task
  // runner.
  std::unique_ptr<SocketFactory> socket_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Impl);
};

}  // namespace

// static
fbl::RefPtr<L2CAP> L2CAP::Create(fxl::RefPtr<hci::Transport> hci,
                                 std::string thread_name) {
  ZX_DEBUG_ASSERT(hci);

  return AdoptRef(new Impl(hci, std::move(thread_name)));
}

}  // namespace l2cap
}  // namespace btlib
