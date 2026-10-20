// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/threads.h>
#include <ddk/debug.h>
#include <ddk/device.h>

#include "smc_pmic_simple.h"

void smc_pmic_init(smc_pmic_client_t* client, uint8_t sid) {
    mtx_init(&client->lock, mtx_plain);
    client->sid = sid;
}

void smc_pmic_cleanup(smc_pmic_client_t* client) {
    client->sid = -1;
}

static void smc_call(enum mtk_pmic_cmd_op cmd, uint8_t sid, uint16_t addr, uint16_t data, size_t len, zx_smc_result_t *result)
{
    zx_status_t status;
    zx_smc_parameters_t parameters;

    parameters.func_id = MTK_SIP_KERNEL_PMIC_CONTROL;
    parameters.arg1 = CMD_TYPE(MTK_SPMI_CTRL);
    parameters.arg2 = SPMI_SMC_CMDA(sid, cmd, addr, data, len);
    parameters.arg3 = 0;
    parameters.arg4 = 0;
    parameters.arg5 = 0;
    parameters.arg6 = 0;
    parameters.client_id = 0;
    parameters.secure_os_id = 0;

    status = zx_smc_call(get_root_resource(), &parameters, result);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Failed to send smcall %d\n", __func__, status);
        return;
    }
    // zxlogf(INFO, "%s:0x%llx, data:0x%lx, ret:%d\n", __func__,
	// 	SPMI_SMC_CMDA(sid, cmd, addr, data, len), (cmd == MTK_SPMI_WRITE)? data:result->arg1, status);
}

int smc_pmic_write_reg(smc_pmic_client_t* client, uint16_t addr, uint8_t data) {
    mtx_lock(&client->lock);

    zx_smc_result_t result;
    smc_call(MTK_SPMI_WRITE, client->sid, addr, data, 1, &result);

    mtx_unlock(&client->lock);

    if (result.arg2 != 0) {
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

int smc_pmic_read_reg(smc_pmic_client_t* client, uint16_t addr, uint8_t* data) {
    mtx_lock(&client->lock);

    zx_smc_result_t result;
    smc_call(MTK_SPMI_READ, client->sid, addr, 0, 1, &result);

    mtx_unlock(&client->lock);

    if (result.arg2!= 0) {
        return ZX_ERR_IO;
    }

    *data = (uint8_t)(result.arg1 & 0xFFFF);
    return ZX_OK;
}

int smc_pmic_bulk_write(smc_pmic_client_t* client, uint16_t start_addr,
                        const uint16_t* data, size_t count) {
    if (count == 0 || count > 16) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint8_t bytes[32];
    for (size_t i = 0; i < count; i++) {
        bytes[i * 2] = (uint8_t)(data[i] & 0xFF);
        bytes[i * 2 + 1] = (uint8_t)((data[i] >> 8) & 0xFF);
    }

    mtx_lock(&client->lock);

    int status = ZX_OK;
    uint16_t addr = start_addr;
    size_t offset = 0;
    size_t remaining = count * 2;

    while (remaining > 0 && status == ZX_OK) {
        size_t chunk = 1;

        uint16_t data16 = bytes[offset];

        zx_smc_result_t result;
        smc_call(MTK_SPMI_WRITE, client->sid, addr, data16, chunk, &result);

        if (result.arg2 != 0) {
            status = ZX_ERR_IO;
            break;
        }

        addr += 1;
        offset += chunk;
        remaining -= chunk;
    }

    mtx_unlock(&client->lock);
    return status;
}

int smc_pmic_bulk_read(smc_pmic_client_t* client, uint16_t start_addr,
                       uint16_t* data, size_t count) {
    if (count == 0 || count > 16) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint8_t bytes[32];

    mtx_lock(&client->lock);

    int status = ZX_OK;
    uint16_t addr = start_addr;
    size_t offset = 0;
    size_t remaining = count * 2;

    while (remaining > 0 && status == ZX_OK) {
        size_t chunk = 1;

        zx_smc_result_t result;
        smc_call(MTK_SPMI_READ, client->sid, addr, 0, chunk, &result);

        if (result.arg2 != 0) {
            status = ZX_ERR_IO;
            break;
        }

        uint16_t data16 = (uint16_t)(result.arg1 & 0xFFFF);
        bytes[offset] = (uint8_t)(data16 & 0xFF);

        addr += 1;
        offset += chunk;
        remaining -= chunk;
    }

    mtx_unlock(&client->lock);

    if (status == ZX_OK) {
        for (size_t i = 0; i < count; i++) {
            data[i] = bytes[i * 2] | ((uint16_t)bytes[i * 2 + 1] << 8);
        }
    }

    return status;
}
