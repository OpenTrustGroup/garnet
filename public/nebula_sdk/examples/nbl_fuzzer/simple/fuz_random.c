// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <uapi/err.h>

#include <openssl/aes.h>

#include <fuz_common.h>

#define AES256_KEY_SIZE 32

/* CSPRNG state */
static uint8_t entropy_pool[AES256_KEY_SIZE];
static uint64_t counter;
static uint8_t rand_bytes_buf[AES_BLOCK_SIZE];
static uint32_t rand_bytes_pos;
static bool rng_initialized;

/**
 * Implement random functions using Nebula system.
 * 1. If set random seed, then create fixed random sequence data.
 * 2. If random function can draw buffer with any length.
 *
 * Cannot use srandom/random, it doesn't draw all buffer, just [0~MAX_RANDOM).
 * Cannot use zx_cprng_draw / trusty_rng_secure_rand / trusty_rng_hw_rand,
 *   because they don't draw fixed sequence data even if set seed or entropy.
 * So create a new implementation from trusty_rng_secure_rand.
 */

static int _trusty_rng_init(fuz_seed_t rand_seed);
static int _trusty_rng_secure_rand(uint8_t *data, size_t len);

/**
 * Initialize the random seed.
 * And then draw fixed random sequence data
 */
int fuz_init_random(fuz_seed_t seed) { return _trusty_rng_init(seed); }

/**
 * Draw random data in buffer with length.
 */
int fuz_draw_random(void *buf, size_t len) {
  return _trusty_rng_secure_rand((uint8_t *)buf, len);
}

uint32_t fuz_rnd() {
  uint32_t result;
  int err;

  err = _trusty_rng_secure_rand((uint8_t *)&result, sizeof(int));
  if (err != NO_ERROR) {
    fuz_print_log("%s Cannot draw data by trusty prng %d.\n", __func__, err);
    exit(-1);
  }

  return result;
}

// draw a random number in [0, max)
uint32_t fuz_rnd_range(uint32_t max) {
  uint32_t result;
  int err;

  err = _trusty_rng_secure_rand((uint8_t *)&result, sizeof(int));
  if (err != NO_ERROR) {
    fuz_print_log("%s Cannot draw data by trusty prng %d.\n", __func__, err);
    exit(-1);
  }

  return result % max;
}

/*
 * Pick a random power of two between 2^0 and 2^(WORD_BIT-1)
 */
unsigned long fuz_rand_single_bit(unsigned char size) {
  if (size > WORD_BIT)
    size = WORD_BIT;

  return (1UL << (fuz_rnd() % size));
}

/*
 * set N bits, where N= rand(0 - WORDSIZE/2)
 */
static unsigned long fuz_randbits(int limit);
/*
 * Pick 1 random byte, and repeat it through a long.
 */
static unsigned long fuz_rept_byte(void);
/*
 * Generate, and munge a 16bit number.
 */
unsigned short fuz_rand16(void) {
  unsigned short r = 0, r2;

  switch (fuz_rnd() % 6) {
  case 0:
    r = RAND_BYTE();
    break;

  case 1:
    r = fuz_rand_single_bit(16);
    break;
  case 2:
    r = fuz_randbits(16);
    break;
  case 3:
    r = fuz_rnd();
    break;
  case 4:
    r2 = fuz_rnd() & 0xff;
    r = r2 | r2 << 8;
    break;
  case 5:
    return 0 - ((fuz_rnd() % 10) + 1);
  }

  /* Sometimes flip sign */
  if (ONE_IN(25))
    r = ~r + 1;

  if (ONE_IN(25)) {
    int _div = 1 << RAND_RANGE(1, 4); /* 2,4,8 or 16 */
    r /= _div;
  }

  if (RAND_BOOL()) {
    /* limit the size */
    switch (fuz_rnd() % 3) {
    case 0:
      r &= 0xff;
      break;
    case 1:
      r &= 0xfff;
      break;
    case 2:
      r &= PAGE_MASK;
      break;
    }
  }
  return r;
}

