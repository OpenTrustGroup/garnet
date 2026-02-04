// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved
#include "test_sm2.h"

#include <stdlib.h>

#include <nbl_sm2.h>
#include <nbl_sm3.h>

#include "unittest.h"

/*
 * G/MT 0003 (SM2) Part 5 Annex B.2 - key exchange protocol based on elliptic
 * curves
 */
static const uint8_t gmt_003_part5_b2_private_A[] = {
    /* dA[32] */
    0x81, 0xEB, 0x26, 0xE9, 0x41, 0xBB, 0x5A, 0xF1, 0x6D, 0xF1, 0x16,
    0x49, 0x5F, 0x90, 0x69, 0x52, 0x72, 0xAE, 0x2C, 0xD6, 0x3D, 0x6C,
    0x4A, 0xE1, 0x67, 0x84, 0x18, 0xBE, 0x48, 0x23, 0x00, 0x29,
};
static const uint8_t gmt_003_part5_b2_public_xA[] = {
    /* xA[32] */
    0x16, 0x0E, 0x12, 0x89, 0x7D, 0xF4, 0xED, 0xB6, 0x1D, 0xD8, 0x12,
    0xFE, 0xB9, 0x67, 0x48, 0xFB, 0xD3, 0xCC, 0xF4, 0xFF, 0xE2, 0x6A,
    0xA6, 0xF6, 0xDB, 0x95, 0x40, 0xAF, 0x49, 0xC9, 0x42, 0x32,
};
static const uint8_t gmt_003_part5_b2_public_yA[] = {
    /* xA[32] */
    0x4A, 0x7D, 0xAD, 0x08, 0xBB, 0x9A, 0x45, 0x95, 0x31, 0x69, 0x4B,
    0xEB, 0x20, 0xAA, 0x48, 0x9D, 0x66, 0x49, 0x97, 0x5E, 0x1B, 0xFC,
    0xF8, 0xC4, 0x74, 0x1B, 0x78, 0xB4, 0xB2, 0x23, 0x00, 0x7F,
};
static const uint8_t gmt_003_part5_b2_eph_private_A[] = {
    /* rA */
    0xD4, 0xDE, 0x15, 0x47, 0x4D, 0xB7, 0x4D, 0x06, 0x49, 0x1C, 0x44,
    0x0D, 0x30, 0x5E, 0x01, 0x24, 0x00, 0x99, 0x0F, 0x3E, 0x39, 0x0C,
    0x7E, 0x87, 0x15, 0x3C, 0x12, 0xDB, 0x2E, 0xA6, 0x0B, 0xB3,
};
static const uint8_t gmt_003_part5_b2_eph_public_xA[] = {
    /* x1 where (x1, y1) = [rA]G */
    0x64, 0xCE, 0xD1, 0xBD, 0xBC, 0x99, 0xD5, 0x90, 0x04, 0x9B, 0x43,
    0x4D, 0x0F, 0xD7, 0x34, 0x28, 0xCF, 0x60, 0x8A, 0x5D, 0xB8, 0xFE,
    0x5C, 0xE0, 0x7F, 0x15, 0x02, 0x69, 0x40, 0xBA, 0xE4, 0x0E,
};
static const uint8_t gmt_003_part5_b2_eph_public_yA[] = {
    /* y1 where (x1, y1) = [rA]G */
    0x37, 0x66, 0x29, 0xC7, 0xAB, 0x21, 0xE7, 0xDB, 0x26, 0x09, 0x22,
    0x49, 0x9D, 0xDB, 0x11, 0x8F, 0x07, 0xCE, 0x8E, 0xAA, 0xE3, 0xE7,
    0x72, 0x0A, 0xFE, 0xF6, 0xA5, 0xCC, 0x06, 0x20, 0x70, 0xC0,
};
static const uint8_t gmt_003_part5_b2_public_xB[] = {
    /* xB */
    0x6A, 0xE8, 0x48, 0xC5, 0x7C, 0x53, 0xC7, 0xB1, 0xB5, 0xFA, 0x99,
    0xEB, 0x22, 0x86, 0xAF, 0x07, 0x8B, 0xA6, 0x4C, 0x64, 0x59, 0x1B,
    0x8B, 0x56, 0x6F, 0x73, 0x57, 0xD5, 0x76, 0xF1, 0x6D, 0xFB,
};
static const uint8_t gmt_003_part5_b2_private_B[] = {
    /* dB */
    0x78, 0x51, 0x29, 0x91, 0x7D, 0x45, 0xA9, 0xEA, 0x54, 0x37, 0xA5,
    0x93, 0x56, 0xB8, 0x23, 0x38, 0xEA, 0xAD, 0xDA, 0x6C, 0xEB, 0x19,
    0x90, 0x88, 0xF1, 0x4A, 0xE1, 0x0D, 0xEF, 0xA2, 0x29, 0xB5,
};
static const uint8_t gmt_003_part5_b2_public_yB[] = {
    /* yB */
    0xEE, 0x48, 0x9D, 0x77, 0x16, 0x21, 0xA2, 0x7B, 0x36, 0xC5, 0xC7,
    0x99, 0x20, 0x62, 0xE9, 0xCD, 0x09, 0xA9, 0x26, 0x43, 0x86, 0xF3,
    0xFB, 0xEA, 0x54, 0xDF, 0xF6, 0x93, 0x05, 0x62, 0x1C, 0x4D,
};
static const uint8_t gmt_003_part5_b2_eph_private_B[] = {
    /* rB */
    0x7E, 0x07, 0x12, 0x48, 0x14, 0xB3, 0x09, 0x48, 0x91, 0x25, 0xEA,
    0xED, 0x10, 0x11, 0x13, 0x16, 0x4E, 0xBF, 0x0F, 0x34, 0x58, 0xC5,
    0xBD, 0x88, 0x33, 0x5C, 0x1F, 0x9D, 0x59, 0x62, 0x43, 0xD6,
};
static const uint8_t gmt_003_part5_b2_eph_public_xB[] = {
    /* x2 where (x2, y2) = [rB]G */
    0xAC, 0xC2, 0x76, 0x88, 0xA6, 0xF7, 0xB7, 0x06, 0x09, 0x8B, 0xC9,
    0x1F, 0xF3, 0xAD, 0x1B, 0xFF, 0x7D, 0xC2, 0x80, 0x2C, 0xDB, 0x14,
    0xCC, 0xCC, 0xDB, 0x0A, 0x90, 0x47, 0x1F, 0x9B, 0xD7, 0x07,
};
static const uint8_t gmt_003_part5_b2_eph_public_yB[] = {
    /* y2 where (x2, y2) = [rB]G */
    0x2F, 0xED, 0xAC, 0x04, 0x94, 0xB2, 0xFF, 0xC4, 0xD6, 0x85, 0x38,
    0x76, 0xC7, 0x9B, 0x8F, 0x30, 0x1C, 0x65, 0x73, 0xAD, 0x0A, 0xA5,
    0x0F, 0x39, 0xFC, 0x87, 0x18, 0x1E, 0x1A, 0x1B, 0x46, 0xFE,
};
static const uint8_t gmt_003_part5_b2_id_A[] = {
    /* IDA */
    0x32, 0x31, 0x34, 0x33, 0x36, 0x35, 0x37, 0x38,
    0x31, 0x32, 0x33, 0x34, 0x33, 0x34, 0x37, 0x38,
};
static const uint8_t gmt_003_part5_b2_id_B[] = {
    /* IDB */
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
};

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
static void side_a_init(Side* side_a) {
  nbl_sm_init_ecc_keypair(
      &side_a->key, gmt_003_part5_b2_private_A,
      ARRAY_SIZE(gmt_003_part5_b2_private_A) * 8, gmt_003_part5_b2_public_xA,
      ARRAY_SIZE(gmt_003_part5_b2_public_xA) * 8, gmt_003_part5_b2_public_yA,
      ARRAY_SIZE(gmt_003_part5_b2_public_yA) * 8, NBL_ECC_CURVE_SM2);

  nbl_sm_init_ecc_public_key(
      &side_a->pub_key, gmt_003_part5_b2_public_xA,
      ARRAY_SIZE(gmt_003_part5_b2_public_xA) * 8, gmt_003_part5_b2_public_yA,
      ARRAY_SIZE(gmt_003_part5_b2_public_yA) * 8, NBL_ECC_CURVE_SM2);

  nbl_sm_init_ecc_keypair(&side_a->eph_key, gmt_003_part5_b2_eph_private_A,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_private_A) * 8,
                          gmt_003_part5_b2_eph_public_xA,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_public_xA) * 8,
                          gmt_003_part5_b2_eph_public_yA,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_public_yA) * 8,
                          NBL_ECC_CURVE_SM2);

  nbl_sm_init_ecc_public_key(
      &side_a->eph_pub_key, gmt_003_part5_b2_eph_public_xA,
      ARRAY_SIZE(gmt_003_part5_b2_eph_public_xA) * 8,
      gmt_003_part5_b2_eph_public_yA,
      ARRAY_SIZE(gmt_003_part5_b2_eph_public_yA) * 8, NBL_ECC_CURVE_SM2);

  side_a->id = gmt_003_part5_b2_id_A;

  memset(side_a->confirm, 0, sizeof(side_a->confirm));

  memset(side_a->shared_secret, 0, sizeof(side_a->shared_secret));
}

