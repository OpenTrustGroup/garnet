// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/test/fakes/fake_view.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/fxl/logging.h"

namespace media_player {
namespace test {
namespace {

constexpr uint32_t kViewTokenValue = 1;

}  // namespace

FakeView::FakeView()
    : dispatcher_(async_get_default_dispatcher()),
      binding_(this),
      service_provider_binding_(this),
      input_connection_binding_(this) {}

FakeView::~FakeView() {}

void FakeView::Bind(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request,
    ::fuchsia::ui::viewsv1::ViewListenerPtr listener,
    zx::eventpair parent_export_token, fidl::StringPtr label) {
  binding_.Bind(std::move(view_request));
  owner_.Bind(std::move(view_owner_request));
  view_listener_ = std::move(listener);
  parent_export_token_ = std::move(parent_export_token);
  label_ = label;
}

void FakeView::GetToken(GetTokenCallback callback) {
  ::fuchsia::ui::viewsv1token::ViewToken view_token;
  view_token.value = kViewTokenValue;
  callback(view_token);
}

void FakeView::GetServiceProvider(
    fidl::InterfaceRequest<::fuchsia::sys::ServiceProvider> service_provider) {
  service_provider_binding_.Bind(std::move(service_provider));
}

void FakeView::OfferServiceProvider(
    fidl::InterfaceHandle<::fuchsia::sys::ServiceProvider> service_provider,
    fidl::VectorPtr<fidl::StringPtr> service_names) {
  FXL_NOTIMPLEMENTED();
}

void FakeView::GetContainer(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewContainer> container) {
  FXL_NOTIMPLEMENTED();
}

void FakeView::ConnectToService(fidl::StringPtr name, zx::channel channel) {
  if (name == ::fuchsia::ui::input::InputConnection::Name_) {
    input_connection_binding_.Bind(std::move(channel));
    return;
  }

  FXL_LOG(ERROR) << "ServiceProvider::ConnectToService: name " << name
                 << "  not recognized";
}

void FakeView::SetEventListener(
    fidl::InterfaceHandle<::fuchsia::ui::input::InputListener> listener) {
  input_view_listener_ = listener.Bind();
}

void FakeView::GetInputMethodEditor(
    ::fuchsia::ui::input::KeyboardType keyboard_type,
    ::fuchsia::ui::input::InputMethodAction action,
    ::fuchsia::ui::input::TextInputState initial_state,
    fidl::InterfaceHandle<::fuchsia::ui::input::InputMethodEditorClient> client,
    fidl::InterfaceRequest<::fuchsia::ui::input::InputMethodEditor> editor) {
  FXL_NOTIMPLEMENTED();
}

void FakeView::ShowKeyboard() {
  FXL_NOTIMPLEMENTED();
}

void FakeView::HideKeyboard() {
  FXL_NOTIMPLEMENTED();
}

FakeView::Owner::Owner() : binding_(this) {}

FakeView::Owner::~Owner() {}

void FakeView::Owner::Bind(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request) {
  binding_.Bind(std::move(view_owner_request));
}

void FakeView::Owner::GetToken(GetTokenCallback callback) {
  FXL_LOG(INFO) << "Owner::GetToken";
  ::fuchsia::ui::viewsv1token::ViewToken view_token;
  view_token.value = kViewTokenValue;
  callback(view_token);
}

}  // namespace test
}  // namespace media_player
