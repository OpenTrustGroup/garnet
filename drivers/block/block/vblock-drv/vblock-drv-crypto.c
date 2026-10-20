// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vblock-drv-crypto.h"
#include "vblock-drv.h"
#include "../blk_crypto.h"
#include "bitops.h"

#include <string.h>
#include <ddk/debug.h>

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif

void virtio_blk_evict_all_keys(guest_ctx_t *guest) {
  struct blk_crypto_profile *profile = guest->get_crypto_profile(guest);
  int i, count = 0;

  for (i = 0; i < BLK_CRYPTO_KEY_SLOT_NUM; ++i) {
    uint64_t const mask = BIT(i);
    if (!(mask & guest->crypto_key_bitmap))
      continue;

    int ret = __blk_crypto_evict_key(profile, &guest->crypto_keys[i]);
    if (ret)
      zxlogf(ERROR, "[vblk-crypto][%d]: evict key@%d failed: %d\n", guest->vmid,
             i, ret);
    else
      ++count;
  }

  guest->crypto_key_bitmap = 0;
  zxlogf(INFO, "[vblk-crypto][%d]: evict all key done, total: %d\n", guest->vmid,
         count);
}

static int virtio_blk_crypt_add_key(struct blk_crypto_profile *profile,
                                    guest_ctx_t *guest,
                                    struct blk_crypto_base_key const *new_key,
                                    int slot) {
  struct blk_crypto_key *key;
  bool ret;

  key = &guest->crypto_keys[slot];
  memcpy(&key->base_key, (void *)new_key, sizeof(new_key[0]));

  ret = __blk_crypto_cfg_supported(profile, &key->crypto_cfg);
  if (!ret) {
    zxlogf(ERROR, "[vblk-crypto][%d]: error crypto cfg not supported\n",
           guest->vmid);
    return -1;
  }

  return 0;
}

static inline void update_bits(unsigned long *bmap, unsigned long mask,
                               unsigned long new_bmap) {
  *bmap = (*bmap & ~mask) | (mask & new_bmap);
}

static int virtio_blk_crypt_key_handle(struct blk_crypto_profile *profile,
                                       guest_ctx_t *guest, struct block_op *req,
                                       req_hdr_t const *hdr) {
  struct blk_crypto_base_key const *crypt_key;
  uint8_t bmap;
  uint64_t cur_bmap, update, toggle_map;
  uint64_t mask;
  int slot;
  int ret;

  /* Other type may be also equiped with crypto ctx */
  if (hdr->type != VIRTIO_BLK_T_OUT && hdr->type != VIRTIO_BLK_T_IN) {
    /* Just print a warning log, key will still been programmed. */
    /* Lets see which type except IN and OUT can alse take a crypto
     * with itself */
    zxlogf(WARN,
           "[vblk-crypto][%d]: set but not a R/W request!, hdr->type: %u\n",
           guest->vmid, hdr->type);
  }

  if (hdr->key_index >= BLK_CRYPTO_KEY_SLOT_NUM)
    zxlogf(ERROR, "[vblk-crypto][%d]: key_index out of bound\n", guest->vmid);

  memcpy(block_op_crypto_dun(req), hdr->bc_dun, sizeof(hdr->bc_dun));
  slot = hdr->key_index;
  toggle_map = guest->crypto_key_bitmap & (0x1UL << (32 + slot));
  mask = BIT(slot + 32);
  bmap = hdr->bmap;
  cur_bmap = (bmap & 0x1UL) << (32 + slot);

  update = cur_bmap ^ toggle_map;
  if (!update)
    return 0;

  /* reprogram */
  if (likely(test_bit(slot, &guest->crypto_key_bitmap))) {
    // zxlogf(INFO, "[vblk-crypto][%d]: reprogram @%d\n", guest->vmid, slot);
    __blk_crypto_evict_key(profile, &guest->crypto_keys[slot]);
    goto add_key;
  }

  // zxlogf(INFO, "[vblk-crypto][%d]: program key @%d\n", guest->vmid, slot);
  set_bit(slot, &guest->crypto_key_bitmap);
add_key:
  crypt_key = &hdr->key;
  ret = virtio_blk_crypt_add_key(profile, guest, crypt_key, slot);
  if (unlikely(ret)) {
    zxlogf(ERROR,
           "[vblk-crypto][%d]: virtio_blk_crypt_add_key failed, ret: %d\n",
           guest->vmid, ret);
    return ret;
  }

  update_bits(&guest->crypto_key_bitmap, mask, cur_bmap);

  return 0;
}

void bop_set_crypto(guest_ctx_t *guest, block_op_t *bop, req_hdr_t const *hdr,
                    void *crypto_ctx_alloc) {
  struct blk_crypto_profile *profile = guest->get_crypto_profile(guest);

  block_op_crypto_ctx(bop) = crypto_ctx_alloc;
  if (unlikely(!block_op_crypto_ctx(bop))) {
    zxlogf(ERROR, "[vblk-crypto][%d]: failed to alloc crypto ctx\n", guest->vmid);
    return;
  }

  memset(block_op_crypto_ctx(bop), 0, sizeof(block_op_crypto_ctx(bop)[0]));

  virtio_blk_crypt_key_handle(profile, guest, bop, hdr);
  // get a slot in profile, and do a programming if needed
  blk_crypto_get_keyslot(
      profile, block_op_crypto_key(bop) = guest->crypto_keys + hdr->key_index,
      &block_op_crypto_keyslot(bop));
}
