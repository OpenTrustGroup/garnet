// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tui_test.h"
#include <nbl_gui/color.h>
#include <nbl_gui/nbl_gui.h>
#include <unistd.h>
using namespace nbl_gui;

const char *kKeymap[] = {"1",       "2",  "3",          "\n", "4", "5",
                         "6",       "\n", "7",          "8",  "9", "\n",
                         kSymbolOk, "0",  kSymbolErase, ""};

const char *kKeymap2[] = {"123"};

void nbl_tui_init() {
  Engine *engine = new Engine();
  printf("nbl_tui_init start\n");
  auto ret = engine->Init();
  if (ret != NBL_NO_ERROR) {
    printf("Failed to initialize nbl_gui engine.\n");
    printf("nbl_tui_init finish\n");
    return;
  }
  printf("engine.Quit\n");
  engine->Quit();
  delete engine;
  sleep(3);
  printf("nbl_tui_init finish\n");
}

void nbl_tui_loop() {
  nbl_status_t ret;
  Engine *engine;

  printf("nbl_tui_loop start1\n");
  engine = new Engine();
  engine->Quit();
  delete engine;
  engine = NULL;
  sleep(5);

  engine = new Engine();
  printf("nbl_tui_loop start2\n");
  engine->StartIdleTimer(5, [engine] {
    printf("engine.Quit\n");
    engine->Quit();
  });
  engine->Loop();
  delete engine;
  engine = NULL;
  sleep(5);

  printf("nbl_tui_loop start3\n");
  engine = new Engine();
  engine->StartIdleTimer(5, [engine] {
    printf("engine.Quit\n");
    engine->Quit();
  });
  ret = engine->LoadLayoutFile("/pkg/assets/layout.json");
  engine->Loop();
  delete engine;
  engine = NULL;
  sleep(5);

  printf("nbl_tui_loop start4\n");
  engine = new Engine();
  ret = engine->Init();
  if (ret != NBL_NO_ERROR) {
    printf("Failed to initialize nbl_gui engine.\n");
    return;
  }
  engine->StartIdleTimer(5, [engine] {
    printf("engine.Quit\n");
    engine->Quit();
  });
  ret = engine->LoadLayoutFile("/pkg/assets/layout.json");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to read layout file.\n");
    return;
  }
  engine->Loop();
  delete engine;
  engine = NULL;
  sleep(5);

  printf("nbl_tui_loop finish\n");
}

void nbl_tui_load_font(const char *path) {
  Engine *engine = new Engine();
  printf("nbl_tui_load_font start\n");
  auto ret = engine->Init();
  if (ret != NBL_NO_ERROR) {
    printf("Failed to initialize nbl_gui engine.\n");
  }
  if (path != NULL) {
    std::string path_str(path);
    ret = engine->LoadFallbackFont(path_str);
  } else {
    ret = engine->LoadFallbackFont("");
  }
  ret = engine->LoadFallbackFont("/pkg/assets/test.json");
  printf("engine.Quit\n");
  engine->Quit();
  delete engine;
  sleep(3);
  printf("nbl_tui_load_font finish\n");
}

void nbl_tui_load_layout(const char *path) {
  Engine *engine = new Engine();
  printf("nbl_tui_load_layout start\n");
  auto ret = engine->Init();
  if (ret != NBL_NO_ERROR) {
    printf("Failed to initialize nbl_gui engine.\n");
  }
  if (path != NULL) {
    std::string path_str(path);
    ret = engine->LoadLayoutFile(path_str);
  } else {
    ret = engine->LoadLayoutFile("");
  }
  ret = engine->LoadLayoutFile("/pkg/assets/test.json");
  printf("engine.Quit\n");
  engine->Quit();
  delete engine;
  sleep(3);
  printf("nbl_tui_load_layout finish\n");
}