/*
 * Generate, and munge a 32bit number.
 */
unsigned int fuz_rand32(void) {
  unsigned long r = 0;

  switch (fuz_rnd() % 8) {
  case 0:
    r = RAND_BYTE();
    break;
  case 1:
    r = fuz_rand16();
    break;
  case 2:
    r = fuz_rand_single_bit(32);
    break;
  case 3:
    r = fuz_randbits(32);
    break;
  case 4:
    r = fuz_rnd();
    break;
  case 5:
    r = fuz_rept_byte();
    break;

  case 6:
    return get_interesting_value();

  case 7:
    return 0 - ((fuz_rnd() % 10) + 1);
  }

  /* Sometimes deduct it from INT_MAX */
  if (ONE_IN(25))
    r = INT_MAX - r;

  /* Sometimes flip sign */
  if (ONE_IN(25))
    r = ~r + 1;

  /* we might get lucky if something is counting ints/longs etc. */
  if (ONE_IN(25)) {
    int _div = 1 << RAND_RANGE(1, 4); /* 2,4,8 or 16 */
    r /= _div;
  }

  if (RAND_BOOL()) {
    /* limit the size */
    switch (fuz_rnd() % 4) {
    case 0:
      r &= 0xff;
      break;
    case 1:
      r &= 0xffff;
      break;
    case 2:
      r &= PAGE_MASK;
      break;
    case 3:
      r &= 0xffffff;
      break;
    }
  }

  return r;
}

/*
 *	This is the generic part of the trusty app rng service.
 *	A server implementation for retrieving hardware backed
 *	random numbers, used by trusty_rng_hw_rand,
 *	is required to be provided by a hardware
 *	specific backend at tipc port HWRNG_PORT.
 *
 *	Clients of this library are encouraged to use the
 *	trusty_rng_secure_rand rather than the trusty_rng_hw_rand
 *	routine, as the latter incurs an IPC penalty with connection
 *	overhead.
 */

static int _aes_encrypt_ecb(uint8_t *dst, const uint8_t *src,
                            const uint8_t *key_bytes, size_t key_size_bits) {
  AES_KEY key;
  if (AES_set_encrypt_key(key_bytes, key_size_bits, &key) != 0)
    return ERR_INVALID_ARGS;

  AES_encrypt(src, dst, &key);
  return NO_ERROR;
}

static int _trusty_rng_init(fuz_seed_t rand_seed) {
  int err = NO_ERROR;

  // err = trusty_rng_hw_rand(entropy_pool, sizeof(entropy_pool));
  // if (err)
  //	goto done;
  // Initialize one part of entropy.
  memset(entropy_pool, 0, sizeof(entropy_pool));
  memcpy(entropy_pool, &rand_seed, sizeof(rand_seed));

  counter = 0;
  rand_bytes_pos = sizeof(rand_bytes_buf);
  rng_initialized = true;

  return err;
}

static int _trusty_rng_secure_rand(uint8_t *data, size_t len) {
  int err = NO_ERROR;

  if (!data || !len)
    return ERR_INVALID_ARGS;

  if (!rng_initialized) {
    err = ERR_NOT_READY;
    goto done;
  }

  while (len > 0) {
    if (rand_bytes_pos == sizeof(rand_bytes_buf)) {
      /* Increment the counter and put the counter value into a buffer */
      uint8_t counter_buf[AES_BLOCK_SIZE];
      ++counter;
      memset(counter_buf, 0, sizeof(counter_buf));
      memcpy(counter_buf, &counter, sizeof(counter));

      /* Encrypt the counter, placing the results in rand_bytes_buf */
      err = _aes_encrypt_ecb(rand_bytes_buf, counter_buf, entropy_pool,
                             sizeof(entropy_pool) * 8);
      if (err)
        goto done;

      rand_bytes_pos = 0;
    }

    /* Copy random bytes into the output buffer */
    while (rand_bytes_pos < sizeof(rand_bytes_buf) && len > 0) {
      *data++ = rand_bytes_buf[rand_bytes_pos++];
      len--;
    }
  }
done:
  return err;
}

