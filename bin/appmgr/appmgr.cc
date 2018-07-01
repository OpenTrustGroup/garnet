// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/appmgr.h"

namespace component {
namespace {
constexpr char kRootLabel[] = "app";
}  // namespace

Appmgr::Appmgr(async_t* async, AppmgrArgs args)
    : loader_vfs_(async),
      loader_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      publish_vfs_(async),
      publish_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      sysmgr_url_(std::move(args.sysmgr_url)),
      sysmgr_args_(std::move(args.sysmgr_args)) {
  // 1. Serve loader.
  loader_dir_->AddEntry(
      fuchsia::sys::Loader::Name_,
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        root_loader_.AddBinding(
            fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
        return ZX_OK;
      })));

  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0) {
    FXL_LOG(FATAL) << "Appmgr unable to create channel.";
    return;
  }

  if (loader_vfs_.ServeDirectory(loader_dir_, std::move(h2)) != ZX_OK) {
    FXL_LOG(FATAL) << "Appmgr unable to serve directory.";
    return;
  }

  RealmArgs realm_args{nullptr, std::move(h1), kRootLabel,
                       args.run_virtual_console};
  root_realm_ = std::make_unique<Realm>(std::move(realm_args));

  // 2. Publish outgoing directories.
  if (args.pa_directory_request != ZX_HANDLE_INVALID) {
    auto svc = fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
      return root_realm_->BindSvc(std::move(channel));
    }));
    publish_dir_->AddEntry("hub", root_realm_->hub_dir());
    publish_dir_->AddEntry("svc", svc);
    publish_vfs_.ServeDirectory(publish_dir_,
                                zx::channel(args.pa_directory_request));
  }

  // 3. Run sysmgr
  auto run_sysmgr = [this] {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = sysmgr_url_;
    launch_info.arguments.reset(sysmgr_args_);
    root_realm_->CreateComponent(std::move(launch_info), sysmgr_.NewRequest());
  };

  if (!args.retry_sysmgr_crash) {
    run_sysmgr();
    return;
  }
  async::PostTask(async, [this, run_sysmgr] {
    run_sysmgr();
    sysmgr_.set_error_handler(run_sysmgr);
  });
}

Appmgr::~Appmgr() = default;

}  // namespace component
