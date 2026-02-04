// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <nbl_fwk/log.h>
#include <nbl_gui/color.h>
#include <nbl_gui/nbl_gui.h>
#include <nbl_qrcode/qrcode.h>
#include <time.h>

#define LOG_TAG "tui_sample_qrcode"

#define STR_LEN 10

#define TIMER_MAX 10

#define TIMEOUT 30

using namespace nbl_gui;

static constexpr uint8_t kVersion = 6;
static constexpr uint8_t kEccMode = ECC_HIGH;  // ECC_LOW ECC_HIGH
static constexpr uint8_t kPixelsPerModule = 10;
static int timer_count = TIMER_MAX;

void DrawModule(QRCode qrcode, Color* buffer, Color color) {
  size_t pixels_per_line = qrcode.size * kPixelsPerModule;

  for (uint8_t y = 0; y < kPixelsPerModule; y++) {
    for (uint8_t x = 0; x < kPixelsPerModule; x++) {
      buffer[x + y * pixels_per_line] = color;
    }
  }
}

int UpdateQRCode(const char* data) {
  printf("Update QRCode.\n");
  QRCode qrcode;
  uint8_t bytes[qrcode_getBufferSize(kVersion)];

  // find qrcode widget
  auto qrcode_image = FindViewById<Container>("payqrcode");
  if (!qrcode_image) {
    NBL_LOGE("Failed to get qrcode widget.");
    return EXIT_FAILURE;
  }
  // init qrcode data
  if (qrcode_initText(&qrcode, bytes, kVersion, kEccMode, data) != 0) {
    NBL_LOGE("Failed to init qrcode.");
    return EXIT_FAILURE;
  }

  size_t pixels_per_line = qrcode.size * kPixelsPerModule;
  size_t buffer_size = pixels_per_line * pixels_per_line;
  auto img_buffer = std::unique_ptr<Color[]>(new Color[buffer_size]);
  if (!img_buffer) {
    NBL_LOGE("TUI qrcode out of memoru.");
    return EXIT_FAILURE;
  }

  memset(img_buffer.get(), 0xff, buffer_size * sizeof(Color));
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      auto p = &img_buffer[x * kPixelsPerModule +
                           y * pixels_per_line * kPixelsPerModule];
      struct Color black_color;
      black_color.full = 0xFF000000;
      if (qrcode_getModule(&qrcode, x, y)) {
        DrawModule(qrcode, p, black_color);
      }
    }
  }

  int result = qrcode_image->SetBackgroundImage(
      (uint8_t*)img_buffer.get(), buffer_size * sizeof(Color), pixels_per_line,
      pixels_per_line);
  if (img_buffer) {
    img_buffer.reset();
    img_buffer = nullptr;
  }
  if (result != NBL_NO_ERROR) {
    NBL_LOGE("Failed to set qrcode bitmap.");
    return EXIT_FAILURE;
  }
  return NBL_NO_ERROR;
}

void UpdateQRCodeRandom() {
  char str[STR_LEN + 1] = {0};
  int i;

  srand(time(NULL));
  for (i = 0; i < STR_LEN; i++) {
    str[i] = rand() % ('z' - 'a' + 1) + 'a';
  }
  int result = UpdateQRCode(str);
  if (result != NBL_NO_ERROR) {
    NBL_LOGE("Failed to update qrcode.");
    exit(-1);
  }
}

void UpdateTimerTask(Task* task) {
  // Get param by task->GetParam();
  auto dialog = FindViewById<Dialog>("dialog");
  if (dialog && dialog->IsHidden()) {
    auto label_timer = FindViewById<TextView>("timer");
    timer_count -= 1;
    if (timer_count <= 0) {
      timer_count = 0;
      UpdateQRCodeRandom();
      timer_count = TIMER_MAX;
    }
    std::string timer_string = std::to_string(timer_count);
    if (timer_string.size() < 2)
      timer_string = "0" + timer_string;
    label_timer->SetText(timer_string);
    label_timer->Refresh();
  }
}

int main(int argc, char** argv) {
  Engine engine;
  timer_count = TIMER_MAX;

  printf("TUI sample QRcode\n");

  auto ret = engine.Init();
  if (ret != NBL_NO_ERROR) {
    NBL_LOGE("Failed to initialize nbl_gui engine.");
    return EXIT_FAILURE;
  }

  ret = engine.LoadFallbackFont("/pkg/data/DroidSansFallbackFull.ttf");
  if (ret != NBL_NO_ERROR) {
    NBL_LOGE("Failed to load ttf font file");
    return EXIT_FAILURE;
  }

  ret = engine.LoadLayoutFile("/pkg/data/layout.json");
  if (ret != NBL_NO_ERROR) {
    NBL_LOGE("Failed to read layout file");
    return EXIT_FAILURE;
  }

  auto dialog = FindViewById<Dialog>("dialog");
  auto dialogButtonCancel = FindViewById<Button>("dialog-btn-cancel");
  if (dialogButtonCancel) {
    dialogButtonCancel->SetOnClickListener([dialog] {
      NBL_LOGI("Dialog cancel button is clicked.\n");
      dialog->Hide();
    });
  }
  auto dialogButtonConfirm = FindViewById<Button>("dialog-btn-confirm");
  if (dialogButtonConfirm) {
    dialogButtonConfirm->SetOnClickListener([&engine] {
      NBL_LOGI("Dialog confirm button is clicked.\n");
      engine.Quit();
    });
  }

  auto btnback = FindViewById<ImageButton>("buttonback");
  if (btnback) {
    btnback->SetOnClickListener([dialog] {
      NBL_LOGI("Back button is clicked.\n");
      dialog->Show();
    });
  }

  int result = UpdateQRCode("1234567890");
  if (result != NBL_NO_ERROR) {
    NBL_LOGE("Failed to set qrcode.");
    return EXIT_FAILURE;
  }

  engine.StartIdleTimer(TIMEOUT, [&engine] {
    NBL_LOGI("TUI qrcode timeout.\n");
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

  // Update timer per seconds.
  Task* task = new Task(&UpdateTimerTask, 1000, NULL);
  if (task == NULL) {
    NBL_LOGE("TUI failed to new task.");
    return EXIT_FAILURE;
  }

  engine.Loop();

  if (task != NULL) {
    delete task;
    task = NULL;
  }

  return EXIT_SUCCESS;
}
