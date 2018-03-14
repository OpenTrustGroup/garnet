// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_state.h"

#include "garnet/bin/ui/view_manager/view_impl.h"
#include "garnet/bin/ui/view_manager/view_registry.h"
#include "garnet/bin/ui/view_manager/view_stub.h"
#include "lib/fxl/strings/string_printf.h"

namespace view_manager {

ViewState::ViewState(ViewRegistry* registry,
                     mozart::ViewTokenPtr view_token,
                     f1dl::InterfaceRequest<mozart::View> view_request,
                     mozart::ViewListenerPtr view_listener,
                     scenic_lib::Session* session,
                     const std::string& label)
    : view_token_(std::move(view_token)),
      view_listener_(std::move(view_listener)),
      top_node_(session),
      label_(label),
      impl_(new ViewImpl(registry, this)),
      view_binding_(impl_.get(), std::move(view_request)),
      owner_binding_(impl_.get()),
      weak_factory_(this) {
  FXL_DCHECK(view_token_);
  FXL_DCHECK(view_listener_);

  view_binding_.set_error_handler([this, registry] {
    registry->OnViewDied(this, "View connection closed");
  });
  owner_binding_.set_error_handler([this, registry] {
    registry->OnViewDied(this, "ViewOwner connection closed");
  });
  view_listener_.set_error_handler([this, registry] {
    registry->OnViewDied(this, "ViewListener connection closed");
  });
}

ViewState::~ViewState() {}

void ViewState::IssueProperties(mozart::ViewPropertiesPtr properties) {
  issued_properties_ = std::move(properties);
}

void ViewState::BindOwner(
    f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FXL_DCHECK(!owner_binding_.is_bound());
  owner_binding_.Bind(std::move(view_owner_request));
}

void ViewState::ReleaseOwner() {
  FXL_DCHECK(owner_binding_.is_bound());
  owner_binding_.Unbind();
}

ViewState* ViewState::AsViewState() {
  return this;
}

const std::string& ViewState::FormattedLabel() const {
  if (formatted_label_cache_.empty()) {
    formatted_label_cache_ =
        label_.empty()
            ? fxl::StringPrintf("<V%d>", view_token_->value)
            : fxl::StringPrintf("<V%d:%s>", view_token_->value, label_.c_str());
  }
  return formatted_label_cache_;
}
app::ServiceProvider* ViewState::GetServiceProviderIfSupports(
    std::string service_name) {
  if (service_names_) {
    auto& v = service_names_.storage();
    if (std::find(v.begin(), v.end(), service_name) != v.end()) {
      return service_provider_.get();
    }
  }
  return nullptr;
}

void ViewState::SetServiceProvider(
    f1dl::InterfaceHandle<app::ServiceProvider> service_provider,
    f1dl::Array<f1dl::String> service_names) {
  if (service_provider) {
    service_provider_ = service_provider.Bind();
    service_names_ = std::move(service_names);

  } else {
    service_provider_.Unbind();
    service_names_.reset();
  }
}

void ViewState::RebuildFocusChain() {
  focus_chain_ = std::make_unique<FocusChain>();
  // This will come into play with focus chain management API
  focus_chain_->version = 1;

  // Construct focus chain by adding our ancestors until we hit a root
  size_t index = 0;
  focus_chain_->chain.resize(index + 1);
  focus_chain_->chain[index++] = view_token().Clone();
  ViewState* parent = view_stub()->parent();
  while (parent) {
    focus_chain_->chain.resize(index + 1);
    focus_chain_->chain[index++] = parent->view_token().Clone();
    ViewStub* stub = parent->view_stub();
    parent = stub ? stub->parent() : nullptr;
  }
}

std::ostream& operator<<(std::ostream& os, ViewState* view_state) {
  if (!view_state)
    return os << "null";
  return os << view_state->FormattedLabel();
}

}  // namespace view_manager
