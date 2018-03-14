// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/auth/token_manager/test/dev_auth_provider_impl.h"

namespace {
std::string GenerateRandomString() {
  uint32_t random_number;
  size_t random_size;
  zx_status_t status =
      zx_cprng_draw(&random_number, sizeof random_number, &random_size);
  FXL_CHECK(status == ZX_OK);
  FXL_CHECK(sizeof random_number == random_size);
  return std::to_string(random_number);
}

}  // namespace

namespace auth {
namespace dev_auth_provider {

using auth::dev_auth_provider::DevAuthProviderImpl;

DevAuthProviderImpl::DevAuthProviderImpl() {}

DevAuthProviderImpl::~DevAuthProviderImpl() {}

void DevAuthProviderImpl::GetPersistentCredential(
    f1dl::InterfaceHandle<auth::AuthenticationUIContext> auth_ui_context,
    const GetPersistentCredentialCallback& callback) {
  auto profile = auth::UserProfileInfo::New();
  profile->id = GenerateRandomString() + "@example.com";

  callback(AuthProviderStatus::OK, "rt_" + GenerateRandomString(),
           std::move(profile));
  return;
}

void DevAuthProviderImpl::GetAppAccessToken(
    const f1dl::String& credential,
    const f1dl::String& app_client_id,
    const f1dl::Array<f1dl::String> app_scopes,
    const GetAppAccessTokenCallback& callback) {
  AuthTokenPtr access_token = auth::AuthToken::New();
  access_token->token =
      std::string(credential) + ":at_" + GenerateRandomString();
  access_token->token_type = TokenType::ACCESS_TOKEN;
  access_token->expires_in = 3600;

  callback(AuthProviderStatus::OK, std::move(access_token));
}

void DevAuthProviderImpl::GetAppIdToken(const f1dl::String& credential,
                                        const f1dl::String& audience,
                                        const GetAppIdTokenCallback& callback) {
  AuthTokenPtr id_token = auth::AuthToken::New();
  id_token->token = std::string(credential) + ":idt_" + GenerateRandomString();
  id_token->token_type = TokenType::ID_TOKEN;
  id_token->expires_in = 3600;

  callback(AuthProviderStatus::OK, std::move(id_token));
}

void DevAuthProviderImpl::GetAppFirebaseToken(
    const f1dl::String& id_token,
    const f1dl::String& firebase_api_key,
    const GetAppFirebaseTokenCallback& callback) {
  FirebaseTokenPtr fb_token = auth::FirebaseToken::New();
  fb_token->id_token =
      std::string(firebase_api_key) + ":fbt_" + GenerateRandomString();
  fb_token->email = GenerateRandomString() + "@devauthprovider.com";
  fb_token->local_id = "local_id_" + GenerateRandomString();
  fb_token->expires_in = 3600;

  callback(AuthProviderStatus::OK, std::move(fb_token));
}

void DevAuthProviderImpl::RevokeAppOrPersistentCredential(
    const f1dl::String& credential,
    const RevokeAppOrPersistentCredentialCallback& callback) {
  callback(AuthProviderStatus::OK);
}

}  // namespace dev_auth_provider
}  // namespace auth
