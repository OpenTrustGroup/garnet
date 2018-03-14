// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Token manager unit tests using DEV auth provider.

#include <memory>
#include <string>

#include "garnet/bin/auth/store/auth_db.h"
#include "garnet/bin/auth/store/auth_db_file_impl.h"
#include "garnet/bin/auth/token_manager/token_manager_factory_impl.h"
#include "garnet/bin/auth/token_manager/token_manager_impl.h"
#include "garnet/lib/callback/capture.h"
#include "garnet/lib/gtest/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/auth/fidl/auth_provider.fidl.h"
#include "lib/auth/fidl/token_manager.fidl-sync.h"
#include "lib/auth/fidl/token_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/services.h"
#include "lib/test_runner/cpp/reporting/gtest_listener.h"
#include "lib/test_runner/cpp/reporting/reporter.h"

namespace e2e_dev {
namespace {

const std::string kTestUserId = "tq_auth_user_1";
const std::string kTestUserProfileId = "tq_auth_user_profile_1";
const auth::AuthProviderType kDevAuthProvider = auth::AuthProviderType::DEV;

class DevTokenManagerAppTest : public gtest::TestWithMessageLoop {
 public:
  DevTokenManagerAppTest()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  }

  ~DevTokenManagerAppTest() {}

 protected:
  // ::testing::Test:
  void SetUp() override {
    auth::Status status;
    app::Services services;
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "token_manager";
    launch_info->directory_request = services.NewRequest();
    {
      std::ostringstream stream;
      stream << "--verbose=" << fxl::GetVlogVerbosity();
      launch_info->arguments.push_back(stream.str());
    }
    application_context_->launcher()->CreateApplication(
        std::move(launch_info), app_controller_.NewRequest());
    app_controller_.set_error_handler([] {
      FXL_LOG(ERROR) << "Error in connecting to TokenManagerFactory service.";
    });

    services.ConnectToService(token_mgr_factory_.NewRequest());

    auto dev_config_ptr = auth::AuthProviderConfig::New();
    dev_config_ptr->auth_provider_type = kDevAuthProvider;
    dev_config_ptr->url = "dev_auth_provider";
    auth_provider_configs_.push_back(std::move(dev_config_ptr));

    token_mgr_factory_->GetTokenManager(kTestUserId,
                                        std::move(auth_provider_configs_),
                                        token_mgr_.NewRequest());

    // Make sure the state is clean
    // TODO: Once namespace for file system is per user, this won't be needed
    token_mgr_->DeleteAllTokens(kDevAuthProvider, kTestUserProfileId,
                                callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    ASSERT_EQ(auth::Status::OK, status);
  }

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  app::ApplicationControllerPtr app_controller_;
  f1dl::Array<auth::AuthProviderConfigPtr> auth_provider_configs_;

 protected:
  auth::TokenManagerPtr token_mgr_;
  auth::TokenManagerFactoryPtr token_mgr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevTokenManagerAppTest);
};

TEST_F(DevTokenManagerAppTest, Authorize) {
  auth::AuthenticationUIContextPtr auth_ui_context;
  auth::Status status;
  auth::UserProfileInfoPtr user_info;

  token_mgr_->Authorize(kDevAuthProvider, std::move(auth_ui_context),
                        callback::Capture(MakeQuitTask(), &status, &user_info));

  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  // TODO(ukode): Validate user_info contents
}

TEST_F(DevTokenManagerAppTest, GetAccessToken) {
  auto scopes = f1dl::Array<f1dl::String>::New(0);
  auth::Status status;
  f1dl::String access_token;

  token_mgr_->GetAccessToken(
      kDevAuthProvider, kTestUserProfileId, "", std::move(scopes),
      callback::Capture(MakeQuitTask(), &status, &access_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(access_token.get().find(":at_") != std::string::npos);
}

TEST_F(DevTokenManagerAppTest, GetIdToken) {
  auth::Status status;
  f1dl::String id_token;

  token_mgr_->GetIdToken(kDevAuthProvider, kTestUserProfileId, "",
                         callback::Capture(MakeQuitTask(), &status, &id_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(id_token.get().find(":idt_") != std::string::npos);
}

TEST_F(DevTokenManagerAppTest, GetFirebaseToken) {
  auth::Status status;
  auth::FirebaseTokenPtr firebase_token;

  token_mgr_->GetFirebaseToken(
      kDevAuthProvider, kTestUserProfileId, "firebase_test_api_key", "",
      callback::Capture(MakeQuitTask(), &status, &firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  if (!firebase_token.is_null()) {
    EXPECT_TRUE(firebase_token->id_token.get().find(":fbt_") !=
                std::string::npos);
    EXPECT_TRUE(firebase_token->email.get().find("@devauthprovider.com") !=
                std::string::npos);
    EXPECT_TRUE(firebase_token->local_id.get().find("local_id_") !=
                std::string::npos);
  }
}

TEST_F(DevTokenManagerAppTest, GetCachedFirebaseToken) {
  auth::Status status;
  auth::FirebaseTokenPtr firebase_token;
  auth::FirebaseTokenPtr other_firebase_token;
  auth::FirebaseTokenPtr cached_firebase_token;

  token_mgr_->GetFirebaseToken(
      kDevAuthProvider, kTestUserProfileId, "", "key1",
      callback::Capture(MakeQuitTask(), &status, &firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  token_mgr_->GetFirebaseToken(
      kDevAuthProvider, kTestUserProfileId, "", "key2",
      callback::Capture(MakeQuitTask(), &status, &other_firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  token_mgr_->GetFirebaseToken(
      kDevAuthProvider, kTestUserProfileId, "", "key1",
      callback::Capture(MakeQuitTask(), &status, &cached_firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  ASSERT_NE(firebase_token->id_token, other_firebase_token->id_token);
  ASSERT_EQ(firebase_token->id_token, cached_firebase_token->id_token);
  ASSERT_EQ(firebase_token->email, cached_firebase_token->email);
  ASSERT_EQ(firebase_token->local_id, cached_firebase_token->local_id);
}

TEST_F(DevTokenManagerAppTest, EraseAllTokens) {
  auto scopes = f1dl::Array<f1dl::String>::New(0);
  auth::Status status;

  f1dl::String old_id_token;
  f1dl::String old_access_token;
  f1dl::String new_id_token;
  f1dl::String new_access_token;
  auth::FirebaseTokenPtr old_firebase_token;
  auth::FirebaseTokenPtr new_firebase_token;

  token_mgr_->GetIdToken(
      kDevAuthProvider, kTestUserProfileId, "",
      callback::Capture(MakeQuitTask(), &status, &old_id_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  token_mgr_->GetAccessToken(
      kDevAuthProvider, kTestUserProfileId, "", std::move(scopes),
      callback::Capture(MakeQuitTask(), &status, &old_access_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  token_mgr_->GetFirebaseToken(
      kDevAuthProvider, kTestUserProfileId, "", "",
      callback::Capture(MakeQuitTask(), &status, &old_firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  token_mgr_->DeleteAllTokens(kDevAuthProvider, kTestUserProfileId,
                              callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  scopes = f1dl::Array<f1dl::String>::New(0);
  token_mgr_->GetIdToken(
      kDevAuthProvider, kTestUserProfileId, "",
      callback::Capture(MakeQuitTask(), &status, &new_id_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  token_mgr_->GetAccessToken(
      kDevAuthProvider, kTestUserProfileId, "", std::move(scopes),
      callback::Capture(MakeQuitTask(), &status, &new_access_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  token_mgr_->GetFirebaseToken(
      kDevAuthProvider, kTestUserProfileId, "", "",
      callback::Capture(MakeQuitTask(), &status, &new_firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  ASSERT_NE(old_id_token, new_id_token);
  ASSERT_NE(old_access_token, new_access_token);
  ASSERT_NE(old_firebase_token->id_token, new_firebase_token->id_token);
}

TEST_F(DevTokenManagerAppTest, GetIdTokenFromCache) {
  auth::Status status;
  f1dl::String id_token;
  f1dl::String cached_id_token;

  token_mgr_->GetIdToken(kDevAuthProvider, kTestUserProfileId, "",
                         callback::Capture(MakeQuitTask(), &status, &id_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  token_mgr_->GetIdToken(
      kDevAuthProvider, kTestUserProfileId, "",
      callback::Capture(MakeQuitTask(), &status, &cached_id_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(id_token.get().find(":idt_") != std::string::npos);
  ASSERT_EQ(id_token.get(), cached_id_token.get());

  token_mgr_->DeleteAllTokens(kDevAuthProvider, kTestUserProfileId,
                              callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  token_mgr_->GetIdToken(
      kDevAuthProvider, kTestUserProfileId, "",
      callback::Capture(MakeQuitTask(), &status, &cached_id_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(id_token.get().find(":idt_") != std::string::npos);
  ASSERT_NE(id_token.get(), cached_id_token.get());
}

TEST_F(DevTokenManagerAppTest, GetAccessTokenFromCache) {
  auto scopes = f1dl::Array<f1dl::String>::New(0);
  auth::Status status;
  f1dl::String id_token;
  f1dl::String access_token;
  f1dl::String cached_access_token;

  token_mgr_->GetAccessToken(
      kDevAuthProvider, kTestUserProfileId, "", std::move(scopes),
      callback::Capture(MakeQuitTask(), &status, &access_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  token_mgr_->GetIdToken(kDevAuthProvider, kTestUserProfileId, "",
                         callback::Capture(MakeQuitTask(), &status, &id_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  scopes = f1dl::Array<f1dl::String>::New(0);
  token_mgr_->GetAccessToken(
      kDevAuthProvider, kTestUserProfileId, "", std::move(scopes),
      callback::Capture(MakeQuitTask(), &status, &cached_access_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  EXPECT_TRUE(access_token.get().find(":at_") != std::string::npos);
  ASSERT_EQ(access_token.get(), cached_access_token.get());
}

TEST_F(DevTokenManagerAppTest, GetAndRevokeCredential) {
  auth::AuthenticationUIContextPtr auth_ui_context;
  auth::Status status;
  auth::UserProfileInfoPtr user_info;

  std::string credential;
  f1dl::String token;
  auto scopes = f1dl::Array<f1dl::String>::New(0);

  token_mgr_->Authorize(kDevAuthProvider, std::move(auth_ui_context),
                        callback::Capture(MakeQuitTask(), &status, &user_info));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  f1dl::String user_profile_id = user_info->id;

  // Obtain the stored credential
  auth::store::AuthDbFileImpl auth_db(auth::kAuthDbPath + kTestUserId +
                                      auth::kAuthDbPostfix);
  auto db_status = auth_db.Load();
  EXPECT_EQ(db_status, auth::store::Status::kOK);
  db_status = auth_db.GetRefreshToken(
      auth::store::CredentialIdentifier(user_profile_id,
                                        auth::store::IdentityProvider::TEST),
      &credential);
  EXPECT_EQ(db_status, auth::store::Status::kOK);

  EXPECT_TRUE(credential.find("rt_") != std::string::npos);

  token_mgr_->GetIdToken(kDevAuthProvider, user_profile_id, "",
                         callback::Capture(MakeQuitTask(), &status, &token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(token.get().find(credential) != std::string::npos);

  token_mgr_->GetAccessToken(
      kDevAuthProvider, user_profile_id, "", std::move(scopes),
      callback::Capture(MakeQuitTask(), &status, &token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(token.get().find(credential) != std::string::npos);

  token_mgr_->DeleteAllTokens(kDevAuthProvider, user_profile_id,
                              callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);

  // The credential should now be revoked
  token_mgr_->GetIdToken(kDevAuthProvider, user_profile_id, "",
                         callback::Capture(MakeQuitTask(), &status, &token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(token.get().find(credential) == std::string::npos);

  token_mgr_->GetAccessToken(
      kDevAuthProvider, user_profile_id, "", std::move(scopes),
      callback::Capture(MakeQuitTask(), &status, &token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(token.get().find(credential) == std::string::npos);
}

}  // namespace
}  // namespace e2e_dev

int main(int argc, char** argv) {
  test_runner::GTestListener listener(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  {
    fsl::MessageLoop message_loop;
    auto context = app::ApplicationContext::CreateFromStartupInfoNotChecked();
    test_runner::ReportResult(argv[0], context.get(), listener.GetResults());
  }

  return status;
}
