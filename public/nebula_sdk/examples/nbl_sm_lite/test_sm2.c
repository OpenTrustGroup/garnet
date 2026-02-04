// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved
#include "test_sm2.h"

#include <stdlib.h>

#include <nbl_sm2.h>
#include <nbl_sm3.h>

#include "unittest.h"

/* G/MT 0003 (SM2) Part 5 Annex C.2 - encryption/decryption */
static const uint8_t gmt_0003_part5_c2_sm2_testvector_ptx[19] =
    /* M */
    "encryption standard";
static const uint8_t gmt_0003_part5_c2_sm2_testvector_private[] = {
    /* dB */
    0x39, 0x45, 0x20, 0x8F, 0x7B, 0x21, 0x44, 0xB1, 0x3F, 0x36, 0xE3,
    0x8A, 0xC6, 0xD3, 0x9F, 0x95, 0x88, 0x93, 0x93, 0x69, 0x28, 0x60,
    0xB5, 0x1A, 0x42, 0xFB, 0x81, 0xEF, 0x4D, 0xF7, 0xC5, 0xB8};
static const uint8_t gmt_0003_part5_c2_sm2_testvector_public_x[] = {
    /* xB */
    0x09, 0xF9, 0xDF, 0x31, 0x1E, 0x54, 0x21, 0xA1, 0x50, 0xDD, 0x7D,
    0x16, 0x1E, 0x4B, 0xC5, 0xC6, 0x72, 0x17, 0x9F, 0xAD, 0x18, 0x33,
    0xFC, 0x07, 0x6B, 0xB0, 0x8F, 0xF3, 0x56, 0xF3, 0x50, 0x20};
static const uint8_t gmt_0003_part5_c2_sm2_testvector_public_y[] = {
    /* yB */
    0xCC, 0xEA, 0x49, 0x0C, 0xE2, 0x67, 0x75, 0xA5, 0x2D, 0xC6, 0xEA,
    0x71, 0x8C, 0xC1, 0xAA, 0x60, 0x0A, 0xED, 0x05, 0xFB, 0xF3, 0x5E,
    0x08, 0x4A, 0x66, 0x32, 0xF6, 0x07, 0x2D, 0xA9, 0xAD, 0x13};
static const uint8_t gmt_0003_part5_c2_sm2_testvector_out[] = {
    /* C */
    /* C1 */
    0x04, 0x04, 0xEB, 0xFC, 0x71, 0x8E, 0x8D, 0x17, 0x98, 0x62, 0x04, 0x32,
    0x26, 0x8E, 0x77, 0xFE, 0xB6, 0x41, 0x5E, 0x2E, 0xDE, 0x0E, 0x07, 0x3C,
    0x0F, 0x4F, 0x64, 0x0E, 0xCD, 0x2E, 0x14, 0x9A, 0x73, 0xE8, 0x58, 0xF9,
    0xD8, 0x1E, 0x54, 0x30, 0xA5, 0x7B, 0x36, 0xDA, 0xAB, 0x8F, 0x95, 0x0A,
    0x3C, 0x64, 0xE6, 0xEE, 0x6A, 0x63, 0x09, 0x4D, 0x99, 0x28, 0x3A, 0xFF,
    0x76, 0x7E, 0x12, 0x4D, 0xF0,
    /* C2 */
    0x21, 0x88, 0x6C, 0xA9, 0x89, 0xCA, 0x9C, 0x7D, 0x58, 0x08, 0x73, 0x07,
    0xCA, 0x93, 0x09, 0x2D, 0x65, 0x1E, 0xFA,
    /* C3 */
    0x59, 0x98, 0x3C, 0x18, 0xF8, 0x09, 0xE2, 0x62, 0x92, 0x3C, 0x53, 0xAE,
    0xC2, 0x95, 0xD3, 0x03, 0x83, 0xB5, 0x4E, 0x39, 0xD6, 0x09, 0xD1, 0x60,
    0xAF, 0xCB, 0x19, 0x08, 0xD0, 0xBD, 0x87, 0x66};

