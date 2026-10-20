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
#include "ufs_sys.h"
#include "ufshcd_crypto.h"
#include "ufs_host.h"
#include "ufs_sip.h"

/* Blk-crypto modes supported by UFS crypto */
static const struct ufs_crypto_alg_entry {
	enum ufs_crypto_alg ufs_alg;
	enum ufs_crypto_key_size ufs_key_size;
} ufs_crypto_algs[BLK_ENCRYPTION_MODE_MAX] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.ufs_alg = UFS_CRYPTO_ALG_AES_XTS,
		.ufs_key_size = UFS_CRYPTO_KEY_SIZE_256,
	},
};

static int ufshcd_program_key(struct ufs_hba *hba,
			      const union ufs_crypto_cfg_entry *cfg, int slot)
{
	int i;
	u32 slot_offset = hba->crypto_cfg_register + slot * sizeof(*cfg);
	int err = 0;

	UFS_LOGD("\n");

	/* Ensure that CFGE is cleared before programming the key */
	ufshcd_writel(hba, 0, slot_offset + 16 * sizeof(cfg->reg_val[0]));
	/* Write the key */
	for (i = 0; i < 16; i++) {
		ufshcd_writel(hba, cfg->reg_val[i],
			      slot_offset + i * sizeof(cfg->reg_val[0]));
	}
	/* Write dword 17 */
	ufshcd_writel(hba, cfg->reg_val[17],
		      slot_offset + 17 * sizeof(cfg->reg_val[0]));
	/* Dword 16 must be written last */
	ufshcd_writel(hba, cfg->reg_val[16],
		      slot_offset + 16 * sizeof(cfg->reg_val[0]));

	return err;
}

int ufshcd_crypto_keyslot_program(struct blk_crypto_profile *profile,
					 const struct blk_crypto_key *key,
					 unsigned int slot)
{
	struct ufs_hba *hba = ufs_get_host(0);
	const struct ufs_crypto_alg_entry *alg =
			&ufs_crypto_algs[key->crypto_cfg.crypto_mode];
	u8 data_unit_mask = key->crypto_cfg.data_unit_size / 512;
	int i;
	int cap_idx = -1;
	union ufs_crypto_cfg_entry cfg = {};
	int err;

	if(hba == NULL)
		return -1;

	const union ufs_crypto_cap_entry *ccap_array = hba->crypto_cap_array;
	for (i = 0; i < hba->crypto_capabilities.num_crypto_cap; i++) {
		if (ccap_array[i].algorithm_id == alg->ufs_alg &&
		    ccap_array[i].key_size == alg->ufs_key_size &&
		    (ccap_array[i].sdus_mask & data_unit_mask)) {
			cap_idx = i;
			break;
		}
	}

	if (cap_idx < 0){
		UFS_LOGE("No crypto capability found algorithm %d, key size:%d, data_unit_mask:%x\n",
			alg->ufs_alg, alg->ufs_key_size, data_unit_mask);
		return -1;
	}

	cfg.data_unit_size = data_unit_mask;
	cfg.crypto_cap_idx = cap_idx;
	cfg.config_enable = UFS_CRYPTO_CONFIGURATION_ENABLE;

	if (ccap_array[cap_idx].algorithm_id == UFS_CRYPTO_ALG_AES_XTS) {
		/* In XTS mode, the blk_crypto_key's size is already doubled */
		memcpy(cfg.crypto_key, key->raw, key->size/2);
		memcpy(cfg.crypto_key + UFS_CRYPTO_KEY_MAX_SIZE/2,
		       key->raw + key->size/2, key->size/2);
	} else {
		memcpy(cfg.crypto_key, key->raw, key->size);
	}

	err = ufshcd_program_key(hba, &cfg, slot);

	memset(&cfg, 0, sizeof(cfg));
	return err;
}

static int ufshcd_clear_keyslot(struct ufs_hba *hba, int slot)
{
	/*
	 * Clear the crypto cfg on the device. Clearing CFGE
	 * might not be sufficient, so just clear the entire cfg.
	 */
	union ufs_crypto_cfg_entry cfg = {};

	return ufshcd_program_key(hba, &cfg, slot);
}

int ufshcd_crypto_keyslot_evict(struct blk_crypto_profile *profile,
				       const struct blk_crypto_key *key,
				       unsigned int slot)
{
	struct ufs_hba *hba = ufs_get_host(0);
	if(hba == NULL)
		return -1;

	return ufshcd_clear_keyslot(hba, slot);
}

static const struct blk_crypto_ll_ops ufshcd_crypto_ops = {
	.keyslot_program	= ufshcd_crypto_keyslot_program,
	.keyslot_evict		= ufshcd_crypto_keyslot_evict,
};

