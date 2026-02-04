// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <nbl_fwk/log.h>
#include <nbl_gui/nbl_gui.h>

#define LOG_TAG "tui_sample_payment"
#define TIMEOUT 30

using namespace nbl_gui;

// clang-format off
const char* kKeymap[] = {"1", "2", "3", "\n",
                         "4", "5", "6", "\n",
                         "7", "8", "9", "\n",
                         kSymbolOk, "0",  kSymbolErase, ""};
// clang-format on

std::string quit_message("确认退出？");
std::string pay_message("确认要支付？");

void showDialog(std::string message) {
  auto dialog = FindViewById<Dialog>("dialog");
  auto dialogTextView = FindViewById<TextView>("dialog-textview");
  dialogTextView->SetText(message);
  dialogTextView->Refresh();
  dialog->Show();
}

int main(int argc, char** argv) {
  Engine engine;
  NBL_LOGI("TUI sample payment\n");

  auto ret = engine.Init();
  if (ret != NBL_NO_ERROR) {
    NBL_LOGE("Failed to initialize nbl_gui engine.");
    return EXIT_FAILURE;
  }

  ret = engine.LoadFallbackFont("/pkg/data/DroidSansFallbackFull.ttf");
  if (ret != NBL_NO_ERROR) {
    NBL_LOGE("Failed to load ttf font file.");
    return EXIT_FAILURE;
  }

  ret = engine.LoadLayoutFile("/pkg/data/layout.json");
  if (ret != NBL_NO_ERROR) {
    NBL_LOGE("Failed to read layout file.");
    return EXIT_FAILURE;
  }

  auto input_field = FindViewById<InputField>("password");
  auto keypad = FindViewById<Keyboard>("keypad");
  if (keypad) {
    keypad->SetKeyMap(kKeymap);
    if (input_field) {
      input_field->SetOnSubmitListener([](std::string input) {
        NBL_LOGI("You entered: %s\n", input.c_str());
      });
      input_field->SetInputSource(keypad);
    }
  }

  auto dialog = FindViewById<Dialog>("dialog");
  auto dialogTextView = FindViewById<TextView>("dialog-textview");
  auto dialogButtonCancel = FindViewById<Button>("dialog-btn-cancel");
  if (dialogButtonCancel) {
    dialogButtonCancel->SetOnClickListener([dialog] {
      NBL_LOGI("Dialog cancel button is clicked.\n");
      dialog->Hide();
    });
  }
  auto dialogButtonConfirm = FindViewById<Button>("dialog-btn-confirm");
  if (dialogButtonConfirm) {
    dialogButtonConfirm->SetOnClickListener([&engine, dialogTextView,
                                             input_field] {
      NBL_LOGI("Dialog confirm button is clicked.\n");
      if (dialogTextView->GetText().compare(pay_message) == 0) {
        NBL_LOGI("You entered password: %s\n", input_field->GetText().c_str());
      }
      engine.Quit();
    });
  }

  auto btnback = FindViewById<ImageButton>("buttonback");
  if (btnback) {
    btnback->SetOnClickListener([] {
      NBL_LOGI("Back button is clicked.\n");
      showDialog(quit_message);
    });
  }

  auto btn1 = FindViewById<Button>("btn1");
  if (btn1) {
    btn1->SetOnClickListener([] {
      NBL_LOGI("btn1 pressed.\n");
      showDialog(quit_message);
    });
  }

  auto btn2 = FindViewById<Button>("btn2");
  if (btn2) {
    btn2->SetOnClickListener([] {
      NBL_LOGI("btn2 pressed.\n");
      showDialog(pay_message);
    });
  }

  engine.StartIdleTimer(TIMEOUT, [&engine] {
    NBL_LOGI("TUI payment timeout.\n");
    engine.Quit();
  });

  engine.set_error_handler([&engine](Engine::Error err) {
    switch (err) {
      case Engine::Error::EXIT_ON_NEBULA_CLOSE:
      case Engine::Error::EXIT_ON_BACK_PRESSED:
      case Engine::Error::EXIT_ON_HOME_PRESSED:
      case Engine::Error::EXIT_ON_RECENT_PRESSED:
      case Engine::Error::EXIT_ON_SHUTDOWN:
      case Engine::Error::EXIT_ON_CALL_STATE_RINGING:
      case Engine::Error::EXIT_ON_PWRKEY_PRESSED:
        NBL_LOGI("TUI closed by supported err %d\n", err);
        break;
      default:
        NBL_LOGE("TUI closed by unsupported err %d\n", static_cast<int>(err));
        break;
    }
    engine.Quit();
  });

  engine.Loop();

  return EXIT_SUCCESS;
}
