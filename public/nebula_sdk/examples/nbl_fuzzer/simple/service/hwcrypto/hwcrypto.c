#include <fuz_common.h>

#include <trusty_std.h>
#include <uapi/err.h>

#include <interface/hwrng/hwrng.h>
#include <lib/hwkey/hwkey.h>
#include <service_utils.h>

#include <zircon/syscalls.h>

#define RPMB_STORAGE_AUTH_KEY_ID "com.android.trusty.storage_auth.rpmb"
#define HWCRYPTO_UNITTEST_KEYBOX_ID "com.android.trusty.hwcrypto.unittest.key32"

static void prepare_hwkey_msg(struct hwkey_msg *msg) {
  fuz_draw_random(msg, sizeof(*msg));
  switch (fuz_rnd() % 3) {
  case 0:
    if (!ONE_IN(10)) {
      memset(&(msg->op_id), 0, sizeof(msg) - 4);
    }
    msg->cmd = HWKEY_DERIVE;
    msg->arg1 = RAND_BOOL() ? HWKEY_KDF_VERSION_BEST : HWKEY_KDF_VERSION_1;
    break;
  case 1:
    if (!ONE_IN(10)) {
      memset(&(msg->op_id), 0, sizeof(msg) - 4);
    }
    msg->cmd = HWKEY_GET_KEYSLOT;
    break;
  case 2:
    break;
  }
}

static void hwkey_send_two_segment_msg(struct hwkey_msg *msg) {
  handle_t chan;
  ipc_msg_t tx_msg;
  iovec_t tx_iov[2];
  int rc;
  uint32_t size;
  void *buf;

  size = fuz_rnd() % 4097;
  if (size == 0) {
    buf = NULL;
  } else {
    if (msg->cmd == HWKEY_GET_KEYSLOT && !ONE_IN(10)) {
      buf =
          RAND_BOOL() ? RPMB_STORAGE_AUTH_KEY_ID : HWCRYPTO_UNITTEST_KEYBOX_ID;
      size = strlen(buf);
    } else {
      buf = malloc(size);
      if (buf == NULL) {
        fuz_print_log("Error: %s %d\n", __func__, __LINE__);
      }
      register_fuz_mem(buf);
      if (RAND_BOOL()) {
        fuz_draw_random(buf, size);
      }
    }
  }

  rc = sync_connect(HWKEY_PORT, 1000);
  if (rc < 0) {
    fuz_print_log("Error:%s %d, rc:%d\n", __func__, __LINE__, rc);
    return;
  }

  chan = rc;
  tx_iov[0].base = msg;
  tx_iov[0].len = sizeof(*msg);
  tx_iov[1].base = buf;
  tx_iov[2].len = size;
  tx_msg.num_iov = 2;
  tx_msg.iov = tx_iov;
  tx_msg.num_handles = 0;
  tx_msg.handles = NULL;
  rc = _trusty_send_msg(chan, &tx_msg);
  if (rc < NO_ERROR) {
    fuz_print_log("Error:%s %d, rc:%d\n", __func__, __LINE__, rc);
  }

  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
  _trusty_close(chan);
}

static void test_hwkey(void *arg) {
  (void)arg;

  if (ONE_IN(10)) {
    fuz_print_log("random");
    send_random_msg(HWKEY_PORT);
  } else {
    struct hwkey_msg msg;
    prepare_hwkey_msg(&msg);
    if (ONE_IN(10)) {
      fuz_print_log("one seg,cmd=%u ", msg.cmd);
      send_msg_to_service(HWKEY_PORT, &msg, sizeof(msg), false, 0);
    } else {
      fuz_print_log("two seg,cmd=%u ", msg.cmd);
      hwkey_send_two_segment_msg(&msg);
    }
  }
}
struct function_description hwcrypto_hwkey_test = {.do_custom_test =
                                                       test_hwkey};

PrepareTest(hwcrypto_hwkey) { return &hwcrypto_hwkey_test; }

static void test_hwrng(void *arg) {
  (void)arg;
  struct hwrng_req req_hdr;

  if (ONE_IN(10)) {
    fuz_print_log("random");
    send_random_msg(HWRNG_PORT);
  } else {
    req_hdr.len = fuz_rnd() % (1 * 1024 * 1024);
    fuz_print_log("rng_len:%x, ", req_hdr.len);
    send_msg_to_service(HWRNG_PORT, &req_hdr, sizeof(req_hdr), RAND_BOOL(),
                        req_hdr.len);
  }
}
struct function_description hwcrypto_hwrng_test = {.do_custom_test =
                                                       test_hwrng};

PrepareTest(hwcrypto_hwrng) { return &hwcrypto_hwrng_test; }