/*
 * set N bits, where N= rand(0 - WORDSIZE/2)
 */
static unsigned long fuz_randbits(int limit) {
  unsigned int num = fuz_rnd() % (limit / 2);
  unsigned int i;
  unsigned long r = 0;

  for (i = 0; i < num; i++)
    r |= (1UL << (fuz_rnd() % (limit - 1)));

  return r;
}

/*
 * Pick 1 random byte, and repeat it through a long.
 */
static unsigned long fuz_rept_byte(void) {
  unsigned long r = RAND_BYTE();

  r = (r << 8) | r;
  r = (r << 16) | r;
#if WORD_BIT == 64
  r = (r << 32) | r;
#endif
  return r;
}

unsigned long fuz_get_len(void) {
  int i = 0;

  if (RAND_BOOL()) {
    switch (fuz_rnd() % 4) {
    case 0:
      return sizeof(char);
    case 1:
      return sizeof(int);
    case 2:
      return sizeof(long);
    case 3:
      return PAGE_SIZE;
    }
  }

  i = fuz_rand32();

  /* short circuit if 0 */
  if (i == 0)
    return 0;

  switch (fuz_rnd() % 6) {
  case 0:
    i &= 0xff;
    break;
  case 1:
    i &= PAGE_SIZE - 1;
    break;
  case 2:
    i &= 0xffff;
    break;
  case 3:
    i &= 0xffffff;
    break;
  case 4:
    i &= 0xffffffff;
    break;
  case 5:
    // Pass through
    break;
  }

  /* again, short circuit if 0 */
  if (i == 0)
    return 0;

  /* we might get lucky if something is counting ints/longs etc. */
  if (ONE_IN(4)) {
    int _div = 1 << RAND_RANGE(1, 4); /* 2,4,8 or 16 */
    i /= _div;
  }

  return i;
}

/**
 * Get a value as unsigned integer with random value.
 */
unsigned int fuz_get_uint() {
  int rlt;
  unsigned int data;
  rlt = draw_data_by_trusty_prng(sizeof(data), (uint8_t *)&data);
  if (rlt) {
    fuz_print_log("Cannot draw data by trusty prng.\n");
    exit(1);
  }

  return data;
}

/**
 * Get a value as unsigned long with random value.
 */
uint64_t fuz_get_uint64() {
  int rlt;
  uint64_t data;
  rlt = draw_data_by_trusty_prng(sizeof(data), (uint8_t *)&data);
  if (rlt) {
    fuz_print_log("Cannot draw data by trusty prng.\n");
    exit(1);
  }

  return data;
}

// Draw random data to fill bytes with the given length.
int draw_data_by_trusty_prng(size_t len_bytes, uint8_t *bytes) {
  int rlt;

  rlt = fuz_draw_random(bytes, len_bytes);
  if (rlt != NO_ERROR) {
    fuz_print_log("Failed to draw data, result=%d\n", rlt);
    return -1;
  }

  return 0;
}

// Draw a random length(bytes) for random data.
// Range is [0, MAX_BUF_SIZE).
int draw_len_by_trusty_prng(size_t *len_bytes) {
  int rlt = fuz_draw_random((uint8_t *)len_bytes, sizeof(size_t));
  if (rlt != NO_ERROR) {
    fuz_print_log("Failed to trusty_rng_hw_rand, result=%d\n", rlt);
    return -1;
  }

  *len_bytes %= MAX_BUF_SIZE;

  return 0;
}

/*
 * OR a random number of bits into a mask.
 */
uint32_t get_rand_bitmask(unsigned int num, uint32_t *values) {
  uint32_t i;
  uint32_t mask = 0;
  uint32_t bits;

  bits = fuz_rnd() % (num + 1); /* num of bits to OR */
  if (bits == 0)
    return mask;

  for (i = 0; i < bits; i++) {
    mask |= values[fuz_rnd() % num];
  }

  return mask;
}
