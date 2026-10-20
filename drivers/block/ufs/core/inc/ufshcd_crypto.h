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

#include "ufs_hcd.h"
#include "ufs_hci.h"
#include "ufs_utils.h"
#include <garnet/public/lib/blk_crypto/blk_crypto.h>

/*
 * Currently the maximum standard key size is 64 bytes, as that is the key size
 * of BLK_ENCRYPTION_MODE_AES_256_XTS which takes the longest key.
 *
 * The maximum hardware-wrapped key size depends on the hardware's key wrapping
 * algorithm, which is a hardware implementation detail, so it isn't precisely
 * specified.  But currently 128 bytes is plenty in practice.  Implementations
 * are recommended to wrap a 32-byte key for the hardware KDF with AES-256-GCM,
 * which should result in a size closer to 64 bytes than 128.
 *
 * Both of these values can trivially be increased if ever needed.
 */
#define BLK_CRYPTO_MAX_STANDARD_KEY_SIZE	64
#define BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE	128

/* This should use max(), but max() doesn't work in a struct definition. */
#define BLK_CRYPTO_MAX_ANY_KEY_SIZE \
	(BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE > \
	 BLK_CRYPTO_MAX_STANDARD_KEY_SIZE ? \
	 BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE : BLK_CRYPTO_MAX_STANDARD_KEY_SIZE)

#define BLK_CRYPTO_MAX_IV_SIZE		32
#define UPIU_COMMAND_CRYPTO_EN_OFFSET	23

static inline void ufshcd_prepare_lrbp_crypto(struct ufshcd_lrb *lrbp)
{
	block_op_t *bop = lrbp->bop;
	if (!bop || !bop->rw.crypto_context ||!bop->rw.crypto_context->keyslot) {
		lrbp->crypto_key_slot = -1;
		return;
	}

	lrbp->crypto_key_slot = blk_crypto_keyslot_index(bop->rw.crypto_context->keyslot);
	lrbp->data_unit_num = bop->rw.crypto_context->bc_dun[0];
}

static inline void
ufshcd_prepare_req_desc_hdr_crypto(struct ufshcd_lrb *lrbp,
				   struct request_desc_header *h)
{
	if (lrbp->crypto_key_slot < 0 || lrbp->crypto_key_slot > 32 || lrbp->data_unit_num == 0)
		return;

	h->dword_0 |= (1U << UPIU_COMMAND_CRYPTO_EN_OFFSET);  /* crypto enable */
	h->dword_0 |= lrbp->crypto_key_slot;
	h->dword_1 = lower_32_bits(lrbp->data_unit_num);
	h->dword_3 = upper_32_bits(lrbp->data_unit_num);
}

int ufshcd_hba_init_crypto_capabilities(struct ufs_hba *hba);
bool ufshcd_crypto_enable(struct ufs_hba *hba, bool);
void ufshcd_init_crypto(struct ufs_hba *hba);
