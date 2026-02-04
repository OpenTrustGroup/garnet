// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved

/**
 * Below source code is example for SM including 2/3/4.
 * The almost interfaces are like GPTEE Cipher SM.
 */

#include "test_ecc_key.h"
#include "test_sm2.h"
#include "test_sm3.h"
#include "test_sm4.h"

#include "nbl_sm_common.h"

#include "unittest.h"

int main(int argc, char* argv[]) {
  // initialize crypto before using sm2/3/4
  int rlt;

  rlt = nbl_sm_init();
  if (rlt != NBL_SM_SUCCESS) {
    TLOGI("Failed initializing sm.\n");
    exit(1);
  }

  test_sm4();

  test_sm3_hash();
  test_sm3_hmac();

  test_ecc_keypair();
  test_ecc_pub_key();
  test_ecc_key_gen();

  test_sm2_pke();
  test_sm2_pke_by_gen_keypair();

  test_sm2_dsa();
  test_sm2_dsa_by_gen_keypair();

  test_sm2_kep_a();
  test_sm2_kep_b();

  test_sm2_kep_derive();
  test_sm2_kep_derive_by_gen();

  TLOGI("Tests are OK.\n\n");
  return 0;

failed:
  return 1;
}