void showDialog(std::string message) {
  printf("show dialog\n");
  auto dialog = FindViewById<Dialog>("dialog");
  auto dialogTextView = FindViewById<TextView>("dialog-textview");
  dialogTextView->SetText(message);
  dialogTextView->Refresh();
  dialog->Show();
}

void nbl_tui_find_view(const char *view_id) {
  Engine *engine = new Engine();
  std::string view_id_str = "";
  printf("nbl_tui_find_view start\n");

  if (view_id != NULL) {
    view_id_str = view_id;
  }

  auto ret = engine->Init();
  if (ret != NBL_NO_ERROR) {
    printf("Failed to initialize nbl_gui engine.\n");
    return;
  }
  ret = engine->LoadFallbackFont("/pkg/assets/DroidSansFallbackFull.ttf");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to load ttf font file.");
    return;
  }

  ret = engine->LoadLayoutFile("/pkg/assets/layout.json");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to read layout file.");
    return;
  }

  FindViewById<InputField>(view_id_str);
  FindViewById<Keyboard>(view_id_str);
  FindViewById<Dialog>(view_id_str);
  FindViewById<ImageButton>(view_id_str);
  FindViewById<TextView>(view_id_str);
  FindViewById<Button>(view_id_str);

  FindViewById<InputField>("password");
  FindViewById<Keyboard>("keypad");
  FindViewById<Dialog>("dialog");
  FindViewById<ImageButton>("buttonback");
  FindViewById<TextView>("dialog-textview");
  FindViewById<Button>("dialog-btn-cancel");
  FindViewById<Button>("dialog-btn-confirm");
  FindViewById<Button>("btn1");
  FindViewById<Button>("btn2");

  engine->StartIdleTimer(5, [engine] {
    printf("engine.Quit\n");
    engine->Quit();
  });

  engine->Loop();
  delete engine;
  sleep(3);
  printf("nbl_tui_find_view finish\n");
}

void nbl_tui_keyboard(const char *param) {
  Engine *engine = new Engine();
  printf("nbl_tui_keyboard start\n");
  auto ret = engine->Init();
  if (ret != NBL_NO_ERROR) {
    printf("Failed to initialize nbl_gui engine.\n");
    return;
  }
  ret = engine->LoadFallbackFont("/pkg/assets/DroidSansFallbackFull.ttf");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to load ttf font file.");
    return;
  }

  ret = engine->LoadLayoutFile("/pkg/assets/layout.json");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to read layout file.");
    return;
  }

  auto input_field = FindViewById<InputField>("password");
  auto keypad = FindViewById<Keyboard>("keypad");
  if (keypad) {
    if (param) {
      keypad->SetKeyMap(&param);
    } else {
      keypad->SetKeyMap(NULL);
    }
    if (input_field) {
      input_field->SetOnSubmitListener([](std::string) {});
      input_field->SetInputSource(keypad);
    }
  }

  engine->StartIdleTimer(5, [engine] {
    printf("engine.Quit\n");
    engine->Quit();
  });

  engine->Loop();
  delete engine;
  sleep(3);
  printf("nbl_tui_keyboard finish\n");
}

void nbl_tui_dialog(const char *param) {
  Engine *engine = new Engine();
  std::string param_str = "";
  int count = 0;
  printf("nbl_tui_dialog start\n");
  if (param != NULL) {
    param_str = param;
  }
  auto ret = engine->Init();
  if (ret != NBL_NO_ERROR) {
    printf("Failed to initialize nbl_gui engine.\n");
    return;
  }
  ret = engine->LoadFallbackFont("/pkg/assets/DroidSansFallbackFull.ttf");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to load ttf font file.");
    return;
  }

  ret = engine->LoadLayoutFile("/pkg/assets/layout.json");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to read layout file.");
    return;
  }
  engine->StartIdleTimer(5, [engine, &param_str, &count] {
    count += 1;
    if (count == 1) {
      showDialog(param_str);
    } else {
      printf("engine.Quit\n");
      engine->Quit();
    }
  });
  engine->Loop();
  delete engine;
  printf("loop finish\n");
  sleep(3);
  printf("nbl_tui_dialog finish\n");
}

