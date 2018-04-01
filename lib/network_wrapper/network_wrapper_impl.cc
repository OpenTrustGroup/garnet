// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/network_wrapper/network_wrapper_impl.h"

#include <utility>

#include "garnet/lib/callback/cancellable_helper.h"
#include "garnet/lib/callback/destruction_sentinel.h"
#include "garnet/lib/callback/to_function.h"
#include "garnet/lib/callback/trace_callback.h"
#include "lib/fxl/strings/ascii.h"

namespace network_wrapper {

const uint32_t kMaxRedirectCount = 32;
const int32_t kTooManyRedirectErrorCode = -310;
const int32_t kInvalidResponseErrorCode = -320;

class NetworkWrapperImpl::RunningRequest {
 public:
  explicit RunningRequest(
      std::function<network::URLRequest()> request_factory)
      : request_factory_(std::move(request_factory)), redirect_count_(0u) {}

  void Cancel() {
    FXL_DCHECK(on_empty_callback_);
    on_empty_callback_();
  }

  // Set the network service to use. This will start (or restart) the request.
  void SetNetworkService(network::NetworkService* network_service) {
    network_service_ = network_service;
    if (network_service) {
      // Restart the request, as any fidl callback is now pending forever.
      Start();
    }
  }

  void set_callback(std::function<void(network::URLResponse)> callback) {
    // Once this class calls its callback, it must notify its container.
    callback_ = [ this, callback = std::move(callback) ](
        network::URLResponse response) mutable {
      FXL_DCHECK(on_empty_callback_);
      if (destruction_sentinel_.DestructedWhile([
            callback = std::move(callback), &response
          ] { callback(std::move(response)); })) {
        return;
      }
      on_empty_callback_();
    };
  }

  void set_on_empty(const fxl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

 private:
  void Start() {
    // Cancel any pending request.
    url_loader_.Unbind();

    // If no network service has been set, bail out and wait to be called again.
    if (!network_service_)
      return;

    auto request = request_factory_();

    // If last response was a redirect, follow it.
    if (!next_url_.empty())
      request.url = next_url_;

    network_service_->CreateURLLoader(url_loader_.NewRequest());

    const std::string& url = request.url;
    const std::string& method = request.method;
    url_loader_->Start(
        std::move(request),
        TRACE_CALLBACK(
            callback::ToStdFunction([this](network::URLResponse response) {
              url_loader_.Unbind();

              if (response.error) {
                callback_(std::move(response));
                return;
              }

              // 307 and 308 are redirects for which the HTTP
              // method must not change.
              if (response.status_code == 307 ||
                  response.status_code == 308) {
                HandleRedirect(std::move(response));
                return;
              }

              callback_(std::move(response));
              return;
            }),
            "network_wrapper", "network_url_loader_start", "url", url, "method",
            method));

    url_loader_.set_error_handler([this]() {
      // If the connection to the url loader failed, restart the request.
      // TODO(qsr): LE-77: Handle multiple failures with:
      // 1) backoff.
      // 2) notification to the user.
      Start();
    });
  }

  void HandleRedirect(network::URLResponse response) {
    // Follow the redirect if a Location header is found.
    for (const auto& header : *response.headers) {
      if (fxl::EqualsCaseInsensitiveASCII(header.name.get(), "location")) {
        ++redirect_count_;
        if (redirect_count_ >= kMaxRedirectCount) {
          callback_(NewErrorResponse(kTooManyRedirectErrorCode,
                                     "Too many redirects."));
          return;
        }

        next_url_ = header.value;
        Start();
        return;
      }
    }

    // Return an error otherwise.
    callback_(
        NewErrorResponse(kInvalidResponseErrorCode, "No Location header."));
    // |this| might be deleted withing the callback, don't reference member
    // variables afterwards.
  }

  network::URLResponse NewErrorResponse(int32_t code, std::string reason) {
    network::URLResponse response;
    response.error = network::NetworkError::New();
    response.error->code = code;
    response.error->description = reason;
    return response;
  }

  std::function<network::URLRequest()> request_factory_;
  std::function<void(network::URLResponse)> callback_;
  fxl::Closure on_empty_callback_;
  std::string next_url_;
  uint32_t redirect_count_;
  network::NetworkService* network_service_;
  network::URLLoaderPtr url_loader_;
  callback::DestructionSentinel destruction_sentinel_;
};

NetworkWrapperImpl::NetworkWrapperImpl(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    std::unique_ptr<backoff::Backoff> backoff,
    std::function<network::NetworkServicePtr()> network_service_factory)
    : backoff_(std::move(backoff)),
      network_service_factory_(std::move(network_service_factory)),
      task_runner_(std::move(task_runner)) {}

NetworkWrapperImpl::~NetworkWrapperImpl() {}

fxl::RefPtr<callback::Cancellable> NetworkWrapperImpl::Request(
    std::function<network::URLRequest()> request_factory,
    std::function<void(network::URLResponse)> callback) {
  RunningRequest& request =
      running_requests_.emplace(std::move(request_factory));

  auto cancellable =
      callback::CancellableImpl::Create([&request]() { request.Cancel(); });

  request.set_callback(cancellable->WrapCallback(TRACE_CALLBACK(
      std::move(callback), "network_wrapper", "network_request")));
  if (!in_backoff_) {
    request.SetNetworkService(GetNetworkService());
  }

  return cancellable;
}

network::NetworkService* NetworkWrapperImpl::GetNetworkService() {
  if (!network_service_) {
    network_service_ = network_service_factory_();
    network_service_.set_error_handler([this]() {
      FXL_LOG(WARNING) << "Network service crashed or not configured "
                       << "in environment, trying to reconnect.";
      FXL_DCHECK(!in_backoff_);
      in_backoff_ = true;
      for (auto& request : running_requests_) {
        request.SetNetworkService(nullptr);
      }
      network_service_.Unbind();
      task_runner_.PostDelayedTask([this] { RetryGetNetworkService(); },
                                   backoff_->GetNext());
    });
  }

  return network_service_.get();
}

void NetworkWrapperImpl::RetryGetNetworkService() {
  in_backoff_ = false;
  if (running_requests_.empty()) {
    return;
  }
  network::NetworkService* network_service = GetNetworkService();
  for (auto& request : running_requests_) {
    request.SetNetworkService(network_service);
  }
}

}  // namespace network_wrapper
