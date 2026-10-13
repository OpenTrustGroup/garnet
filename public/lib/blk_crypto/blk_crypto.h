// SPDX-License-Identifier: BSD-3-Clause

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
   * Held across every call into @ll_ops and across every change to
   * @slot_hashtable, so that a keyslot cannot move while it is being
   * looked up.
   */
  mtx_t lock;
  /*
   * Hash table which maps struct *blk_crypto_key to keyslots, so that we
   * can find a key's keyslot in O(1) time rather than O(num_slots).
   * Protected by 'lock'.
   */
  list_node_t *slot_hashtable;
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

/*
 * blk_crypto_profile_init(), blk_crypto_reprogram_all_keys() and
 * blk_crypto_keyslot_index() are declared in <zircon/device/blk_crypto.h>:
 * they are the surface a storage driver uses, and a driver must be able to
 * build against the published DDK without this header.  What stays here is
 * the profile layout and the helpers that need it.
 */

#endif