static void side_b_init(Side* side_b) {
  nbl_sm_init_ecc_keypair(
      &side_b->key, gmt_003_part5_b2_private_B,
      ARRAY_SIZE(gmt_003_part5_b2_private_B) * 8, gmt_003_part5_b2_public_xB,
      ARRAY_SIZE(gmt_003_part5_b2_public_xB) * 8, gmt_003_part5_b2_public_yB,
      ARRAY_SIZE(gmt_003_part5_b2_public_yB) * 8, NBL_ECC_CURVE_SM2);

  nbl_sm_init_ecc_public_key(
      &side_b->eph_pub_key, gmt_003_part5_b2_eph_public_xB,
      ARRAY_SIZE(gmt_003_part5_b2_eph_public_xB) * 8,
      gmt_003_part5_b2_eph_public_yB,
      ARRAY_SIZE(gmt_003_part5_b2_eph_public_yB) * 8, NBL_ECC_CURVE_SM2);

  nbl_sm_init_ecc_keypair(&side_b->eph_key, gmt_003_part5_b2_eph_private_B,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_private_B) * 8,
                          gmt_003_part5_b2_eph_public_xB,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_public_xB) * 8,
                          gmt_003_part5_b2_eph_public_yB,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_public_yB) * 8,
                          NBL_ECC_CURVE_SM2);

  nbl_sm_init_ecc_public_key(
      &side_b->pub_key, gmt_003_part5_b2_public_xB,
      ARRAY_SIZE(gmt_003_part5_b2_public_xB) * 8, gmt_003_part5_b2_public_yB,
      ARRAY_SIZE(gmt_003_part5_b2_public_yB) * 8, NBL_ECC_CURVE_SM2);

  side_b->id = gmt_003_part5_b2_id_B;

  memset(side_b->confirm, 0, sizeof(side_b->confirm));

  memset(side_b->shared_secret, 0, sizeof(side_b->shared_secret));
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
 * Test SM2 derive key as standard flow.
 */
void test_sm2_kep_derive() {
  Side side_a;
  Side side_b;

  // Side A/B : create keypair and ephemeral keypair of both sides.
  side_a_init(&side_a);
  side_b_init(&side_b);

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
