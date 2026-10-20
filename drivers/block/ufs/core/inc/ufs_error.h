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

// Error codes - UFS General
#define UFS_ERR_NONE                     (0)
#define UFS_ERR_NOT_SUPPORTED            (-2)
#define UFS_ERR_NO_RESOURCES             (-3)
#define UFS_ERR_NO_MEMORY                (-4)
#define UFS_ERR_INVALID_ARGS             (-10)

#define UFS_ERR_NEED_REINIT_HOST         (-201)
#define UFS_ERR_INVALID_LU               (-202)
#define UFS_ERR_TIMEOUT_QUERY_REQUEST    (-203)
#define UFS_ERR_OCS_ERROR                (-204)
#define UFS_ERR_TASK_RESP_ERROR          (-205)
#define UFS_ERR_INVALID_DESCRIPTOR       (-206)
#define UFS_ERR_FDEVICEINIT_NOT_CLR      (-207)
#define UFS_ERR_UNIT_ATTENTION_RETRY     (-208)
