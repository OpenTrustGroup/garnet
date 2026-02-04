// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved
#include "test_sm2.h"

#include <stdlib.h>

#include <nbl_sm2.h>
#include <nbl_sm3.h>

#include "unittest.h"

static const uint8_t gmt_003_part5_b2_id_A[] = {
    /* IDA [16] = "sideA.nebula.com"*/
    0x73, 0x69, 0x64, 0x65, 0x41, 0x2e, 0x6e, 0x65,
    0x62, 0x75, 0x6c, 0x61, 0x2e, 0x63, 0x6f, 0x6d};
static const uint8_t gmt_003_part5_b2_id_B[] = {
    /* IDB [16] = "sideB.nebula.com"*/
    0x73, 0x69, 0x64, 0x65, 0x42, 0x2e, 0x6e, 0x65,
    0x62, 0x75, 0x6c, 0x61, 0x2e, 0x63, 0x6f, 0x6d};

//////////////////////////////////////////////
// Below test case simulates "sm2 derive key".
// SM2 keypair is created by ECC.
// Side A is initiator, and side B is responder.
// NOTICE: These APIs follow flow of GPTEE's "TEE_DeriveKey",
//         NOT flow of "sm2 specification".
//////////////////////////////////////////////
// A side.

typedef struct {
  nbl_ecc_keypair_t key;
  nbl_ecc_public_key_t pub_key;

  nbl_ecc_keypair_t eph_key;
  nbl_ecc_public_key_t eph_pub_key;

  const uint8_t* id;

  uint8_t confirm[NBL_SM2_CONFIRM_DATA_LEN];

  uint8_t shared_secret[NBL_SM2_SHARED_SECRET_KEY_LEN];
} Side;

/**
 * Side should create TWO keypair: one and ephemeral.
 * And set a ID of each side.
 */