/* SM2 encryption/decryption */
static const uint8_t sm2_testvector2_ptx[] =
    /* M */
    "This test vector is used to test encryption then decryption. The "
    "plain text is longer than 64 characters, so it will exercise parts "
    "of the code that are not used for shorter messages.";
static const uint8_t sm2_testvector2_private[] = {
    /* dB */
    0x39, 0x45, 0x20, 0x8F, 0x7B, 0x21, 0x44, 0xB1, 0x3F, 0x36, 0xE3,
    0x8A, 0xC6, 0xD3, 0x9F, 0x95, 0x88, 0x93, 0x93, 0x69, 0x28, 0x60,
    0xB5, 0x1A, 0x42, 0xFB, 0x81, 0xEF, 0x4D, 0xF7, 0xC5, 0xB8};
static const uint8_t sm2_testvector2_public_x[] = {
    /* xB */
    0x09, 0xF9, 0xDF, 0x31, 0x1E, 0x54, 0x21, 0xA1, 0x50, 0xDD, 0x7D,
    0x16, 0x1E, 0x4B, 0xC5, 0xC6, 0x72, 0x17, 0x9F, 0xAD, 0x18, 0x33,
    0xFC, 0x07, 0x6B, 0xB0, 0x8F, 0xF3, 0x56, 0xF3, 0x50, 0x20};
static const uint8_t sm2_testvector2_public_y[] = {
    /* yB */
    0xCC, 0xEA, 0x49, 0x0C, 0xE2, 0x67, 0x75, 0xA5, 0x2D, 0xC6, 0xEA,
    0x71, 0x8C, 0xC1, 0xAA, 0x60, 0x0A, 0xED, 0x05, 0xFB, 0xF3, 0x5E,
    0x08, 0x4A, 0x66, 0x32, 0xF6, 0x07, 0x2D, 0xA9, 0xAD, 0x13};
static const uint8_t sm2_testvector2_out[] = {
    0x00, /* Not used -- test runs in encrypt/decrypt mode */
};

/*
 * G/MT 0003 (SM2) Part 5 Annex A.2 - SM2 digital signature based on
 * elliptic curves
 */
static const uint8_t gmt_003_part5_a2_ptx[] = {
    /* ZA = SM3(ENTLA || IDA || a || b || xG || yG || xA || yA) */
    0xB2, 0xE1, 0x4C, 0x5C, 0x79, 0xC6, 0xDF, 0x5B, 0x85, 0xF4, 0xFE, 0x7E,
    0xD8, 0xDB, 0x7A, 0x26, 0x2B, 0x9D, 0xA7, 0xE0, 0x7C, 0xCB, 0x0E, 0xA9,
    0xF4, 0x74, 0x7B, 0x8C, 0xCD, 0xA8, 0xA4, 0xF3,
    /* M */
    0x6D, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x20, 0x64, 0x69, 0x67, 0x65,
    0x73, 0x74};
