#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "blk_crypto.h"

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
    profile->slots[slot].key = NULL;
    /* slots came from malloc: a node has to be cleared before it can be
     * asked whether it is in a list. */
    list_clear_node(&profile->slots[slot].hash_node);
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
    list_initialize(&profile->slot_hashtable[i]);

  profile->dev_profile = dev_profile;
  *_profile = profile;
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

/*
 * Was a static inline in blk_crypto.h.  It needs the profile layout, so it
 * lives with the implementation and the driver calls it through the DDK.
 */
unsigned int blk_crypto_keyslot_index(struct blk_crypto_keyslot *slot) {
  return slot - slot->profile->slots;
}
