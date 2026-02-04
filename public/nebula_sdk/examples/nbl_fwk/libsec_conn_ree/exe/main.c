#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sec_client.h>
#include <mcheck.h>
#include <nbl_fwk_ree/err.h>

#define LOG_TAG "sec_srv_test"
#define SRV_NAME "com.nebula.test.sec_srv"

static int _tests_total  = 0; /* Number of conditions checked */
static int _tests_failed = 0; /* Number of conditions failed  */

#define TLOGI(fmt, ...) \
    fprintf(stderr, "%s: %d: " fmt, LOG_TAG, __LINE__,  ## __VA_ARGS__)
/*
 *   Begin and end test macro
 */
#define _TEST_BEGIN(name)                                        \
	int _all_ok = 1;                                    \
	const char *_test = name;                               \
	TLOGI("%s:\n", _test);


#define _TEST_END                                                \
{                                                               \
	if (_all_ok)                                            \
		TLOGI("%s: PASSED\n", _test);                   \
	else {                                                    \
		TLOGI("%s: FAILED\n", _test);                   \
		exit(-1);                                        \
	}                                                        \
}

/*
 * EXPECT_* macros to check test results.
 */
#define _EXPECT_EQ(expected, actual, msg)                        \
{                                                               \
	__typeof__(actual) _e = expected;                       \
	__typeof__(actual) _a = actual;                         \
	_tests_total++;                                         \
	if (_e != _a) {                                         \
		TLOGI("%s: expected " #expected " (%d), "      \
		    "actual " #actual " (%d)\n",               \
		    msg, (int)_e, (int)_a);                     \
		_tests_failed++;                                \
		_all_ok = 0;                                \
	}                                                       \
}

#define _EXPECT_NOT_NULL(actual, msg)                        \
{                                                               \
	__typeof__(actual) _a = actual;                         \
	_tests_total++;                                         \
	if (!_a) {                                         \
		TLOGI("%s: expected not null, "      \
		    "actual " #actual " (%d)\n",               \
		    msg, (int)_a);                     \
		_tests_failed++;                                \
		_all_ok = 0;                                \
	}                                                       \
}

#define _EXPECT_GT(expected, actual, msg)                        \
{                                                               \
	__typeof__(actual) _e = expected;                       \
	__typeof__(actual) _a = actual;                         \
	_tests_total++;                                         \
	if (_e <= _a) {                                         \
		TLOGI("%s: expected " #expected " (%d), "      \
		    "actual " #actual " (%d)\n",                \
		    msg, (int)_e, (int)_a);                     \
		_tests_failed++;                                \
		_all_ok = 0;                                \
	}                                                       \
}

#define _EXPECT_GE_ZERO(actual, msg)                             \
{                                                               \
	__typeof__(actual) _a = actual;                         \
	_tests_total++;                                         \
	if (_a < 0) {                                           \
		TLOGI("%s: expected >= 0 "                     \
		    "actual " #actual " (%d)\n", msg, (int)_a); \
		_tests_failed++;                                \
		_all_ok = 0;                                \
	}                                                       \
}

#define _EXPECT_LE_ZERO(actual, msg)                             \
{                                                               \
	__typeof__(actual) _a = actual;                         \
	_tests_total++;                                         \
	if (_a > 0) {                                           \
		TLOGI("%s: expected <= 0 "                     \
		    "actual " #actual " (%d)\n", msg, (int)_a); \
		_tests_failed++;                                \
		_all_ok = 0;                                \
	}                                                       \
}


#define _EXPECT_GT_ZERO(actual, msg)                             \
{                                                               \
	__typeof__(actual) _a = actual;                         \
	_tests_total++;                                         \
	if (_a <= 0) {                                          \
		TLOGI("%s: expected > 0 "                      \
		    "actual " #actual " (%d)\n", msg, (int)_a); \
		_tests_failed++;                                \
		_all_ok = 0;                                \
	}                                                       \
}

#define _EXPECT_LT_ZERO(actual, msg)                             \
{                                                               \
	__typeof__(actual) _a = actual;                         \
	_tests_total++;                                         \
	if (_a >= 0) {                                          \
		TLOGI("%s: expected < 0 "                      \
		    "actual " #actual " (%d)\n", msg, (int)_a); \
		_tests_failed++;                                \
		_all_ok = 0;                                \
	}                                                       \
}

#define _ABORT_IF(_cond, lbl)                                    \
{                                                               \
	if (_cond) {                                            \
		goto lbl;                                       \
	}                                                       \
}

#define _ABORT_IF_NOT_OK(lbl) _ABORT_IF((!_all_ok), lbl)

static void _fill_test_msg(uint8_t *msg, size_t size) {
	int i;

	for (i = 0; i < size; i++) {
		msg[i] = i % 256;
	}
}

static int _check_test_msg(const uint8_t *buf, size_t size) {
	int i;
	int ok = 1;

	for (i = 0; i < size; i++) {
		if (buf[i] == (i % 256)) {
			continue;
		} else {
			TLOGI("check test msg[%d]=%d failed.\n", i, (int)buf[i]);
			ok = 0;
			break;
		}
	}

	return ok;
}

static void _test_case_001() {
	 _TEST_BEGIN(__func__)

	 sec_conn_t *conn = secc_connect(SRV_NAME);
	 _EXPECT_NOT_NULL(conn, "check connect to ta");

	 secc_disconnect(conn);

	 _TEST_END
}

static void _test_case_002() {
	uint8_t *snd_buf = (uint8_t *)malloc(0x100000);
	size_t snd_size;
	uint8_t *rcv_buf = (uint8_t *)malloc(0x100000);
	size_t rcv_size;
	int rc = -1;

	_TEST_BEGIN(__func__)

	sec_conn_t *conn = secc_connect(SRV_NAME);
	 _EXPECT_NOT_NULL(conn, "check connect to ta");

	 snd_size = 0x10;
	 _fill_test_msg(snd_buf, snd_size);
	 rc = secc_call(conn, snd_buf, snd_size, rcv_buf, &rcv_size, 0x100000);
	 _EXPECT_EQ(0, rc, "check return code");
	 _EXPECT_EQ(snd_size, rcv_size, "check reply size");
	 _EXPECT_EQ(1, _check_test_msg(rcv_buf, rcv_size), "check reply data");

	 snd_size = 3999;
	 _fill_test_msg(snd_buf, snd_size);
	 rc = secc_call(conn, snd_buf, snd_size, rcv_buf, &rcv_size, 0x100000);
	 _EXPECT_EQ(0, rc, "check return code");
	 _EXPECT_EQ(snd_size, rcv_size, "check reply size");
	 _EXPECT_EQ(1, _check_test_msg(rcv_buf, rcv_size), "check reply data");

	 snd_size = 4000;
	 _fill_test_msg(snd_buf, snd_size);
	 rc = secc_call(conn, snd_buf, snd_size, rcv_buf, &rcv_size, 0x100000);
	 _EXPECT_EQ(0, rc, "check return code");
	 _EXPECT_EQ(snd_size, rcv_size, "check reply size");
	 _EXPECT_EQ(1, _check_test_msg(rcv_buf, rcv_size), "check reply data");

	 snd_size = 4001;
	 _fill_test_msg(snd_buf, snd_size);
	 rc = secc_call(conn, snd_buf, snd_size, rcv_buf, &rcv_size, 0x100000);
	 _EXPECT_EQ(0, rc, "check return code");
	 _EXPECT_EQ(snd_size, rcv_size, "check reply size");
	 _EXPECT_EQ(1, _check_test_msg(rcv_buf, rcv_size), "check reply data");

	 snd_size = 0x100000;
	 _fill_test_msg(snd_buf, snd_size);
	 rc = secc_call(conn, snd_buf, snd_size, rcv_buf, &rcv_size, 0x100000);
	 _EXPECT_EQ(0, rc, "check return code");
	 _EXPECT_EQ(snd_size, rcv_size, "check reply size");
	 _EXPECT_EQ(1, _check_test_msg(rcv_buf, rcv_size), "check reply data");

	 secc_disconnect(conn);

	 _TEST_END

	 free(snd_buf);
	 free(rcv_buf);
}

static void _test_case_003() {
	uint8_t *snd_buf = (uint8_t *) malloc(0x100000);
	size_t snd_size;
	uint8_t *rcv_buf = (uint8_t *) malloc(0x100000);
	size_t rcv_size;
	int rc = -1;
	sec_conn_t *conn = NULL;

	_TEST_BEGIN(__func__)

	conn = secc_connect(NULL);
	_EXPECT_EQ(NULL, conn, "check pass srv name NULL");

	secc_disconnect(NULL);

	conn = secc_connect(SRV_NAME);
	_EXPECT_NOT_NULL(conn, "check connect to ta");

	snd_size = 0;
	rc = secc_call(conn, snd_buf, snd_size, rcv_buf, &rcv_size, 0x100000);
	_EXPECT_EQ(0, rc, "check pass snd_size 0");

	snd_size = 0x10;
	_fill_test_msg(snd_buf, snd_size);
	rc = secc_call(NULL, snd_buf, snd_size, rcv_buf, &rcv_size, 0x100000);
	_EXPECT_EQ(NBL_ERR_INVALID_ARGS, rc, "check pass conn NULL");

	snd_size = 3999;
	_fill_test_msg(snd_buf, snd_size);
	rc = secc_call(conn, NULL, snd_size, rcv_buf, &rcv_size, 0x100000);
	_EXPECT_EQ(NBL_ERR_INVALID_ARGS, rc, "check pass snd_buf NULL");

	snd_size = 4000;
	_fill_test_msg(snd_buf, snd_size);
	rc = secc_call(conn, snd_buf, snd_size, NULL, &rcv_size, 0x100000);
	_EXPECT_EQ(NBL_ERR_INVALID_ARGS, rc, "check pass rcv_buf NULL");

	snd_size = 4001;
	_fill_test_msg(snd_buf, snd_size);
	rc = secc_call(conn, snd_buf, snd_size, rcv_buf, NULL, 0x100000);
	_EXPECT_EQ(0, rc, "check rcv_size NULL");
	_EXPECT_EQ(1, _check_test_msg(rcv_buf, snd_size), "check reply data");

	snd_size = 0x100000;
	_fill_test_msg(snd_buf, snd_size);
	rc = secc_call(conn, snd_buf, snd_size, rcv_buf, &rcv_size, 0x100000 - 1);
	_EXPECT_EQ(NBL_ERR_GENERIC, rc, "check insufficient rec_buf");

	secc_disconnect(conn);

	_TEST_END

	free(snd_buf);
	free(rcv_buf);
}

int main()
{
	mtrace();

	_test_case_001();
	_test_case_002();
	_test_case_003();

    return 0;
}
