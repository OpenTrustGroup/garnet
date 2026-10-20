
#ifndef __BLK_CRYPTO_H__
#define __BLK_CRYPTO_H__

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <threads.h>
#include <zircon/driver/binding.h>
#include <zircon/types.h>
#include <zircon/device/blk_crypto.h>
#include <stdatomic.h>

#include "garnet/public/lib/blk_crypto/blk_crypto.h"

int __blk_crypto_evict_key(struct blk_crypto_profile *profile,
                           const struct blk_crypto_key *key);

bool __blk_crypto_cfg_supported(struct blk_crypto_profile *profile,
                                const struct blk_crypto_config *cfg);

int blk_crypto_get_keyslot(struct blk_crypto_profile *profile,
                           const struct blk_crypto_key *key,
                           struct blk_crypto_keyslot **slot_ptr);

void blk_crypto_put_keyslot(struct blk_crypto_keyslot *slot);

#endif