static const uint8_t gmt_003_part5_a2_private[] = {
    /* dA */
    0x39, 0x45, 0x20, 0x8F, 0x7B, 0x21, 0x44, 0xB1, 0x3F, 0x36, 0xE3,
    0x8A, 0xC6, 0xD3, 0x9F, 0x95, 0x88, 0x93, 0x93, 0x69, 0x28, 0x60,
    0xB5, 0x1A, 0x42, 0xFB, 0x81, 0xEF, 0x4D, 0xF7, 0xC5, 0xB8,
};
static const uint8_t gmt_003_part5_a2_public_x[] = {
    /* xA */
    0x09, 0xF9, 0xDF, 0x31, 0x1E, 0x54, 0x21, 0xA1, 0x50, 0xDD, 0x7D,
    0x16, 0x1E, 0x4B, 0xC5, 0xC6, 0x72, 0x17, 0x9F, 0xAD, 0x18, 0x33,
    0xFC, 0x07, 0x6B, 0xB0, 0x8F, 0xF3, 0x56, 0xF3, 0x50, 0x20,
};
static const uint8_t gmt_003_part5_a2_public_y[] = {
    /* yA */
    0xCC, 0xEA, 0x49, 0x0C, 0xE2, 0x67, 0x75, 0xA5, 0x2D, 0xC6, 0xEA,
    0x71, 0x8C, 0xC1, 0xAA, 0x60, 0x0A, 0xED, 0x05, 0xFB, 0xF3, 0x5E,
    0x08, 0x4A, 0x66, 0x32, 0xF6, 0x07, 0x2D, 0xA9, 0xAD, 0x13,
};
static const uint8_t gmt_003_part5_a2_out[] = {
    /* r */
    0xF5,
    0xA0,
    0x3B,
    0x06,
    0x48,
    0xD2,
    0xC4,
    0x63,
    0x0E,
    0xEA,
    0xC5,
    0x13,
    0xE1,
    0xBB,
    0x81,
    0xA1,
    0x59,
    0x44,
    0xDA,
    0x38,
    0x27,
    0xD5,
    0xB7,
    0x41,
    0x43,
    0xAC,
    0x7E,
    0xAC,
    0xEE,
    0xE7,
    0x20,
    0xB3,
    /* s */
    0xB1,
    0xB6,
    0xAA,
    0x29,
    0xDF,
    0x21,
    0x2F,
    0xD8,
    0x76,
    0x31,
    0x82,
    0xBC,
    0x0D,
    0x42,
    0x1C,
    0xA1,
    0xBB,
    0x90,
    0x38,
    0xFD,
    0x1F,
    0x7F,
    0x42,
    0xD4,
    0x84,
    0x0B,
    0x69,
    0xC4,
    0x85,
    0xBB,
    0xC1,
    0xAA,
};

/*
 * G/MT 0003 (SM2) Part 5 Annex B.2 - key exchange protocol based on elliptic
 * curves
 */
static const uint8_t gmt_003_part5_b2_private_A[] = {
    /* dA */
    0x81, 0xEB, 0x26, 0xE9, 0x41, 0xBB, 0x5A, 0xF1, 0x6D, 0xF1, 0x16,
    0x49, 0x5F, 0x90, 0x69, 0x52, 0x72, 0xAE, 0x2C, 0xD6, 0x3D, 0x6C,
    0x4A, 0xE1, 0x67, 0x84, 0x18, 0xBE, 0x48, 0x23, 0x00, 0x29,
};
static const uint8_t gmt_003_part5_b2_public_xA[] = {
    /* xA */
    0x16, 0x0E, 0x12, 0x89, 0x7D, 0xF4, 0xED, 0xB6, 0x1D, 0xD8, 0x12,
    0xFE, 0xB9, 0x67, 0x48, 0xFB, 0xD3, 0xCC, 0xF4, 0xFF, 0xE2, 0x6A,
    0xA6, 0xF6, 0xDB, 0x95, 0x40, 0xAF, 0x49, 0xC9, 0x42, 0x32,
};
static const uint8_t gmt_003_part5_b2_public_yA[] = {
    /* xA */
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
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
};
static const uint8_t gmt_003_part5_b2_id_B[] = {
    /* IDB */
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
};
static const uint8_t gmt_003_part5_b2_conf_A[] = {
    /* S2 = SA */
    0x18, 0xC7, 0x89, 0x4B, 0x38, 0x16, 0xDF, 0x16, 0xCF, 0x07, 0xB0,
    0x5C, 0x5E, 0xC0, 0xBE, 0xF5, 0xD6, 0x55, 0xD5, 0x8F, 0x77, 0x9C,
    0xC1, 0xB4, 0x00, 0xA4, 0xF3, 0x88, 0x46, 0x44, 0xDB, 0x88,
};
static const uint8_t gmt_003_part5_b2_conf_B[] = {
    /* S1 = SB */
    0xD3, 0xA0, 0xFE, 0x15, 0xDE, 0xE1, 0x85, 0xCE, 0xAE, 0x90, 0x7A,
    0x6B, 0x59, 0x5C, 0xC3, 0x2A, 0x26, 0x6E, 0xD7, 0xB3, 0x36, 0x7E,
    0x99, 0x83, 0xA8, 0x96, 0xDC, 0x32, 0xFA, 0x20, 0xF8, 0xEB,
};
static const uint8_t gmt_003_part5_b2_shared_secret[] = {
    /* KA = KB */
    0x6C, 0x89, 0x34, 0x73, 0x54, 0xDE, 0x24, 0x84,
    0xC6, 0x0B, 0x4A, 0xB1, 0xFD, 0xE4, 0xC6, 0xE5,
};

