
#ifndef __PUBLIC_LIB_BLK_CRYPTO_H__
#define __PUBLIC_LIB_BLK_CRYPTO_H__

#include <zircon/device/blk_crypto.h>
#include <sync/completion.h>

#define DECLARE_WAKEUP_ENTRY(entry) completion_t entry

struct blk_crypto_profile {

  struct blk_crypto_profile_dev *dev_profile;
  /* private: The following fields shouldn't be accessed by drivers. */

  /* Number of keyslots, or 0 if not applicable */
  unsigned int num_slots;

  /* List of idle slots, with least recently used slot at front */
  //        wait_queue_head_t idle_slots_wait_queue;
  list_head idle_slots;
  mtx_t idle_slots_lock;
  DECLARE_WAKEUP_ENTRY(idle_slots_wait_entry);

  /*
   * Serializes all calls to functions in @ll_ops as well as all changes
   * to @slot_hashtable.  This can also be taken in read mode to look up
   * keyslots while ensuring that they can't be changed concurrently.
   */
  mtx_t lock;
  /*
   * Hash table which maps struct *blk_crypto_key to keyslots, so that we
   * can find a key's keyslot in O(1) time rather than O(num_slots).
   * Protected by 'lock'.
   */
  struct hlist_head *slot_hashtable;
  unsigned int log_slot_ht_size;

  /* Per-keyslot data */
  struct blk_crypto_keyslot *slots;
};

#define __dev_profile(crypto_profile)                                          \
  ({                                                                           \
    struct blk_crypto_profile_dev *_dev_profile = NULL;                        \
    if (!crypto_profile->dev_profile)                                          \
      printf("blk crypto error: NULL dev_profile\n");                          \
    else                                                                       \
      _dev_profile = crypto_profile->dev_profile;                              \
    _dev_profile;                                                              \
  })

/**
 * blk_crypto_keyslot_index() - Get the index of a keyslot
 * @slot: a keyslot that blk_crypto_get_keyslot() returned
 *
 * Return: the 0-based index of the keyslot within the device's keyslots.
 */
static inline unsigned int
blk_crypto_keyslot_index(struct blk_crypto_keyslot *slot) {
  return slot - slot->profile->slots;
}

void blk_crypto_profile_init(struct blk_crypto_profile_dev *dev_profile,
                             struct blk_crypto_profile **_profile,
                             uint16_t num_slots);

void blk_crypto_reprogram_all_keys(struct blk_crypto_profile *profile);

__EXPORT void dump_crypto_profile(struct blk_crypto_profile const *profile);

#endif