void nbl_tui_textview(const char *param) {
  Engine *engine = new Engine();
  std::string param_str = "";
  int count = 0;
  printf("nbl_tui_textview start\n");
  if (param != NULL) {
    param_str = param;
  }
  auto ret = engine->Init();
  if (ret != NBL_NO_ERROR) {
    printf("Failed to initialize nbl_gui engine.\n");
    return;
  }
  ret = engine->LoadFallbackFont("/pkg/assets/DroidSansFallbackFull.ttf");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to load ttf font file.");
    return;
  }

  ret = engine->LoadLayoutFile("/pkg/assets/layout.json");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to read layout file.");
    return;
  }
  engine->StartIdleTimer(5, [engine, &param_str, &count] {
    count += 1;
    if (count == 1) {
      auto textView = FindViewById<TextView>("note");
      textView->SetText(param_str);
      textView->Refresh();
    } else {
      printf("engine.Quit\n");
      engine->Quit();
    }
  });
  engine->Loop();
  delete engine;
  printf("loop finish\n");
  sleep(3);
  printf("nbl_tui_textview finish\n");
}

void nbl_tui_container(void *bitmap, size_t bitmap_size, int bitmap_width,
                       int bitmap_height) {
  Engine *engine = new Engine();
  int count = 0;
  printf("nbl_tui_container start\n");
  auto ret = engine->Init();
  if (ret != NBL_NO_ERROR) {
    printf("Failed to initialize nbl_gui engine.\n");
    return;
  }
  ret = engine->LoadFallbackFont("/pkg/assets/DroidSansFallbackFull.ttf");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to load ttf font file.");
    return;
  }

  ret = engine->LoadLayoutFile("/pkg/assets/layout.json");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to read layout file.");
    return;
  }
  engine->StartIdleTimer(5, [engine, &count, bitmap, &bitmap_size,
                             &bitmap_width, &bitmap_height] {
    (void)bitmap_height;
    count += 1;
    if (count == 1) {
      Container *container = FindViewById<Container>("logo");
      if (container) {
        printf("SetBackgroundImage%zu.\n", bitmap_size);
        int height = bitmap_size / sizeof(Color) / bitmap_width;
        container->SetBackgroundImage((uint8_t *)bitmap,
                                      bitmap_width * height * sizeof(Color),
                                      (uint32_t)bitmap_width, (uint32_t)height);
      } else {
        printf("Failed to get test widget container.\n");
      }
    } else {
      printf("engine.Quit\n");
      engine->Quit();
    }
  });
  engine->Loop();
  delete engine;
  printf("loop finish\n");
  sleep(3);
  printf("nbl_tui_container finish\n");
}

void nbl_tui_input_field_set_text(const char *text) {
  Engine *engine = new Engine();
  std::string input_text = "";
  printf("nbl_tui_input_field_set_text start\n");
  if (text != NULL) {
    input_text = text;
  }

  auto ret = engine->Init();
  if (ret != NBL_NO_ERROR) {
    printf("Failed to initialize nbl_gui engine.\n");
    return;
  }
  ret = engine->LoadFallbackFont("/pkg/assets/DroidSansFallbackFull.ttf");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to load ttf font file.");
    return;
  }

  ret = engine->LoadLayoutFile("/pkg/assets/layout.json");
  if (ret != NBL_NO_ERROR) {
    printf("Failed to read layout file.");
    return;
  }

  auto input_field = FindViewById<InputField>("password");

  engine->StartIdleTimer(5, [engine,input_field,input_text] {
    input_field->SetText(input_text);
    printf("engine.Quit\n");
    engine->Quit();
  });

  engine->Loop();
  delete engine;
  sleep(3);
  printf("nbl_tui_input_field_set_text finish\n");
}