/**
 * encrypt and decrypt
 * Cannot compare cipher data with result of another encrypt,
 * because asymmetric encrypt data is different with each other.
 */
void test_sm2_pke() {
  int rlt;
  uint8_t* rlt_out = NULL;
  size_t rlt_len = 0;

  uint8_t* rlt_out2 = NULL;
  size_t rlt_len2 = 0;

  nbl_ecc_public_key_t public_key;
  nbl_ecc_keypair_t keypair;

  rlt = nbl_sm_init_ecc_public_key(
      &public_key, sm2_testvector2_public_x,
      ARRAY_SIZE(sm2_testvector2_public_x) * 8, sm2_testvector2_public_y,
      ARRAY_SIZE(sm2_testvector2_public_y) * 8, NBL_ECC_CURVE_SM2);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  rlt = nbl_sm_init_ecc_keypair(
      &keypair, sm2_testvector2_private,
      ARRAY_SIZE(sm2_testvector2_private) * 8, sm2_testvector2_public_x,
      ARRAY_SIZE(sm2_testvector2_public_x) * 8, sm2_testvector2_public_y,
      ARRAY_SIZE(sm2_testvector2_public_y) * 8, NBL_ECC_CURVE_SM2);
  if (NBL_SM_SUCCESS != rlt) {
    nbl_sm_destroy_ecc_public_key(&public_key);
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  // If want get actual size of out, can set rlt_len = 65.
  // Only len input is bigger than or equal with 65, and actual size > rlt_len,
  // the function will return actual length.
  rlt_len = 65;
  rlt_out = (uint8_t*)calloc(1, rlt_len);

  // Try first.
  rlt = nbl_sm2_pke_encrypt(&public_key, sm2_testvector2_ptx,
                            ARRAY_SIZE(sm2_testvector2_ptx), rlt_out, &rlt_len);
  if (rlt != NBL_SM_SUCCESS) {
    if (NBL_SM_ERROR_SHORT_BUFFER == rlt) {
      // if return SHORT BUFFER, then rlt_len will be actual size.
      // Reallocate memory of cipher data buffer.
      free(rlt_out);
      rlt_out = (uint8_t*)calloc(1, rlt_len);

      // encrpy again.
      rlt = nbl_sm2_pke_encrypt(&public_key, sm2_testvector2_ptx,
                                ARRAY_SIZE(sm2_testvector2_ptx), rlt_out,
                                &rlt_len);

      if (NBL_SM_SUCCESS != rlt) {
        TLOGI("failed, err=%#x\n", rlt);
        goto failed;
      }
    } else {
      TLOGI("failed, err=%#x\n", rlt);
      goto failed;
    }
  }

  rlt_len2 = ARRAY_SIZE(sm2_testvector2_ptx);
  rlt_out2 = (uint8_t*)calloc(1, rlt_len2);

  // decrypt.
  rlt = nbl_sm2_pke_decrypt(&keypair, rlt_out, rlt_len, rlt_out2, &rlt_len2);
  if (rlt != NBL_SM_SUCCESS) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  CHECK_BUF_EQ(sm2_testvector2_ptx, rlt_out2, rlt_len2, "decrypt");

  free(rlt_out);
  free(rlt_out2);

  nbl_sm_destroy_ecc_public_key(&public_key);
  nbl_sm_destroy_ecc_keypair(&keypair);

  return;

failed:
  if (rlt_out)
    free(rlt_out);
  if (rlt_out2)
    free(rlt_out2);

  nbl_sm_destroy_ecc_public_key(&public_key);
  nbl_sm_destroy_ecc_keypair(&keypair);

  exit(1);
}

/**
 * encrypt and decrypt
 */
void test_sm2_pke_by_gen_keypair() {
  int rlt;
  uint8_t* rlt_out = NULL;
  size_t rlt_len = 0;

  uint8_t* rlt_out2 = NULL;
  size_t rlt_len2 = 0;

  nbl_ecc_public_key_t public_key;
  nbl_ecc_keypair_t keypair;

  rlt = nbl_sm_alloc_ecc_keypair(&keypair);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }
  keypair.curve = NBL_ECC_CURVE_SM2;

  rlt = nbl_sm_gen_ecc_keypair(&keypair, NBL_SM2_ECC_PRIV_KEY_LEN_BITS);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  rlt = nbl_sm_alloc_ecc_public_key(&public_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  rlt = nbl_sm_ecc_keypair_2_public_key(&keypair, &public_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  // Almostly, length of ciphered data is less than 2 times length of plain
  // text.
  rlt_len = ARRAY_SIZE(sm2_testvector2_ptx) * 2;
  rlt_out = (uint8_t*)calloc(1, rlt_len);

  rlt = nbl_sm2_pke_encrypt(&public_key, sm2_testvector2_ptx,
                            ARRAY_SIZE(sm2_testvector2_ptx), rlt_out, &rlt_len);
  if (rlt != NBL_SM_SUCCESS) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  rlt_len2 = ARRAY_SIZE(sm2_testvector2_ptx);
  rlt_out2 = (uint8_t*)calloc(1, rlt_len2);

  // decrypt.
  rlt = nbl_sm2_pke_decrypt(&keypair, rlt_out, rlt_len, rlt_out2, &rlt_len2);
  if (rlt != NBL_SM_SUCCESS) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  CHECK_BUF_EQ(sm2_testvector2_ptx, rlt_out2, rlt_len2, "decrypt");

  free(rlt_out);
  free(rlt_out2);

  nbl_sm_destroy_ecc_public_key(&public_key);
  nbl_sm_destroy_ecc_keypair(&keypair);

  return;

failed:
  if (rlt_out)
    free(rlt_out);
  if (rlt_out2)
    free(rlt_out2);

  nbl_sm_destroy_ecc_public_key(&public_key);
  nbl_sm_destroy_ecc_keypair(&keypair);

  exit(1);
}

/**
 * Sign and verify
 * algorithms of sign/verify :
 * NBL_SM2_DSA_SM3 : each sign result is different.
 *   Before use, digest plain text by sm3.
 */
void test_sm2_dsa() {
  int rlt;
  nbl_ecc_keypair_t keypair;
  nbl_ecc_public_key_t public_key;
  nbl_sm3_context_t ctx;

  size_t rlt_len = 0;
  uint8_t* rlt_out = NULL;

  // prepare public key and key pair.

  rlt = nbl_sm_init_ecc_public_key(
      &public_key, gmt_003_part5_a2_public_x,
      ARRAY_SIZE(gmt_003_part5_a2_public_x) * 8, gmt_003_part5_a2_public_y,
      ARRAY_SIZE(gmt_003_part5_a2_public_y) * 8, NBL_ECC_CURVE_SM2);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  rlt = nbl_sm_init_ecc_keypair(
      &keypair, gmt_003_part5_a2_private,
      ARRAY_SIZE(gmt_003_part5_a2_private) * 8, gmt_003_part5_a2_public_x,
      ARRAY_SIZE(gmt_003_part5_a2_public_x) * 8, gmt_003_part5_a2_public_y,
      ARRAY_SIZE(gmt_003_part5_a2_public_y) * 8, NBL_ECC_CURVE_SM2);
  if (NBL_SM_SUCCESS != rlt) {
    nbl_sm_destroy_ecc_public_key(&public_key);
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  // Create hash of plain text.
  uint8_t ptx_hash[NBL_SM3_HASH_LEN] = {};
  size_t ptx_hash_len = sizeof(ptx_hash);

  rlt = nbl_sm3_init(&ctx, NBL_SM3_HASH, NULL, 0);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  rlt = nbl_sm3_update(&ctx, gmt_003_part5_a2_ptx,
                       ARRAY_SIZE(gmt_003_part5_a2_ptx));
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  rlt = nbl_sm3_final(&ctx, ptx_hash, ptx_hash_len);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  rlt = nbl_sm3_destroy(&ctx);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  // Users shall known size of signature.
  rlt_len = ARRAY_SIZE(gmt_003_part5_a2_out);
  rlt_out = (uint8_t*)calloc(1, rlt_len);

  // sign
  rlt = nbl_sm2_dsa_sign(&keypair, ptx_hash, ptx_hash_len, rlt_out, &rlt_len);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed2;
  }

  // verify
  rlt =
      nbl_sm2_dsa_verify(&public_key, ptx_hash, ptx_hash_len, rlt_out, rlt_len);
  if (NBL_SM_SUCCESS != rlt) {
    if (NBL_SM_ERROR_SIGNATURE_INVALID != rlt) {
      TLOGI("failed verify, err=%#x\n", rlt);
    } else {
      TLOGI("failed, err=%#x\n", rlt);
    }
    goto failed2;
  }

  free(rlt_out);

  nbl_sm_destroy_ecc_public_key(&public_key);
  nbl_sm_destroy_ecc_keypair(&keypair);

  return;

failed:

  nbl_sm3_destroy(&ctx);

failed2:
  nbl_sm_destroy_ecc_public_key(&public_key);
  nbl_sm_destroy_ecc_keypair(&keypair);

  if (rlt_out)
    free(rlt_out);

  exit(1);
}

void test_sm2_dsa_by_gen_keypair() {
  int rlt;
  nbl_ecc_keypair_t keypair;
  nbl_ecc_public_key_t public_key;
  nbl_sm3_context_t ctx;

  size_t rlt_len = 0;
  uint8_t* rlt_out = NULL;

  const char test_ptx[] = "This message will be sent to public side";

  // create keypair
  rlt = nbl_sm_alloc_ecc_keypair(&keypair);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }
  keypair.curve = NBL_ECC_CURVE_SM2;

  rlt = nbl_sm_gen_ecc_keypair(&keypair, NBL_SM2_ECC_PRIV_KEY_LEN_BITS);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  // create public key.
  rlt = nbl_sm_alloc_ecc_public_key(&public_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  rlt = nbl_sm_ecc_keypair_2_public_key(&keypair, &public_key);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    exit(1);
  }

  // Create hash of plain text.
  uint8_t ptx_hash[NBL_SM3_HASH_LEN] = {};
  size_t ptx_hash_len = sizeof(ptx_hash);

  rlt = nbl_sm3_init(&ctx, NBL_SM3_HASH, NULL, 0);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  rlt = nbl_sm3_update(&ctx, (const uint8_t*)test_ptx, ARRAY_SIZE(test_ptx));
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  rlt = nbl_sm3_final(&ctx, ptx_hash, ptx_hash_len);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  rlt = nbl_sm3_destroy(&ctx);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  // Users shall known size of signature.
  rlt_len = NBL_SM2_SIG_LEN;
  rlt_out = (uint8_t*)calloc(1, rlt_len);

  // sign
  rlt = nbl_sm2_dsa_sign(&keypair, ptx_hash, ptx_hash_len, rlt_out, &rlt_len);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed2;
  }

  // verify
  rlt =
      nbl_sm2_dsa_verify(&public_key, ptx_hash, ptx_hash_len, rlt_out, rlt_len);
  if (NBL_SM_SUCCESS != rlt) {
    if (NBL_SM_ERROR_SIGNATURE_INVALID != rlt) {
      TLOGI("failed verify, err=%#x\n", rlt);
    } else {
      TLOGI("failed, err=%#x\n", rlt);
    }
    goto failed2;
  }

  free(rlt_out);

  nbl_sm_destroy_ecc_public_key(&public_key);
  nbl_sm_destroy_ecc_keypair(&keypair);

  return;

failed:

  nbl_sm3_destroy(&ctx);

failed2:
  nbl_sm_destroy_ecc_public_key(&public_key);
  nbl_sm_destroy_ecc_keypair(&keypair);

  if (rlt_out)
    free(rlt_out);

  exit(1);
}

