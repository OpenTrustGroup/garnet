// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blk_crypto.h"

#define _63f (ULONG_MAX >> 8)

#define WAIT_ON(entry, condition, action0, action1)                            \
  do {                                                                         \
    action0;                                                                   \
    if (condition)                                                             \
      break;                                                                   \
    action1;                                                                   \
    completion_wait(&entry, ZX_TIME_INFINITE);                                 \
  } while (1)

#define WAKEUP_ENTRY(entry)                                                    \
  do {                                                                         \
    completion_signal(&entry);                                                 \
  } while (0)

#define WAKEUP_RESET(entry)                                                    \
  do {                                                                         \
    completion_reset(&entry);                                                  \
  } while (0)

void blk_crypto_key_set_priv(struct blk_crypto_key *key, uint8_t priv) {
  key->priv = priv;
}

static inline struct hlist_head *
blk_crypto_hash_bucket_for_key(struct blk_crypto_profile *profile,
                               const struct blk_crypto_key *key) {
  uint64_t const pkey = (uint64_t)key;
  void const *__key = (void *)((pkey & _63f) | ((uint64_t)key->priv << 56));
  return &profile->slot_hashtable[hash_ptr(__key, profile->log_slot_ht_size)];
}

// profile->lock shall hold
static struct blk_crypto_keyslot *
blk_crypto_find_keyslot(struct blk_crypto_profile *profile,
                        const struct blk_crypto_key *key) {
  const struct hlist_head *head = blk_crypto_hash_bucket_for_key(profile, key);
  struct blk_crypto_keyslot *slotp;

  hlist_for_each_entry(struct blk_crypto_keyslot, slotp, head, hash_node) {
    if (slotp->key == key)
      return slotp;
  }
  return NULL;
}

static inline void
blk_crypto_profile_keyslot_remove(struct blk_crypto_profile *profile,
                                  struct blk_crypto_keyslot *keyslot) {
  hlist_del(&keyslot->hash_node);
  keyslot->key = NULL;
}

static void
blk_crypto_remove_slot_from_lru_list(struct blk_crypto_keyslot *slot) {
  struct blk_crypto_profile *profile = slot->profile;

  mtx_lock(&profile->idle_slots_lock);
  list_delete(&slot->idle_slot_node);
  mtx_unlock(&profile->idle_slots_lock);
}

static struct blk_crypto_keyslot *
blk_crypto_find_and_grab_keyslot(struct blk_crypto_profile *profile,
                                 const struct blk_crypto_key *key) {
  struct blk_crypto_keyslot *slot;

  slot = blk_crypto_find_keyslot(profile, key);
  if (!slot)
    return NULL;
  if (atomic_fetch_add(&slot->slot_refs, 1) == 0) {
    /* Took first reference to this slot; remove it from LRU list */
    blk_crypto_remove_slot_from_lru_list(slot);
  }
  return slot;
}

static inline void blk_crypto_hw_enter(struct blk_crypto_profile *profile) {
  /*
   * Calling into the driver requires profile->lock held and the device
   * resumed.  But we must resume the device first, since that can acquire
   * and release profile->lock via blk_crypto_reprogram_all_keys().
   */
  mtx_lock(&profile->lock);
}

static inline void blk_crypto_hw_exit(struct blk_crypto_profile *profile) {
  mtx_unlock(&profile->lock);
}

int blk_crypto_get_keyslot(struct blk_crypto_profile *profile,
                           const struct blk_crypto_key *key,
                           struct blk_crypto_keyslot **slot_ptr) {
  struct blk_crypto_keyslot *slot;
  int slot_idx;
  int err;

  *slot_ptr = NULL;

  /*
   * If the device has no concept of "keyslots", then there is no need to
   * get one.
   */
  if (unlikely(profile->num_slots == 0))
    return 0;

  mtx_lock(&profile->lock);
  slot = blk_crypto_find_and_grab_keyslot(profile, key);
  mtx_unlock(&profile->lock);
  if (slot)
    goto success;

  for (;;) {
    blk_crypto_hw_enter(profile);
    slot = blk_crypto_find_and_grab_keyslot(profile, key);
    if (slot) {
      blk_crypto_hw_exit(profile);
      goto success;
    }

    /*
     * If we're here, that means there wasn't a slot that was
     * already programmed with the key. So try to program it.
     */
    if (!list_is_empty(&profile->idle_slots))
      break;

    blk_crypto_hw_exit(profile);
    WAIT_ON(profile->idle_slots_wait_entry,
            !list_is_empty(&profile->idle_slots), , );

    WAKEUP_RESET(profile->idle_slots_wait_entry);
  }

  slot = list_peek_head_type(&profile->idle_slots, struct blk_crypto_keyslot,
                             idle_slot_node);
  slot_idx = blk_crypto_keyslot_index(slot);

  err = __dev_profile(profile)->ll_ops.keyslot_program(profile, key, slot_idx);
  if (err) {
    zxlogf(ERROR, "keyslot_program failed: %d\n", err);
    WAKEUP_ENTRY(profile->idle_slots_wait_entry);
    blk_crypto_hw_exit(profile);
    return -1;
  }

  /* Move this slot to the hash list for the new key. */
  if (slot->key)
    hlist_del(&slot->hash_node);

  slot->key = key;
  hlist_add_head(&slot->hash_node,
                 blk_crypto_hash_bucket_for_key(profile, key));

  atomic_init(&slot->slot_refs, 1);

  blk_crypto_remove_slot_from_lru_list(slot);

  blk_crypto_hw_exit(profile);
success:
  *slot_ptr = slot;
  return 0;
}

