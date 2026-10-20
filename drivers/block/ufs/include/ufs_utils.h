/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Copyright (c) 2019 MediaTek Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "ufs_hcd.h"

//#include <mt_gpt.h> /* @@LK2_BRINGUP_WA@@ gic not ready */

/* Byte order for UTF-16 strings */
enum utf16_endian {
    UTF16_HOST_ENDIAN,
    UTF16_LITTLE_ENDIAN,
    UTF16_BIG_ENDIAN
};

extern struct ufs_ocs_error g_ufs_ocs_error_str[];

void ufs_mtk_dump_asc_ascq(struct ufs_hba *hba, u8 asc, u8 ascq);

int utf16s_to_utf8s(const wchar_t *pwcs, int inlen, enum utf16_endian endian, u8 *s, int maxout);
int ufs_util_sanitize_inquiry_string(unsigned char *s, int len);
void ufshcd_signal_event(struct ufs_hba *hba, struct ufshcd_lrb *lrbp);