//////////////////////////////////////////////
// Below test case (test_sm2_kep_a & test_sm2_kep_b)
// simulates "sm2 derive key".
// But these functions ignore communication between side A and B.

//////////////////////////////////////////////
// A side.

void test_sm2_kep_a() {
  int rlt;
  uint8_t conf_A[32] = {};

  nbl_ecc_keypair_t keyA;
  nbl_sm_init_ecc_keypair(
      &keyA, gmt_003_part5_b2_private_A,
      ARRAY_SIZE(gmt_003_part5_b2_private_A) * 8, gmt_003_part5_b2_public_xA,
      ARRAY_SIZE(gmt_003_part5_b2_public_xA) * 8, gmt_003_part5_b2_public_yA,
      ARRAY_SIZE(gmt_003_part5_b2_public_yA) * 8, NBL_ECC_CURVE_SM2);

  nbl_ecc_keypair_t eph_keyA;
  nbl_sm_init_ecc_keypair(&eph_keyA, gmt_003_part5_b2_eph_private_A,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_private_A) * 8,
                          gmt_003_part5_b2_eph_public_xA,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_public_xA) * 8,
                          gmt_003_part5_b2_eph_public_yA,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_public_yA) * 8,
                          NBL_ECC_CURVE_SM2);

  nbl_ecc_public_key_t keyB;
  nbl_sm_init_ecc_public_key(
      &keyB, gmt_003_part5_b2_public_xB,
      ARRAY_SIZE(gmt_003_part5_b2_public_xB) * 8, gmt_003_part5_b2_public_yB,
      ARRAY_SIZE(gmt_003_part5_b2_public_yB) * 8, NBL_ECC_CURVE_SM2);

  nbl_ecc_public_key_t eph_keyB;
  nbl_sm_init_ecc_public_key(&eph_keyB, gmt_003_part5_b2_eph_public_xB,
                             ARRAY_SIZE(gmt_003_part5_b2_eph_public_xB) * 8,
                             gmt_003_part5_b2_eph_public_yB,
                             ARRAY_SIZE(gmt_003_part5_b2_eph_public_yB) * 8,
                             NBL_ECC_CURVE_SM2);

  // A's secret key.
  uint8_t a_out[sizeof(gmt_003_part5_b2_shared_secret)];
  memset(a_out, 0, sizeof(a_out));

  nbl_sm2_kep_parms_t a_param;
  a_param.is_initiator = true;  // init
  a_param.out = a_out;
  a_param.out_len = ARRAY_SIZE(a_out);
  a_param.initiator_id = gmt_003_part5_b2_id_A;
  a_param.initiator_id_len = ARRAY_SIZE(gmt_003_part5_b2_id_A);
  a_param.responder_id = gmt_003_part5_b2_id_B;
  a_param.responder_id_len = ARRAY_SIZE(gmt_003_part5_b2_id_B);
  a_param.conf_in = gmt_003_part5_b2_conf_B;
  a_param.conf_in_len = ARRAY_SIZE(gmt_003_part5_b2_conf_B);
  a_param.conf_out = conf_A;
  a_param.conf_out_len = ARRAY_SIZE(conf_A);

  rlt = nbl_sm2_kep_derive(&keyA, &eph_keyA, &keyB, &eph_keyB, &a_param);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  // a_param.out is shared secret key between A and B.
  CHECK_BUF_EQ(gmt_003_part5_b2_shared_secret, a_param.out, a_param.out_len,
               "A dervie");

  nbl_sm_destroy_ecc_public_key(&keyB);
  nbl_sm_destroy_ecc_public_key(&eph_keyB);
  nbl_sm_destroy_ecc_keypair(&keyA);
  nbl_sm_destroy_ecc_keypair(&eph_keyA);

  return;

