#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <zircon/syscalls.h>

#include <nsc_nss.h>

#include <lib/rng/trusty_rng.h>
#include <uapi/err.h>

/**
 * Use RANDOM API to create test data.
 * 1. Example for how to use RADNOM API: trusty soft/hw, random & rand, zircon
 * syscall, boringssl.
 * 2. Create test data for NIST assess.
 * 3. If data is binary, length of bits is 32bits(4bytes) align.
 *    If data is text, '0' or '1' should be ONLY ASCII value in file. NO 32 bits
 * align.
 */

/////////////////////////////////////////////////
// Write data to file.

static const int WRITE_BUF_SIZE = 64 * 1024;
static char* write_buf = NULL;
static size_t write_buf_len = 0;

static size_t write_buf_total = 0;

int write_end_to_file(int fd) {
  int rlt;

  rlt = nbl_nsc_write(fd, write_buf, write_buf_len);
  if (rlt == -1) {
    printf("Cannot write data into file. errno=%d\n", nbl_nsc_get_errno());
    return -1;
  }

  write_buf_total += write_buf_len;
  // printf("total=%zu\n", write_buf_total);

  write_buf_len = 0;

  return 0;
}

int write_byte_to_file(int fd, char ch, size_t len_bits) {
  char MASK = 1;
  char one = 0;
  int rlt;

  if (write_buf == NULL) {
    write_buf = (char*)malloc(WRITE_BUF_SIZE);
    write_buf_len = 0;
  }

  if (len_bits > 8) {
    printf("length(%zu) of bits is over 8.\n", len_bits);
    return -1;
  }

  for (size_t i = 0; i < len_bits; i++) {
    if (ch & MASK) {
      one = '1';
    } else {
      one = '0';
    }

    if (write_buf_len >= WRITE_BUF_SIZE) {
      rlt = write_end_to_file(fd);
      if (rlt != 0) {
        return -1;
      }
    }
    write_buf[write_buf_len] = one;
    write_buf_len++;

    MASK <<= 1;
  }
  return 0;
}

//////////////////////////////////////////////////
// trusty PSESUDO-RANDOM

int start_by_trusty_prng() {
  int rlt;

  time_t seed = time(0);

  rlt = trusty_rng_add_entropy((unsigned char*)&seed, sizeof(seed));
  if (rlt != NO_ERROR)
    return -1;

  return 0;
}

int draw_by_trusty_prng(int fd, size_t len_bits) {
  const int BUF_LEN = 128;  // other value is OK.
  uint8_t buf[BUF_LEN];
  int rlt;

  while (len_bits > 0) {
    rlt = trusty_rng_secure_rand(buf, sizeof(buf));
    if (rlt != NO_ERROR) {
      printf("Failed to trusty_rng_hw_rand, result=%d\n", rlt);
      return -1;
    }
    for (size_t i = 0; i < BUF_LEN; i++) {
      if (len_bits >= 8) {
        rlt = write_byte_to_file(fd, buf[i], 8);
      } else {
        rlt = write_byte_to_file(fd, buf[i], len_bits);
      }
      if (rlt != 0) {
        return -1;
      }

      if (len_bits > 8)
        len_bits -= 8;
      else
        len_bits = 0;
    }
  }

  return 0;
}

//////////////////////////////////////////////////
// trusty Hardware API

int start_by_trusty_hw() {
  // Don't need seed.
  return 0;
}

int draw_by_trusty_hw(int fd, size_t len_bits) {
  const int BUF_LEN = 128;  // other value is OK.
  uint8_t buf[BUF_LEN];
  int rlt;

  while (len_bits > 0) {
    rlt = trusty_rng_hw_rand(buf, sizeof(buf));
    if (rlt != NO_ERROR) {
      printf("Failed to trusty_rng_hw_rand, result=%d\n", rlt);
      return -1;
    }
    for (size_t i = 0; i < BUF_LEN; i++) {
      if (len_bits >= 8) {
        rlt = write_byte_to_file(fd, buf[i], 8);
      } else {
        rlt = write_byte_to_file(fd, buf[i], len_bits);
      }
      if (rlt != 0) {
        return -1;
      }

      if (len_bits > 8)
        len_bits -= 8;
      else
        len_bits = 0;
    }
  }
  return 0;
}