static enum blk_crypto_mode_num
ufshcd_find_blk_crypto_mode(union ufs_crypto_cap_entry cap)
{
	unsigned long i;

	for (i = 0; i < ARRAY_SIZE(ufs_crypto_algs); i++) {
		if (ufs_crypto_algs[i].ufs_alg == cap.algorithm_id &&
		    ufs_crypto_algs[i].ufs_key_size == cap.key_size) {
			return i;
		}
	}
	return BLK_ENCRYPTION_MODE_INVALID;
}

bool ufshcd_crypto_enable(struct ufs_hba *hba, bool reenable)
{
	UFS_LOGD("\n");

    // Check vTEE is existed by SMC_FC_GET_VERSION_STR
    ufs_mtk_crypto_ctrl(1);
	udelay(25);

	UFS_LOGD("HCE value: %x\n",ufshcd_readl(hba, REG_CONTROLLER_ENABLE));
	/* Reset might clear all keys, so reprogram all the keys. */
	if (reenable)
		if (hba->crypto_profile_dev.num_slots)
			blk_crypto_reprogram_all_keys(*hba_to_ufs(hba)->crypto_profile);

	return true;
}

/**
 * ufshcd_hba_init_crypto_capabilities - Read crypto capabilities, init crypto
 *					 fields in hba
 * @hba: Per adapter instance
 *
 * Return: 0 if crypto was initialized or is not supported, else a -errno value.
 */
int ufshcd_hba_init_crypto_capabilities(struct ufs_hba *hba)
{
	int cap_idx;
	int err = 0;
	enum blk_crypto_mode_num blk_mode_num;
	struct blk_crypto_profile_dev *dev_profile;

	UFS_LOGI("\n");
	hba->crypto_capabilities.reg_val =
		ufshcd_readl(hba, REG_CRYPTO_CAPABILITY);
	hba->crypto_cfg_register =
		(u32)hba->crypto_capabilities.config_array_ptr * 0x100;
	hba->crypto_cap_array =
		malloc(hba->crypto_capabilities.num_crypto_cap * sizeof(hba->crypto_cap_array[0]));
	if (!hba->crypto_cap_array) {
		err = -1;
		goto out;
	}

	UFS_LOGI("Slot count  %d\n",hba->crypto_capabilities.config_count);
	UFS_LOGI("CAP count  %d\n",hba->crypto_capabilities.num_crypto_cap);
	UFS_LOGI("Host CAP count  0x%x\n",ufshcd_readl(hba, REG_CONTROLLER_CAPABILITIES));

	/* The actual number of configurations supported is (CFGC+1) */
	dev_profile = &hba->crypto_profile_dev;

	dev_profile->ll_ops = ufshcd_crypto_ops;
	/* UFS only supports 8 bytes for any DUN */
	dev_profile->max_dun_bytes_supported = 8;
	dev_profile->key_types_supported = BLK_CRYPTO_KEY_TYPE_STANDARD;
	dev_profile->num_slots = hba->crypto_capabilities.config_count + 1;
	// init block_proto->crypto_profile
	printf("ufs crypto num_slots: %u\n", dev_profile->num_slots);
	blk_crypto_profile_init(dev_profile, hba_to_ufs(hba)->crypto_profile, dev_profile->num_slots);

	/*
	 * Cache all the UFS crypto capabilities and advertise the supported
	 * crypto modes and data unit sizes to the block layer.
	 */
	for (cap_idx = 0; cap_idx < hba->crypto_capabilities.num_crypto_cap;
	     cap_idx++) {
		hba->crypto_cap_array[cap_idx].reg_val =
			ufshcd_readl(hba,
						 REG_CRYPTO_CAPABILITY_X +
						 cap_idx * sizeof(__le32));
		blk_mode_num = ufshcd_find_blk_crypto_mode(
						hba->crypto_cap_array[cap_idx]);
		if (blk_mode_num != BLK_ENCRYPTION_MODE_INVALID)
			dev_profile->modes_supported[blk_mode_num] |=
				hba->crypto_cap_array[cap_idx].sdus_mask * 512;
		UFS_LOGI("algorithm %d, key size %d, and data unit size mask 0x%x\n",
			hba->crypto_cap_array[cap_idx].algorithm_id,hba->crypto_cap_array[cap_idx].key_size,
			hba->crypto_cap_array[cap_idx].sdus_mask);
	}

	return 0;
out:
	return err;
}

/**
 * ufshcd_init_crypto - Initialize crypto hardware
 * @hba: Per adapter instance
 */
void ufshcd_init_crypto(struct ufs_hba *hba)
{
	unsigned int slot;

	/* Clear all keyslots */
	for (slot = 0; slot < hba->crypto_profile_dev.num_slots; slot++)
		ufshcd_clear_keyslot(hba, slot);
}
