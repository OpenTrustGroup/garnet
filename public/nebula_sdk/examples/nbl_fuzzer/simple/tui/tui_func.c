// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>
#include <stdlib.h>
#include <tui_test.h>

struct function_description tui_init_test = {
    .funcname = "nbl_tui_init", .funcaddr = nbl_tui_init, .argnum = 0};

PrepareTest(nbl_tui_init) { return &tui_init_test; }

struct function_description tui_loop_test = {.funcname = "nbl_tui_loop",
                                             .funcaddr = nbl_tui_loop,
                                             .argnum = 0,
                                             .time_out = 60000};

PrepareTest(nbl_tui_loop) { return &tui_loop_test; }

struct function_description tui_load_font_test = {
    .funcname = "nbl_tui_load_font",
    .funcaddr = nbl_tui_load_font,
    .argnum = 1,
    .argname = {"path"},
    .argtype = {ARG_TYPE_STRING},
    .argmax = {64},
};

PrepareTest(nbl_tui_load_font) { return &tui_load_font_test; }

struct function_description tui_find_view_test = {.funcname =
                                                      "nbl_tui_find_view",
                                                  .funcaddr = nbl_tui_find_view,
                                                  .argnum = 1,
                                                  .argname = {"view_id"},
                                                  .argtype = {ARG_TYPE_STRING},
                                                  .argmax = {64},
                                                  .time_out = 60000};

PrepareTest(nbl_tui_find_view) { return &tui_find_view_test; }

struct function_description tui_keyboard_test = {.funcname = "nbl_tui_keyboard",
                                                 .funcaddr = nbl_tui_keyboard,
                                                 .argnum = 1,
                                                 .argname = {"param"},
                                                 .argtype = {ARG_NORMAL_BUF},
                                                 .argmax = {64},
                                                 .time_out = 60000};

PrepareTest(nbl_tui_keyboard) { return &tui_keyboard_test; }

struct function_description tui_container_test = {
    .funcname = "nbl_tui_container",
    .funcaddr = nbl_tui_container,
    .argnum = 4,
    .argname = {"bitmap", "bitmap_size", "bitmap_width", "bitmap_height"},
    .argtype = {ARG_NORMAL_BUF, ARG_LEN, ARG_INT, ARG_INT},
    .argdep[1] = 0 + ARG_DEPEND_MIN,
    .argmax[0] = 1024 * 1024,
    .argmax[2] = 512,
    .argmax[3] = 512,
    .time_out = 60000};

PrepareTest(nbl_tui_container) { return &tui_container_test; }

struct function_description tui_dialog_test = {.funcname = "nbl_tui_dialog",
                                               .funcaddr = nbl_tui_dialog,
                                               .argnum = 1,
                                               .argname = {"param"},
                                               .argtype = {ARG_TYPE_STRING},
                                               .argmax = {64},
                                               .time_out = 60000};

PrepareTest(nbl_tui_dialog) { return &tui_dialog_test; }

struct function_description nbl_tui_textview_test = {
    .funcname = "nbl_tui_textview",
    .funcaddr = nbl_tui_textview,
    .argnum = 1,
    .argname = {"param"},
    .argtype = {ARG_TYPE_STRING},
    .argmax = {64},
    .time_out = 60000};

PrepareTest(nbl_tui_textview) { return &nbl_tui_textview_test; }

struct function_description nbl_tui_input_field_set_text_test = {.funcname =
                                                      "nbl_tui_input_field_set_text",
                                                  .funcaddr = nbl_tui_input_field_set_text,
                                                  .argnum = 1,
                                                  .argname = {"text"},
                                                  .argtype = {ARG_TYPE_STRING},
                                                  .argmax = {64},
                                                  .time_out = 60000};

PrepareTest(nbl_tui_input_field_set_text) { return &nbl_tui_input_field_set_text_test; }
