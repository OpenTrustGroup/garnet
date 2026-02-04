// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved
#include "test_sm2.h"

#include <stdlib.h>

#include <nbl_sm2.h>

#include "unittest.h"

void dbg_print_bn(struct nbl_sm_bignum* bn,
                  const char* prefix,
                  const char* msg) {
  uint8_t* bytes;
  size_t bytes_len, bits_len;

  nbl_sm_bignum_2_bytes(bn, &bytes, &bytes_len, &bits_len);
  printf("\"%s : %s\" : len=%zu(B)/%zu(b)\n", prefix, msg, bytes_len, bits_len);
  for (int i = 0; i < bytes_len; i++) {
    printf("%02x ", bytes[i]);
    if (i % 8 == 7) {
      printf("\n");
    }
  }
  free(bytes);
}

//////////////////////////////////////////////////

void test_ecc_key_alloc_destroy() {
  int rlt;
  nbl_ecc_keypair_t keypair;

  rlt = nbl_sm_alloc_ecc_keypair(&keypair);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    return;
  }

  nbl_sm_destroy_ecc_keypair(&keypair);
}

void test_ecc_keypair() {
  test_ecc_key_alloc_destroy();
}

//////////////////////////////////////////////////

void test_ecc_pub_key_alloc_destroy() {
  int rlt;
  nbl_ecc_public_key_t pub_key;

  rlt = nbl_sm_alloc_ecc_public_key(&pub_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    return;
  }

  nbl_sm_destroy_ecc_public_key(&pub_key);
}

void test_ecc_pub_key() {
  test_ecc_pub_key_alloc_destroy();
}

//////////////////////////////////////////////////

void test_ecc_key_gen() {
  int rlt;
  nbl_ecc_keypair_t keypair;
  nbl_ecc_public_key_t pub_key;

  rlt = nbl_sm_alloc_ecc_keypair(&keypair);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    return;
  }
  keypair.curve = NBL_ECC_CURVE_SM2;

  rlt = nbl_sm_gen_ecc_keypair(&keypair, NBL_SM2_ECC_PRIV_KEY_LEN_BITS);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    return;
  }

  dbg_print_bn(keypair.d, __func__, "keypair d");
  dbg_print_bn(keypair.x, __func__, "keypair x");
  dbg_print_bn(keypair.y, __func__, "keypair y");

  rlt = nbl_sm_alloc_ecc_public_key(&pub_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    return;
  }

  nbl_sm_ecc_keypair_2_public_key(&keypair, &pub_key);

  dbg_print_bn(pub_key.x, __func__, "keypair x");
  dbg_print_bn(pub_key.y, __func__, "keypair y");

  if (pub_key.curve != NBL_ECC_CURVE_SM2) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  nbl_sm_destroy_ecc_keypair(&keypair);
  nbl_sm_destroy_ecc_public_key(&pub_key);
}