/**
 * blk_crypto_put_keyslot() - Release a reference to a keyslot
 * @slot: The keyslot to release the reference of
 *
 * Context: Any context.
 */
void blk_crypto_put_keyslot(struct blk_crypto_keyslot *slot) {
  struct blk_crypto_profile *profile = slot->profile;

  mtx_lock(&profile->idle_slots_lock);
  if (atomic_fetch_sub(&slot->slot_refs, 1) == 1) {
    list_add_tail(&profile->idle_slots, &slot->idle_slot_node);
    mtx_unlock(&profile->idle_slots_lock);
    // TODO: a wakeup-like operation is needed here
    WAKEUP_ENTRY(profile->idle_slots_wait_entry);
    return;
  }
  mtx_unlock(&profile->idle_slots_lock);
}

/**
 * __blk_crypto_cfg_supported() - Check whether the given crypto profile
 *				  supports the given crypto configuration.
 * @profile: the crypto profile to check
 * @cfg: the crypto configuration to check for
 *
 * Return: %true if @profile supports the given @cfg.
 */
bool __blk_crypto_cfg_supported(struct blk_crypto_profile *profile,
                                const struct blk_crypto_config *cfg) {
  if (!profile)
    return false;
  if (!(__dev_profile(profile)->modes_supported[cfg->crypto_mode] &
        cfg->data_unit_size)) {

    zxlogf(ERROR, "blk-crypto, modes_supported[%u]: %u, cfg->dus: %u\n", (uint32_t)cfg->crypto_mode,
		(uint32_t)__dev_profile(profile)->modes_supported[cfg->crypto_mode], (uint32_t)cfg->data_unit_size);
    return false;
  }
  if (__dev_profile(profile)->max_dun_bytes_supported < cfg->dun_bytes) {
    zxlogf(ERROR, "blk-crypto, max_dun_bytes_supported: %u, cfg->dub: %u\n",
		    __dev_profile(profile)->max_dun_bytes_supported, cfg->dun_bytes);
    return false;
  }
  return true;
}

/*
 * This is an internal function that evicts a key from an inline encryption
 * device that can be either a real device or the blk-crypto-fallback "device".
 * It is used only by blk_crypto_evict_key(); see that function for details.
 */
int __blk_crypto_evict_key(struct blk_crypto_profile *profile,
                           const struct blk_crypto_key *key) {
  struct blk_crypto_keyslot *slot;
  int err;

  if (unlikely(profile->num_slots == 0)) {
    if (__dev_profile(profile)->ll_ops.keyslot_evict) {
      blk_crypto_hw_enter(profile);
      err = __dev_profile(profile)->ll_ops.keyslot_evict(profile, key, -1);
      blk_crypto_hw_exit(profile);
      return err;
    }
    return 0;
  }

  blk_crypto_hw_enter(profile);
  slot = blk_crypto_find_keyslot(profile, key);
  if (!slot) {
    /*
     * Not an error, since a key not in use by I/O is not guaranteed
     * to be in a keyslot.  There can be more keys than keyslots.
     */
    err = 0;
    goto out;
  }

  if ((atomic_load(&slot->slot_refs) != 0)) {
    /* BUG: key is still in use by I/O */
    zxlogf(WARN, "blk-crypto, evict a key in using\n");
    err = ZX_ERR_SHOULD_WAIT;
    goto out_remove;
  }
  err = __dev_profile(profile)->ll_ops.keyslot_evict(
      profile, key, blk_crypto_keyslot_index(slot));
out_remove:
  /*
   * Callers free the key even on error, so unlink the key from the hash
   * table and clear slot->key even on error.
   */
  blk_crypto_profile_keyslot_remove(profile, slot);
out:
  blk_crypto_hw_exit(profile);
  return err;
}

void blk_crypto_profile_destroy(struct blk_crypto_profile *profile) {
  if (!profile)
    return;
  free(profile->slot_hashtable);
  free(profile->slots);
  memset(profile, 0, sizeof(*profile));
}
