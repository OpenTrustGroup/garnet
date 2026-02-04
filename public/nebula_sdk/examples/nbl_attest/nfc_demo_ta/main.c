// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <nbl_attest/attest_api.h>
#include <nbl_fwk/err.h>
#include <nbl_fwk/log.h>

#define LOG_TAG "nfc_demo_ta"

#define TEST_BUF_SIZE 64
#define TEST_PATTERN 0xaa
#define EXPECT_PATTERN 0xab

static int nfc_mock_demo() {
  int rc;

  uint8_t data_in[TEST_BUF_SIZE];
  uint8_t data_out[TEST_BUF_SIZE];
  uint32_t data_out_size = TEST_BUF_SIZE;
  uint32_t i;

  rc = nbl_nfc_connect(NFC_TECH_MOCK, 20, 1, "200.01");
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("mock:failed (%d) to connect to nfc tag\n", rc);
    return rc;
  }

  memset(data_in, TEST_PATTERN, TEST_BUF_SIZE);
  memset(data_out, 0, TEST_BUF_SIZE);
  rc = nbl_nfc_transceive(data_in, TEST_BUF_SIZE, data_out, &data_out_size);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("mock:failed (%d) to transceive nfc data\n", rc);
    goto fail;
  }

  if (data_out_size != TEST_BUF_SIZE) {
    NBL_LOGE("mock:unexpected data out size: %u\n", data_out_size);
    rc = NBL_ERR_BAD_LEN;
    goto fail;
  }

  for (i = 0; i < TEST_BUF_SIZE; i++) {
    if (data_out[i] != EXPECT_PATTERN) {
      NBL_LOGE("mock:unexpected nfc data: out[%u]=%u\n", i, data_out[i]);
      rc = NBL_ERR_IO;
      break;
    }
  }

fail:
  nbl_nfc_disconnect();
  return rc;
}

static int nfc_nfca_demo() {
  int rc;
  uint8_t buf[16] = {0x5A, 0xFF, 0xFF, 0xFF};
  uint8_t compare[16] = {0xB2};
  uint32_t data_out_size;

  rc = nbl_nfc_connect(NFC_TECH_NFC_A, 60, 1, "3000.01");
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("nfca:failed (%d) to connect to nfc tag\n", rc);
    return rc;
  }

  rc = nbl_nfc_transceive(buf, 4, buf, &data_out_size);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("nfca:failed (%d) to transceive nfc data\n", rc);
    goto fail;
  }

  if (data_out_size != 16) {
    NBL_LOGE("nfca:unexpected data out size: %u\n", data_out_size);
    rc = NBL_ERR_BAD_LEN;
    goto fail;
  }

  if(memcmp(buf, compare, 16) != 0) {
    NBL_LOGE("nfca:unexpected nfc data.\n");
    rc = NBL_ERR_IO;
  }

fail:
  nbl_nfc_disconnect();
  return rc;
}


int main(int argc, char** argv) {
  NBL_LOGI("Start NFC demo TA\n");

  nfc_mock_demo();
  nfc_nfca_demo();

  NBL_LOGI("Leave NFC demo TA\n");

  return 0;
}