static void side_init(Side* side_a, const uint8_t* id) {
  int rlt;

  // -------------
  rlt = nbl_sm_alloc_ecc_keypair(&side_a->key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }
  side_a->key.curve = NBL_ECC_CURVE_SM2;

  rlt = nbl_sm_gen_ecc_keypair(&side_a->key, NBL_SM2_ECC_PRIV_KEY_LEN_BITS);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  rlt = nbl_sm_alloc_ecc_public_key(&side_a->pub_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  rlt = nbl_sm_ecc_keypair_2_public_key(&side_a->key, &side_a->pub_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  //----------------

  rlt = nbl_sm_alloc_ecc_keypair(&side_a->eph_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }
  side_a->eph_key.curve = NBL_ECC_CURVE_SM2;

  rlt = nbl_sm_gen_ecc_keypair(&side_a->eph_key, NBL_SM2_ECC_PRIV_KEY_LEN_BITS);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  rlt = nbl_sm_alloc_ecc_public_key(&side_a->eph_pub_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }
  side_a->eph_pub_key.curve = NBL_ECC_CURVE_SM2;

  rlt = nbl_sm_ecc_keypair_2_public_key(&side_a->eph_key, &side_a->eph_pub_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  // --------------------

  side_a->id = id;

  memset(side_a->confirm, 0, sizeof(side_a->confirm));

  memset(side_a->shared_secret, 0, sizeof(side_a->shared_secret));
}

static void test_sm2_a_1(Side* side_a,
                         nbl_ecc_public_key_t* peer_pub_key,
                         nbl_ecc_public_key_t* peer_eph_pub_key,
                         const uint8_t* peer_id) {
  int rlt;

  nbl_sm2_kep_parms_t a_param;
  memset(&a_param, 0, sizeof(a_param));

  a_param.is_initiator = true;          // flag = initiator
  a_param.out = side_a->shared_secret;  // [out] share secret key.
  a_param.out_len = sizeof(side_a->shared_secret);
  a_param.initiator_id = side_a->id;  // self id
  a_param.initiator_id_len = NBL_SM2_SIDE_ID_LEN;
  a_param.responder_id = peer_id;  // peer id
  a_param.responder_id_len = NBL_SM2_SIDE_ID_LEN;
  // a_param.conf_in = NULL;		// Now no confirm data.
  // a_param.conf_in_len = 0;
  a_param.conf_out = side_a->confirm;  // [out] confirm data.
  a_param.conf_out_len = sizeof(side_a->confirm);

  rlt = nbl_sm2_kep_derive(&side_a->key, &side_a->eph_key, peer_pub_key,
                           peer_eph_pub_key, &a_param);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  return;

failed:
  exit(1);
}

static void test_sm2_b_1(Side* side_b,
                         nbl_ecc_public_key_t* peer_pub_key,
                         nbl_ecc_public_key_t* peer_eph_pub_key,
                         const uint8_t* peer_id,
                         const uint8_t* conf_A,
                         size_t conf_A_len) {
  int rlt;

  nbl_sm2_kep_parms_t a_param;
  memset(&a_param, 0, sizeof(a_param));

  a_param.is_initiator = false;         // flag = responder
  a_param.out = side_b->shared_secret;  // [out] share secret key
  a_param.out_len = sizeof(side_b->shared_secret);
  a_param.initiator_id = peer_id;  // peer id, because side A is initiator.
  a_param.initiator_id_len = NBL_SM2_SIDE_ID_LEN;
  a_param.responder_id = side_b->id;  // my id
  a_param.responder_id_len = NBL_SM2_SIDE_ID_LEN;
  a_param.conf_in = conf_A;  // confirm data from side A.
  a_param.conf_in_len = conf_A_len;
  a_param.conf_out = side_b->confirm;  // [out] confirm data created by side B.
  a_param.conf_out_len = sizeof(side_b->confirm);

  rlt = nbl_sm2_kep_derive(&side_b->key, &side_b->eph_key, peer_pub_key,
                           peer_eph_pub_key, &a_param);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  return;

failed:
  exit(1);
}

static void test_sm2_a_2(Side* side_a,
                         nbl_ecc_public_key_t* peer_pub_key,
                         nbl_ecc_public_key_t* peer_eph_pub_key,
                         const uint8_t* peer_id,
                         uint8_t conf_B[32],
                         size_t conf_B_len) {
  int rlt;

  nbl_sm2_kep_parms_t a_param;
  memset(&a_param, 0, sizeof(a_param));

  a_param.is_initiator = true;  // flag = initiator
  a_param.out = NULL;           // NO NEED
  a_param.out_len = 0;
  a_param.initiator_id = side_a->id;  // self id
  a_param.initiator_id_len = NBL_SM2_SIDE_ID_LEN;
  a_param.responder_id = peer_id;  // peer id
  a_param.responder_id_len = NBL_SM2_SIDE_ID_LEN;
  a_param.conf_in = conf_B;  // confirm data created by side B.
  a_param.conf_in_len = conf_B_len;
  // a_param.conf_out = NULL;		// NO NEED
  // a_param.conf_out_len = 0;

  rlt = nbl_sm2_kep_derive(&side_a->key, &side_a->eph_key, peer_pub_key,
                           peer_eph_pub_key, &a_param);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  return;

failed:
  exit(1);
}

/**
 * Test SM2 derive key as standard flow. And keys are created by gen function.
 */
void test_sm2_kep_derive_by_gen() {
  Side side_a;
  Side side_b;

  // Side A/B : create keypair and ephemeral keypair of both sides.
  side_init(&side_a, gmt_003_part5_b2_id_A);
  side_init(&side_b, gmt_003_part5_b2_id_B);

  // Side A as initiator,
  // 1. get "public key in keypair and ephemeral keypair of B" and "id of B".
  // 2. create share secret key and confirm data.
  test_sm2_a_1(&side_a, &side_b.pub_key, &side_b.eph_pub_key, side_b.id);

  // Side B as responder.
  // 1. get "public key in keypair and ephemeral keypair of A" and "id of A".
  // 2. create share secrete key and confirm data.
  // 3. At the same time, confirm by confirm data from A.
  test_sm2_b_1(&side_b, &side_a.pub_key, &side_a.eph_pub_key, side_a.id,
               side_a.confirm, sizeof(side_a.confirm));

  // Side A as initiator.
  // 1. get "confirm data" from B. (Side A has known public keys from Side B.)
  // 2. confirm by confirm data from B.
  test_sm2_a_2(&side_a, &side_b.pub_key, &side_b.eph_pub_key, side_b.id,
               side_b.confirm, sizeof(side_b.confirm));

  CHECK_BUF_EQ(side_a.shared_secret, side_b.shared_secret,
               sizeof(side_a.shared_secret), "shared secret key");

  // show secret data.
  printf("\"%s\" shared secret key:\n", __func__);
  for (int i = 0; i < NBL_SM2_SHARED_SECRET_KEY_LEN; i++) {
    printf("%#x ", side_a.shared_secret[i]);
    if (i % 8 == 7) {
      printf("\n");
    }
  }

  return;

failed:
  exit(1);
}