//////////////////////////////////////////////////
// RAND is RANDOM, so don't need a test.
int start_by_random() {
  srandom(time(0));
  return 0;
}

int draw_by_random(int fd, size_t len_bits) {
  // In 64bits system, long is 8bytes, range is [0, RAND_MAX].
  // RAND_MAX is 0x7fffffff, valid bits is 31 bits.

  for (size_t i = 0; i < len_bits; i++) {
    long r = random();
    char* buf = (char*)r;
    for (int i = 0; i < 4; i++) {
      if (i == 3) {
        write_byte_to_file(fd, buf[i], 7);
      } else {
        write_byte_to_file(fd, buf[i], 8);
      }
    }
  }

  return 0;
}

//////////////////////////////////////////////////
// zx_cprng_draw

int start_by_zx_cprng_draw() {
  // On system start, zx_cprng_draw get entropy, so here don't need a new
  // entropy.
  return 0;
}

int draw_by_zx_cprng_draw(int fd, size_t len_bits) {
  char buf[ZX_CPRNG_DRAW_MAX_LEN];  // ZX_CPRNG_DRAW_MAX_LEN = 256
  int rlt;

  while (len_bits > 0) {
    size_t actual;
    zx_status_t status = zx_cprng_draw(buf, ZX_CPRNG_DRAW_MAX_LEN, &actual);
    if (status != ZX_OK) {
      printf("Failed to zx_cprng_draw, zx status=%d\n", status);
      return -1;
    }
    for (size_t i = 0; i < actual; i++) {
      if (len_bits >= 8) {
        rlt = write_byte_to_file(fd, buf[i], 8);
      } else {
        rlt = write_byte_to_file(fd, buf[i], len_bits);
      }
      if (rlt != 0) {
        return -1;
      }

      if (len_bits > 8)
        len_bits -= 8;
      else
        len_bits = 0;
    }
  }
  return 0;
}

//////////////////////////////////////////////////

typedef int (*start_by_x_t)();
// len : count of bits.
typedef int (*draw_by_x_t)(int fd, size_t len_bits);

typedef struct {
  start_by_x_t start_by_x;
  draw_by_x_t draw_by_x;
} RAND_FUNC;

RAND_FUNC rand_func_list[] = {
    {start_by_zx_cprng_draw, draw_by_zx_cprng_draw},
    {start_by_random, draw_by_random},
    {start_by_trusty_hw, draw_by_trusty_hw},
    {start_by_trusty_prng, draw_by_trusty_prng},
};

int main(int argc, char* argv[]) {
  // Min value of bits stream is 1000,000.
  int BIT_COUNT = 1000000;
  int N = 10;
  int TYPE = 3;
  // In gun libc, RAND_MAX = 0x7fffffff = 31 bits.
  // int ONE_RAND_LEN_BITS = 31;
  int rlt = 0;

  int fd = -1;

  rlt = nbl_nsc_start(NULL);
  if (rlt != 0) {
    printf("Cannot start nsc, rlt=%d\n", rlt);
    goto end;
  }

  fd = nbl_nsc_open("1.data", O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    printf("Cannot open file. errno=%d\n", nbl_nsc_get_errno());
    rlt = -1;
    goto end;
  }
  // printf("RAND_MAX=%x, bits=%d, count=%d\n", RAND_MAX, BIT_COUNT, N);

  for (int n = 0; n < N; n++) {
    // Start new test data.
    rlt = rand_func_list[TYPE].start_by_x();
    if (rlt == -1) {
      printf("Cannot start random\n");
    }

    // Draw new test data.
    rlt = rand_func_list[TYPE].draw_by_x(fd, BIT_COUNT);
    if (rlt != 0) {
      goto end;
    }
  }

  write_end_to_file(fd);

end:
  if (fd != -1)
    nbl_nsc_close(fd);

  nbl_nsc_stop();

  return rlt == 0 ? 0 : 1;
}