failed:
  nbl_sm_destroy_ecc_public_key(&keyB);
  nbl_sm_destroy_ecc_public_key(&eph_keyB);
  nbl_sm_destroy_ecc_keypair(&keyA);
  nbl_sm_destroy_ecc_keypair(&eph_keyA);

  exit(1);
}

void test_sm2_kep_b() {
  int rlt;
  uint8_t conf_B[32] = {};

  nbl_ecc_keypair_t keyB;
  nbl_sm_init_ecc_keypair(
      &keyB, gmt_003_part5_b2_private_B,
      ARRAY_SIZE(gmt_003_part5_b2_private_B) * 8, gmt_003_part5_b2_public_xB,
      ARRAY_SIZE(gmt_003_part5_b2_public_xB) * 8, gmt_003_part5_b2_public_yB,
      ARRAY_SIZE(gmt_003_part5_b2_public_yB) * 8, NBL_ECC_CURVE_SM2);

  nbl_ecc_keypair_t eph_keyB;
  nbl_sm_init_ecc_keypair(&eph_keyB, gmt_003_part5_b2_eph_private_B,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_private_B) * 8,
                          gmt_003_part5_b2_eph_public_xB,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_public_xB) * 8,
                          gmt_003_part5_b2_eph_public_yB,
                          ARRAY_SIZE(gmt_003_part5_b2_eph_public_yB) * 8,
                          NBL_ECC_CURVE_SM2);

  nbl_ecc_public_key_t keyA;
  nbl_sm_init_ecc_public_key(
      &keyA, gmt_003_part5_b2_public_xA,
      ARRAY_SIZE(gmt_003_part5_b2_public_xA) * 8, gmt_003_part5_b2_public_yA,
      ARRAY_SIZE(gmt_003_part5_b2_public_yA) * 8, NBL_ECC_CURVE_SM2);

  nbl_ecc_public_key_t eph_keyA;
  nbl_sm_init_ecc_public_key(&eph_keyA, gmt_003_part5_b2_eph_public_xA,
                             ARRAY_SIZE(gmt_003_part5_b2_eph_public_xA) * 8,
                             gmt_003_part5_b2_eph_public_yA,
                             ARRAY_SIZE(gmt_003_part5_b2_eph_public_yA) * 8,
                             NBL_ECC_CURVE_SM2);

  // A's secret key.
  uint8_t a_out[sizeof(gmt_003_part5_b2_shared_secret)];
  memset(a_out, 0, sizeof(a_out));

  nbl_sm2_kep_parms_t a_param;
  a_param.is_initiator = false;  // response
  a_param.out = a_out;
  a_param.out_len = ARRAY_SIZE(a_out);
  a_param.initiator_id = gmt_003_part5_b2_id_A;
  a_param.initiator_id_len = ARRAY_SIZE(gmt_003_part5_b2_id_A);
  a_param.responder_id = gmt_003_part5_b2_id_B;
  a_param.responder_id_len = ARRAY_SIZE(gmt_003_part5_b2_id_B);
  a_param.conf_in = gmt_003_part5_b2_conf_A;
  a_param.conf_in_len = ARRAY_SIZE(gmt_003_part5_b2_conf_A);
  a_param.conf_out = conf_B;
  a_param.conf_out_len = ARRAY_SIZE(conf_B);

  rlt = nbl_sm2_kep_derive(&keyB, &eph_keyB, &keyA, &eph_keyA, &a_param);
  if (NBL_SM_SUCCESS != rlt) {
    TLOGI("failed, err=%#x\n", rlt);
    goto failed;
  }

  // a_param.out is shared secret key between A and B.
  CHECK_BUF_EQ(gmt_003_part5_b2_shared_secret, a_param.out, a_param.out_len,
               "A dervie");

  nbl_sm_destroy_ecc_public_key(&keyA);
  nbl_sm_destroy_ecc_public_key(&eph_keyA);
  nbl_sm_destroy_ecc_keypair(&keyB);
  nbl_sm_destroy_ecc_keypair(&eph_keyB);

  return;

failed:
  nbl_sm_destroy_ecc_public_key(&keyA);
  nbl_sm_destroy_ecc_public_key(&eph_keyA);
  nbl_sm_destroy_ecc_keypair(&keyB);
  nbl_sm_destroy_ecc_keypair(&eph_keyB);

  exit(1);
}
