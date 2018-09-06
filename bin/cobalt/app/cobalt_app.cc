// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/cobalt_app.h"

#include "garnet/bin/cobalt/app/utils.h"
#include "garnet/bin/cobalt/utils/fuchsia_http_client.h"
#include "lib/backoff/exponential_backoff.h"
#include "third_party/cobalt/encoder/file_observation_store.h"
#include "third_party/cobalt/encoder/posix_file_system.h"

namespace cobalt {

namespace http = ::fuchsia::net::oldhttp;

using clearcut::ClearcutUploader;
using encoder::ClearcutV1ShippingManager;
using encoder::ClientSecret;
using encoder::CobaltEncoderFactoryImpl;
using encoder::FileObservationStore;
using encoder::LegacyShippingManager;
using encoder::PosixFileSystem;
using encoder::ShippingManager;
using utils::FuchsiaHTTPClient;

// Each "send attempt" is actually a cycle of potential retries. These
// two parameters configure the SendRetryer.
const std::chrono::seconds kInitialRpcDeadline(10);
const std::chrono::seconds kDeadlinePerSendAttempt(60);

const size_t kMaxBytesPerEnvelope = 512 * 1024;  // 0.5 MiB.
const size_t kMaxBytesTotal = 1024 * 1024;       // 1 MiB

constexpr char kCloudShufflerUri[] = "shuffler.cobalt-api.fuchsia.com:443";
const char kClearcutServerUri[] = "https://jmt17.google.com/log";

constexpr char kAnalyzerPublicKeyPemPath[] =
    "/pkg/data/certs/cobaltv0.1/analyzer_public.pem";
constexpr char kShufflerPublicKeyPemPath[] =
    "/pkg/data/certs/cobaltv0.1/shuffler_public.pem";

constexpr char kLegacyObservationStorePath[] =
    "/data/cobalt_legacy_observation_store";
constexpr char kV1ObservationStorePath[] = "/data/cobalt_v1_observation_store";

CobaltApp::CobaltApp(async_dispatcher_t* dispatcher,
                     std::chrono::seconds schedule_interval,
                     std::chrono::seconds min_interval,
                     const std::string& product_name)
    : system_data_(product_name),
      context_(component::StartupContext::CreateFromStartupInfo()),
      shuffler_client_(kCloudShufflerUri, true),
      send_retryer_(&shuffler_client_),
      network_wrapper_(
          dispatcher, std::make_unique<backoff::ExponentialBackoff>(),
          [this] {
            return context_->ConnectToEnvironmentService<http::HttpService>();
          }),
      encrypt_to_analyzer_(ReadPublicKeyPem(kAnalyzerPublicKeyPemPath),
                           EncryptedMessage::HYBRID_ECDH_V1),
      encrypt_to_shuffler_(ReadPublicKeyPem(kShufflerPublicKeyPemPath),
                           EncryptedMessage::HYBRID_ECDH_V1),
      timer_manager_(dispatcher),
      controller_impl_(
          new CobaltControllerImpl(dispatcher, &shipping_dispatcher_)) {
  store_dispatcher_.Register(
      ObservationMetadata::LEGACY_BACKEND,
      std::make_unique<FileObservationStore>(
          fuchsia::cobalt::MAX_BYTES_PER_OBSERVATION, kMaxBytesPerEnvelope,
          kMaxBytesTotal, std::make_unique<PosixFileSystem>(),
          kLegacyObservationStorePath));
  store_dispatcher_.Register(
      ObservationMetadata::V1_BACKEND,
      std::make_unique<FileObservationStore>(
          fuchsia::cobalt::MAX_BYTES_PER_OBSERVATION, kMaxBytesPerEnvelope,
          kMaxBytesPerEnvelope, std::make_unique<PosixFileSystem>(),
          kV1ObservationStorePath));

  auto schedule_params =
      ShippingManager::ScheduleParams(schedule_interval, min_interval);
  shipping_dispatcher_.Register(
      ObservationMetadata::LEGACY_BACKEND,
      std::make_unique<LegacyShippingManager>(
          schedule_params,
          store_dispatcher_.GetStore(ObservationMetadata::LEGACY_BACKEND)
              .ConsumeValueOrDie(),
          &encrypt_to_shuffler_,
          LegacyShippingManager::SendRetryerParams(kInitialRpcDeadline,
                                                   kDeadlinePerSendAttempt),
          &send_retryer_));
  shipping_dispatcher_.Register(
      ObservationMetadata::V1_BACKEND,
      std::make_unique<ClearcutV1ShippingManager>(
          schedule_params,
          store_dispatcher_.GetStore(ObservationMetadata::V1_BACKEND)
              .ConsumeValueOrDie(),
          &encrypt_to_shuffler_,
          std::make_unique<ClearcutUploader>(
              kClearcutServerUri, std::make_unique<FuchsiaHTTPClient>(
                                      &network_wrapper_, dispatcher))));
  shipping_dispatcher_.Start();

  factory_impl_.reset(new CobaltEncoderFactoryImpl(
      getClientSecret(), &store_dispatcher_,
      &encrypt_to_analyzer_, &shipping_dispatcher_, &system_data_,
      &timer_manager_));

  context_->outgoing().AddPublicService(
      encoder_factory_bindings_.GetHandler(factory_impl_.get()));

  context_->outgoing().AddPublicService(
      logger_factory_bindings_.GetHandler(factory_impl_.get()));

  context_->outgoing().AddPublicService(
      controller_bindings_.GetHandler(controller_impl_.get()));
}

ClientSecret CobaltApp::getClientSecret() {
  // TODO(rudominer): Generate a client secret only once, store it
  // persistently and reuse it in future instances.
  return ClientSecret::GenerateNewSecret();
}
}  // namespace cobalt
