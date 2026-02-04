// Copyright 2019 GoldenRiver Technology Co., Ltd.

#include <nbl_fwk/log.h>
#include <nbl_gui/nbl_gui.h>

#define LOG_TAG "tui_test_privilege"
#define TIMEOUT 5

using namespace nbl_gui;

/**
 * If failed, log message must include "TUI test failed"
 * If success, include "TUI test success"
 */

int main(int argc, char** argv) {
  Engine engine;
  int ret;
  NBL_LOGI("TUI test start.\n");

  ret = engine.Init();
  if (ret != NBL_NO_ERROR) {
    NBL_LOGE("TUI test failed: %d, Failed to initialize nbl_gui engine.\n", ret);
    return EXIT_FAILURE;
  }

  ret = engine.LoadLayoutFile("/pkg/assets/layout.json");
  if (ret != NBL_NO_ERROR) {
    NBL_LOGE("TUI test failed: %d, Failed to read layout file.\n", ret);
    return EXIT_FAILURE;
  }

  engine.StartIdleTimer(TIMEOUT, [&engine] {
    NBL_LOGI("TUI test success.\n");
    engine.Quit();
  });

  engine.Loop();

  NBL_LOGI("TUI test: application ends.\n");

  return EXIT_SUCCESS;
}
