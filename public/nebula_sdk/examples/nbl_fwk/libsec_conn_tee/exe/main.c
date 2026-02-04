#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sec_srv.h>

#define LOG_TAG "sec_srv_test"
#define SRV_NAME "com.nebula.test.sec_srv"

static uint _tests_total  = 0; /* Number of conditions checked */
static uint _tests_failed = 0; /* Number of conditions failed  */

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

static void _on_client_connect(sec_conn_t *conn) {
	void *buf = malloc(0x100000);
	sec_conn_set_priv(conn, buf);
	TLOGI("Client connected.\n");
}

static void _on_handle_msg(sec_conn_t *conn) {
	void *buf = sec_conn_get_priv(conn);
	size_t size = 0;
	int rc = -1;

	_TEST_BEGIN(__func__)

	_EXPECT_NOT_NULL(buf, "check ta msg buffer");

	rc = sec_conn_receive(conn, buf, &size, 0x100000);
	_EXPECT_EQ(0, rc, "check return code");
	_EXPECT_EQ(1, _check_test_msg(buf, size), "check send data");
	if (rc) {
		printf("Receive failed (%d).\n", rc);
		goto failed;
	}

	memset(buf, 0, size);
	_fill_test_msg(buf, size);
	rc = sec_conn_reply(conn, buf, size);
	_EXPECT_EQ(0, rc, "check return code");
	if (rc) {
		printf("Reply failed (%d).\n", rc);
		goto failed;
	}

failed:
	_TEST_END
}

static void _on_client_disconnect(sec_conn_t *conn) {
	void *buf = sec_conn_get_priv(conn);
	free(buf);
	TLOGI("Client disconnected.\n");
}

static sec_srv_desc_t _service = {
	.name = SRV_NAME,
	.on_conn_handler = _on_client_connect,
	.msg_handler = _on_handle_msg,
	.on_disconn_handler = _on_client_disconnect
};

int main()
{
	sec_srv_start(&_service);
    return 0;
}
