// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

// Create a SM2 key with openssl API.
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/obj_mac.h>

static void print_array(unsigned char* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    printf("0x%02X ", data[i]);
    if (i % 8 == 7) {
      printf("\n");
    }
  }
}

static void print_array_to_c(const char* name,
                             unsigned char* data,
                             size_t len,
                             size_t bits_len) {
  printf("size_t %s_len_bits = %zu;\n", name, bits_len);
  printf("const char %s[]= {", name);
  for (size_t i = 0; i < len; i++) {
    printf("0x%02X, ", data[i]);
    if (i % 8 == 7) {
      printf("\n");
    }
  }
  printf("};\n");
}

static void create_sm_keys(void) {
  int rlt;
  unsigned char data[32];  // 32bytes is MAX bytes.

  EC_KEY* key = NULL;
  const EC_GROUP* group = NULL;
  BIGNUM* public_x = NULL;
  BIGNUM* public_y = NULL;

  key = EC_KEY_new_by_curve_name(NID_sm2);

  rlt = EC_KEY_generate_key(key);
  if (!rlt) {
    printf("Cannot generate a key.\n");
    goto end;
  }

  const BIGNUM* private_key = EC_KEY_get0_private_key(key);
  const EC_POINT* public_key = EC_KEY_get0_public_key(key);

  group = EC_KEY_get0_group(key);
  public_x = BN_new();
  public_y = BN_new();
  rlt = EC_POINT_get_affine_coordinates(group, public_key, public_x, public_y,
                                        NULL);
  if (!rlt) {
    printf("Cannot convert POINT to 2 bignum. err=%lu\n", ERR_get_error());
    goto end;
  }

  memset(data, 0, sizeof(data));
  BN_bn2bin(private_key, data);
  print_array_to_c("private_key", data, sizeof(data), BN_num_bits(private_key));

  memset(data, 0, sizeof(data));
  BN_bn2bin(public_x, data);
  print_array_to_c("public_x", data, sizeof(data), BN_num_bits(public_x));

  memset(data, 0, sizeof(data));
  BN_bn2bin(public_y, data);
  print_array_to_c("public_y", data, sizeof(data), BN_num_bits(public_y));

end:
  if (key)
    EC_KEY_free(key);
  if (public_x)
    BN_clear_free(public_x);
  if (public_y)
    BN_clear_free(public_y);
}

int main(int argc, char** argv) {
  create_sm_keys();
}
