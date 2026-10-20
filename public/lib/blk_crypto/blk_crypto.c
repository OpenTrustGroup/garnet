#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "blk_crypto.h"

__EXPORT void dump_crypto_profile(struct blk_crypto_profile const *profile) {
  printf("dump blk_crypto_profile:\n");
  printf("dev_profile: 0x%lx\n", (unsigned long)profile->dev_profile);
  printf("num_slots: %u\n", profile->num_slots);
  printf("slot_hashtable: 0x%lx\n", (unsigned long)profile->slot_hashtable);
  printf("log_slot_ht_size: %u\n", profile->log_slot_ht_size);
  printf("slots: 0x%lx\n", (unsigned long)profile->slots);
  //	printf("");
}

#define _63f (ULONG_MAX >> 8)

#define ilog2(val)                                                             \
  (((val) == 0)                                                                \
       ? 0                                                                     \
       : (((sizeof(unsigned long long) * 8) - 1) - __builtin_clzll(val)))

// this function is only for keyslot assignment
// ll_ops and caps shall be set by hand.
// a practical blk device driver shall negotiate will hardware to first confirm
// that if it support inline crypto, then get the keyslot number, then call this
// function to preliminarily init the crypto profile, then it should set the
// ll_ops and the caps.
__EXPORT void
blk_crypto_profile_init(struct blk_crypto_profile_dev *dev_profile,
                        struct blk_crypto_profile **_profile,
                        uint16_t num_slots) {
  unsigned int slot;
  int slot_hashtable_size, i;
  struct blk_crypto_profile *profile;

  *_profile = NULL;

  if (!num_slots) {
    printf("WARN: ufs crypto, num_slots == 0\n");
    return;
  }

  profile = calloc(1, sizeof(profile[0]));
  if (!profile) {
    printf("WARN: ufs crypto, failed to alloc profile\n");
    return;
  }

  memset(profile, 0, sizeof(profile[0]));
  profile->slots = malloc(num_slots * sizeof(profile->slots[0]));
  if (!profile->slots) {
    printf("WARN: ufs crypto, failed to alloc profile->slots\n");
    free(profile);
    return;
  }

  profile->num_slots = num_slots;

  list_initialize(&profile->idle_slots);

  for (slot = 0; slot < num_slots; slot++) {
    profile->slots[slot].profile = profile;
    list_add_tail(&profile->idle_slots, &profile->slots[slot].idle_slot_node);
  }

  // mutex_init ?
  mtx_init(&profile->idle_slots_lock, mtx_plain);

  slot_hashtable_size = roundup_pow_of_two(num_slots);
  /*
   * hash_ptr() assumes bits != 0, so ensure the hash table has at least 2
   * buckets.  This only makes a difference when there is only 1 keyslot.
   */
  if (slot_hashtable_size < 2)
    slot_hashtable_size = 2;

  profile->log_slot_ht_size = ilog2(slot_hashtable_size);
  profile->slot_hashtable =
      malloc(slot_hashtable_size * sizeof(profile->slot_hashtable[0]));
  if (!profile->slot_hashtable) {
    printf("WARN: ufs crypto, failed to alloc profile->slot_hashtable\n");
    free(profile->slots);
    free(profile);
    return;
  }
  for (i = 0; i < slot_hashtable_size; i++)
    INIT_HLIST_HEAD(&profile->slot_hashtable[i]);

  profile->dev_profile = dev_profile;
  *_profile = profile;

  dump_crypto_profile(profile);
}

/**
 * blk_crypto_reprogram_all_keys() - Re-program all keyslots.
 * @profile: The crypto profile
 *
 * Re-program all keyslots that are supposed to have a key programmed.  This is
 * intended only for use by drivers for hardware that loses its keys on reset.
 *
 * Context: Process context. Takes and releases profile->lock.
 */
__EXPORT void
blk_crypto_reprogram_all_keys(struct blk_crypto_profile *profile) {
  unsigned int slot;

  if (profile->num_slots == 0)
    return;

  /* This is for device initialization, so don't resume the device */
  mtx_lock(&profile->lock);
  for (slot = 0; slot < profile->num_slots; slot++) {
    const struct blk_crypto_key *key = profile->slots[slot].key;
    int err;

    if (!key)
      continue;

    err = __dev_profile(profile)->ll_ops.keyslot_program(profile, key, slot);
    if (err)
      printf("nebula, blk crypto, reprogram all key, err happened: %d\n", err);
  }
  mtx_unlock(&profile->lock);
}
