/*
 * Copyright (c) 2018 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "device.h"

#include <threads.h>

#include "debug.h"

pthread_mutex_t irq_callback_lock;

struct brcmf_bus* dev_get_drvdata(struct brcmf_device* dev) {
    return dev->drvdata;
}

struct brcmfmac_platform_data* dev_get_platdata(struct brcmf_device* dev) {
    brcmf_err("dev_get_platdata was called, but I don't know how to do it.\n");
    return NULL;
}

void dev_set_drvdata(struct brcmf_device* dev, struct brcmf_bus* drvdata) {
    dev->drvdata = drvdata;
}
