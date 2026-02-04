// Copyright 2019 GoldenRiver Technology Co., Ltd. All rights reserved.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void nbl_tui_init();

void nbl_tui_load_font(const char* path);

void nbl_tui_find_view(const char* view_id);

void nbl_tui_keyboard(const char* param);

void nbl_tui_dialog(const char* param);

void nbl_tui_textview(const char* param);

void nbl_tui_loop();

void nbl_tui_container(void* bitmap,
                       size_t bitmap_size,
                       int bitmap_width,
                       int bitmap_height);

void nbl_tui_input_field_set_text(const char *text);

#ifdef __cplusplus
}
#endif
