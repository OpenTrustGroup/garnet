// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMC_PMIC_SIMPLE_H_
#define SMC_PMIC_SIMPLE_H_

#include <zircon/types.h>
#include <zircon/syscalls.h>


#define ARM_SMCCC_FAST_CALL	        (1U)
#define ARM_SMCCC_TYPE_SHIFT		31

#define ARM_SMCCC_SMC_32			0
#define ARM_SMCCC_SMC_64			1
#define ARM_SMCCC_CALL_CONV_SHIFT	30

#define ARM_SMCCC_OWNER_MASK		0x3F
#define ARM_SMCCC_OWNER_SHIFT		24

#define ARM_SMCCC_FUNC_MASK			0xFFFF

#define ARM_SMCCC_SMC_64			1
#define MTK_SIP_SMC_CONVENTION      ARM_SMCCC_SMC_64
#define ARM_SMCCC_OWNER_SIP			2

#define ARM_SMCCC_CALL_VAL(type, calling_convention, owner, func_num) \
	(((type) << ARM_SMCCC_TYPE_SHIFT) | \
	((calling_convention) << ARM_SMCCC_CALL_CONV_SHIFT) | \
	(((owner) & ARM_SMCCC_OWNER_MASK) << ARM_SMCCC_OWNER_SHIFT) | \
	((func_num) & ARM_SMCCC_FUNC_MASK))

#define MTK_SIP_SMC_CMD(fn_id) \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, MTK_SIP_SMC_CONVENTION, \
			   ARM_SMCCC_OWNER_SIP, fn_id)
#define MTK_SIP_KERNEL_PMIC_CONTROL \
	MTK_SIP_SMC_CMD(0x54a)

#define CMD_TYPE(type) (type & 0xf)
#define REG_SMC_CMDA(vm_id, pmic_type, sid, reg_handle, opc, boot_on_off) \
			(((uint64_t)vm_id << 30) + ((uint64_t)pmic_type << 14) \
			+ ((uint64_t)sid << 10) + ((uint64_t)reg_handle << 2) + \
			((uint64_t)opc << 1) + ((uint64_t)boot_on_off))

#define PMIF_BYTECNT_MAX  2
#define SPMI_SMC_CMDA(sid, opc, addr, buf, len)  (((uint64_t)sid << 42) + \
			((uint64_t)opc << 34) + ((uint64_t)addr << 18) + ((uint64_t)buf << 2) + len)

enum PMIC_VM_SUPPORT {
	VM_AND = 0,
	VM_TBOX,
	VM_SOS,
	VM_ATF,
	VM_COUNT,
};

enum mtk_pmic_cmd_type {
	MTK_REG_CTRL = 0,
	MTK_SPMI_CTRL,
	MTK_MAX_CTRL_TYPE,
};

enum mtk_pmic_cmd_op {
	MTK_REG_DIS = 0,
	MTK_REG_EN,
	MTK_SPMI_READ,
	MTK_SPMI_WRITE,
	MTK_SPMI_ARB,
	MTK_MAX_OP_CMD,
};


typedef struct {
    mtx_t lock;
    uint8_t sid;
} smc_pmic_client_t;

void smc_pmic_init(smc_pmic_client_t* client, uint8_t sid);

void smc_pmic_cleanup(smc_pmic_client_t* client);

int smc_pmic_write_reg(smc_pmic_client_t* client, uint16_t addr, uint8_t data);

int smc_pmic_read_reg(smc_pmic_client_t* client, uint16_t addr, uint8_t* data);

int smc_pmic_bulk_write(smc_pmic_client_t* client, uint16_t start_addr,
                        const uint16_t* data, size_t count);

int smc_pmic_bulk_read(smc_pmic_client_t* client, uint16_t start_addr,
                       uint16_t* data, size_t count);

#endif  // SMC_PMIC_SIMPLE_H_
