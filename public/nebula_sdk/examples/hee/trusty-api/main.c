// copy from //zircon/trusty/app/gr-ipc-unittest/main/main.c

// Copyright 2018 GoldenRiver Technology Co., Ltd.
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <uapi/err.h>
#include <trusty_std.h>
#include "unittest/unittest.h"

#define LOG_TAG "ipc-unittest-main"

#include <app/ipc_unittest/common.h>
#include <app/ipc_unittest/uuids.h>
#include <trusty/sys/types.h>

/*  */
static uint _tests_total  = 0; /* Number of conditions checked */
static uint _tests_failed = 0; /* Number of conditions failed  */
static handle_t handle_base;   /* base of valid handle range */

// TODO(GR-0003): Not support uuid test except zero_uuid currently.
//static const uuid_t srv_app_uuid = IPC_UNITTEST_SRV_APP_UUID;

/*
 *   Begin and end test macro
 */
#define _TEST_BEGIN(name)                                        \
	bool _all_ok = true;                                    \
	const char *_test = name;                               \
	TLOGI("%s:\n", _test);


#define _TEST_END                                                \
{                                                               \
	if (_all_ok)                                            \
		TLOGI("%s: PASSED\n", _test);                   \
	else                                                    \
		TLOGI("%s: FAILED\n", _test);                   \
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
		_all_ok = false;                                \
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
		_all_ok = false;                                \
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
		_all_ok = false;                                \
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
		_all_ok = false;                                \
	}                                                       \
}

#define _ABORT_IF(_cond, lbl)                                    \
{                                                               \
	if (_cond) {                                            \
		goto lbl;                                       \
	}                                                       \
}

#define _ABORT_IF_NOT_OK(lbl) _ABORT_IF((!_all_ok), lbl)

/****************************************************************************/

/*
 *  Fill specified buffer with incremental pattern
 */
static void fill_test_buf(uint8_t *buf, size_t cnt, uint8_t seed)
{
	if (!buf || !cnt)
		return;

	while (cnt--) {
		*buf++ = seed++;
	}
}

/*
 *  Local wrapper on top of async connect that provides
 *  synchronos connect with timeout.
 */
int sync_connect(const char *path, uint64_t timeout)
{
	int rc;
	uevent_t evt;
	handle_t chan;

	rc = _trusty_connect(path, IPC_CONNECT_ASYNC | IPC_CONNECT_WAIT_FOR_PORT);
	if (rc >= 0) {
		chan = (handle_t) rc;
		rc = _trusty_wait(chan, &evt, timeout);
		if (rc == 0) {
			rc = ERR_BAD_STATE;
			if (evt.handle == chan) {
				if (evt.event & IPC_HANDLE_POLL_READY)
					return chan;

				if (evt.event & IPC_HANDLE_POLL_HUP)
					rc = ERR_CHANNEL_CLOSED;
			}
		}
		_trusty_close(chan);
	}
	return rc;
}


/****************************************************************************/

/*
 *  wait on handle negative test
 */
static void run_wait_negative_test(void)
{
	int rc;
	uevent_t event;
	lk_time_t timeout = 1000;  // 1 sec

	_TEST_BEGIN(__func__);

	/* waiting on invalid handle. */
	rc = _trusty_wait(INVALID_IPC_HANDLE, &event, timeout);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "wait on invalid handle");

	/*
	 *   call wait on an invalid (out of range) handle
	 *
	 *   check handling of the following cases:
	 *     - handle is on the upper boundary of valid handle range
	 *     - handle is above of the upper boundary of valid handle range
	 *     - handle is below of valid handle range
	 *
	 *   in all cases, the expected result is ERR_BAD_HANDLE error.
	 */
	rc = _trusty_wait(handle_base + MAX_USER_HANDLES, &event, timeout);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "wait on invalid handle");

	rc = _trusty_wait(handle_base + MAX_USER_HANDLES + 1, &event, timeout);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "wait on invalid handle");

	rc = _trusty_wait(handle_base - 1, &event, timeout);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "wait on invalid handle");

	/* waiting on non-existing handle that is in valid range. */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		rc = _trusty_wait(handle_base + i, &event, timeout);
		_EXPECT_EQ (ERR_NOT_FOUND, rc, "wait on invalid handle");
	}

	_TEST_END
}

/*
 *  Close handle unittest
 */
static void run_close_handle_negative_test(void)
{
	int rc;

	_TEST_BEGIN(__func__);

	/* closing an invalid (negative value) handle. */
	rc = _trusty_close(INVALID_IPC_HANDLE);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "closing invalid handle");

	/*
	 *   call close on an invalid (out of range) handle
	 *
	 *   check handling of the following cases:
	 *     - handle is on the upper boundary of valid handle range
	 *     - handle is above of the upper boundary of valid handle range
	 *     - handle is below of valid handle range
	 *
	 *   in all cases, the expected result is ERR_BAD_HANDLE error.
	 */
	rc = _trusty_close(handle_base + MAX_USER_HANDLES);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "closing invalid handle");

	rc = _trusty_close(handle_base + MAX_USER_HANDLES + 1);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "closing invalid handle");

	rc = _trusty_close(handle_base - 1);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "closing invalid handle");

	/* closing non-existing handle that is in valid range. */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		rc = _trusty_close(handle_base + i);
		_EXPECT_EQ (ERR_NOT_FOUND, rc, "closing invalid handle");
	}

	_TEST_END
}

/*
 *  Set cookie negative unittest
 */
static void  run_set_cookie_negative_test(void)
{
	int rc;

	_TEST_BEGIN(__func__);

	/* set cookie for invalid (negative value) handle. */
	rc = _trusty_set_cookie(INVALID_IPC_HANDLE, (void *) 0x1BEEF);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "set cookie for invalid handle");

	/*
	 *   calling set cookie for an invalid (out of range) handle
	 *
	 *   check handling of the following cases:
	 *     - handle is on the upper boundary of valid handle range
	 *     - handle is above of the upper boundary of valid handle range
	 *     - handle is below of valid handle range
	 *
	 *   in all cases, the expected result is ERR_BAD_HANDLE error.
	 */
	rc = _trusty_set_cookie(handle_base + MAX_USER_HANDLES, (void *) 0x2BEEF);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "set cookie for invalid handle");

	rc = _trusty_set_cookie(handle_base + MAX_USER_HANDLES + 1, (void *) 0x2BEEF);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "set cookie for invalid handle");

	rc = _trusty_set_cookie(handle_base - 1, (void *) 0x2BEEF);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "set cookie for invalid handle");

	/* set cookie for non-existing handle that is in valid range. */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		rc = _trusty_set_cookie(handle_base + i, (void *) 0x3BEEF);
		_EXPECT_EQ (ERR_NOT_FOUND, rc, "set cookie for invalid handle");
	}

	_TEST_END
}


/****************************************************************************/

/*
 *  Port create unittest
 */
static void run_port_create_negative_test(void)
{
	int  rc;
	char path[MAX_PORT_PATH_LEN + 16];

	_TEST_BEGIN(__func__);

	/* create port with empty path */
	path[0] = '\0';
	rc = _trusty_port_create(path, 2, 64, 0);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "empty path srv");

	/* create port with zero buffers */
        sprintf(path, "%s.port", SRV_PATH_BASE);
	rc = _trusty_port_create(path, 0, 64, 0);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "no buffers");

	/* create port with zero buffer size */
        sprintf(path, "%s.port", SRV_PATH_BASE);
	rc = _trusty_port_create(path, 2, 0, 0);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "zero buf size");

	/* create port with large number of buffers */
        sprintf(path, "%s.port", SRV_PATH_BASE);
	rc = _trusty_port_create(path, MAX_PORT_BUF_NUM * 100, 64, 0);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "large buf num");

	/* create port with large buffer size */
        sprintf(path, "%s.port", SRV_PATH_BASE);
	rc = _trusty_port_create(path,  2, MAX_PORT_BUF_SIZE * 100, 0);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "large buf size");

	/* create port with path oversized name */
	int len = sprintf(path, "%s.port", SRV_PATH_BASE);
	for (uint i = len; i < sizeof(path); i++) path[i] = 'a';
	path[sizeof(path)-1] = '\0';
	rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE, 0);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "path is too long");
	rc = _trusty_close(rc);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "close port");

	_TEST_END
}

static void run_port_create_test (void)
{
	int  rc;
	uint i;
	char path[MAX_PORT_PATH_LEN];
	handle_t ports[MAX_USER_HANDLES];

	_TEST_BEGIN(__func__);

	/* create maximum number of ports */
	for (i = 2; i < MAX_USER_HANDLES-1; i++) {
		sprintf(path, "%s.port.%s%d", SRV_PATH_BASE, "test", i);
		rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE, 0);
		_EXPECT_GT_ZERO (rc, "create ports");
		ports[i] = (handle_t) rc;

		/* create a new port that collide with an existing port */
		rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE, 0);
		_EXPECT_EQ (ERR_ALREADY_EXISTS, rc, "create existing port");
	}

	/* create one more that should succeed */
	sprintf(path, "%s.port.%s%d", SRV_PATH_BASE, "test", i);
	rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE, 0);
	_EXPECT_GT_ZERO (rc, "create ports");
	ports[i] = (handle_t) rc;

	/* but creating colliding port should fail with different
	   error code because we actually exceeded max number of
	   handles instead of colliding with an existing path */
	rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE, 0);
	_EXPECT_EQ (ERR_NO_RESOURCES, rc, "create existing port");

	sprintf(path, "%s.port.%s%d", SRV_PATH_BASE, "test", MAX_USER_HANDLES);
	rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE, 0);
	_EXPECT_EQ (ERR_NO_RESOURCES, rc, "max ports");

	/* close them all  */
	for (i = 2; i < MAX_USER_HANDLES; i++) {
		/* close a valid port  */
		rc = _trusty_close(ports[i]);
		_EXPECT_EQ (NO_ERROR, rc, "closing port");

		/* close previously closed port. It should fail! */
		rc = _trusty_close(ports[i]);
		_EXPECT_EQ (ERR_NOT_FOUND, rc, "closing closed port");

		ports[i] = INVALID_IPC_HANDLE;
	}

	_TEST_END
}

/*
 *
 */
static void run_wait_on_port_test (void)
{
	int rc;
	uevent_t event;
	char path[MAX_PORT_PATH_LEN];
	handle_t ports[MAX_USER_HANDLES];

	_TEST_BEGIN(__func__);

	#define COOKIE_BASE 100

	/* create maximum number of ports */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		sprintf(path, "%s.port.%s%d", SRV_PATH_BASE, "test", i);
		rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE, 0);
		_EXPECT_GT_ZERO (rc, "max ports");
		ports[i] = (handle_t) rc;

		rc = _trusty_set_cookie(ports[i], (void *)(uintptr_t)(COOKIE_BASE + i));
		_EXPECT_EQ (NO_ERROR, rc, "set cookie on port");
	}

	/* wait on each individual port */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		/* wait with zero timeout */
		rc = _trusty_wait(ports[i], &event, 0);
		_EXPECT_EQ (ERR_TIMED_OUT, rc, "zero timeout");

		/* wait with non-zero timeout */
		rc = _trusty_wait(ports[i], &event, 100);
		_EXPECT_EQ (ERR_TIMED_OUT, rc, "non-zero timeout");
	}

	/* wait on all ports with zero timeout */
	rc = _trusty_wait_any(&event, 0);
	_EXPECT_EQ (ERR_TIMED_OUT, rc, "zero timeout");

	/* wait on all ports with non-zero timeout*/
	rc = _trusty_wait_any(&event, 100);
	_EXPECT_EQ (ERR_TIMED_OUT, rc, "non-zero timeout");

	/* close them all */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		/* close a valid port  */
		rc = _trusty_close(ports[i]);
		_EXPECT_EQ (NO_ERROR, rc, "closing closed port");
		ports[i] = INVALID_IPC_HANDLE;
	}

	_TEST_END
}

/****************************************************************************/

/*
 *  Connect unittests
 */
static void run_connect_negative_test (void)
{
	int  rc;
	char path[MAX_PORT_PATH_LEN + 16];
	lk_time_t connect_timeout = 1000;  // 1 sec

	_TEST_BEGIN(__func__);

	/* try to connect to port with an empty name */
	rc = sync_connect("", connect_timeout);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "empty path");

	/* try to connect to non-existing port  */
	sprintf(path, "%s.conn.%s", SRV_PATH_BASE, "blah-blah");
	rc = _trusty_connect(path, 0);
	_EXPECT_EQ (ERR_NOT_FOUND, rc, "non-existing path");

	/* try to connect to non-existing port  */
	sprintf(path, "%s.conn.%s", SRV_PATH_BASE, "blah-blah");
	rc = _trusty_connect(path, IPC_CONNECT_ASYNC);
	_EXPECT_EQ (ERR_NOT_FOUND, rc, "non-existing path");

	/* try to connect to port with very long name */
	int len = sprintf(path, "%s.conn.", SRV_PATH_BASE);
	for (uint i = len; i < sizeof(path); i++) path[i] = 'a';
	path[sizeof(path)-1] = '\0';
	rc = sync_connect (path, connect_timeout);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "long path");

	rc = _trusty_close(rc);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "close channel");

	_TEST_END
}


static void run_connect_close_test(void)
{
	int  rc;
	char path[MAX_PORT_PATH_LEN];
	handle_t chans[16];

	_TEST_BEGIN(__func__);

	sprintf(path, "%s.srv.%s", SRV_PATH_BASE, "datasink");

	for (uint j = 2; j < MAX_USER_HANDLES; j++) {
		/* do several iterations to make sure we are not
		   not loosing handles */
		for (uint i = 0; i < countof(chans); i++) {
			rc = sync_connect(path, 1000);
			_EXPECT_GT_ZERO (rc, "connect/close");
			chans[i] = (handle_t) rc;
		}

		for (uint i = 0; i < countof(chans); i++) {
			rc = _trusty_close(chans[i]);
			_EXPECT_EQ (NO_ERROR, rc, "connect/close");
		}
	}

	_TEST_END
}

static void run_connect_close_by_peer_test(const char *test)
{
	int rc;
	char path[MAX_PORT_PATH_LEN];
	uevent_t event;
	handle_t chans[16];
	uint chan_cnt = 0;

	_TEST_BEGIN(__func__);

	#define COOKIE_BASE 100

	/*
	 * open up to 16 connection to specified test port which would
	 * close them all in a different way:
	 */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE, test);
	for (uint i = 0; i  < countof(chans); i++) {
		/* open new connection */
		uint retry_cnt = 10;
		while (retry_cnt) {
			rc = sync_connect(path, 2000);
			if (rc == ERR_NOT_FOUND) {
				/* wait a bit and retry */
				--retry_cnt;
				_trusty_nanosleep(0, 0, 100 * MSEC);
			} else {
				break;
			}
		}
		_EXPECT_GT_ZERO (retry_cnt, test);

		/*
		 * depending on task scheduling connect might return real
		 * handle that will be closed later or it might return
		 * ERR_CHANNEL_CLOSED error if the channel has already been
		 * closed. Both cases are correct and must be handled.
		 */
		if (rc >= 0) {
			/* got real handle */
			chans[i] = (handle_t) rc;

			/* attach cookie for returned channel */
			rc = _trusty_set_cookie((handle_t) rc,
			                (void*)(uintptr_t)(COOKIE_BASE + i));
			_EXPECT_EQ (NO_ERROR, rc, test);

			chan_cnt++;
		} else {
			/* could be already closed channel */
			_EXPECT_EQ (ERR_CHANNEL_CLOSED, rc, test);
		}

		/* check if any channels are closed */
		while ((rc = _trusty_wait_any(&event, 0)) == NO_ERROR) {
			_EXPECT_EQ (IPC_HANDLE_POLL_HUP, event.event, test);
			uint idx = (uint)(uintptr_t)event.cookie - COOKIE_BASE;
			_EXPECT_EQ (chans[idx], event.handle, test);
			_EXPECT_GT (countof(chans), idx, test);
			if (idx < countof(chans)) {
				rc = _trusty_close(chans[idx]);
				_EXPECT_EQ (NO_ERROR, rc, test);
				chans[idx] = INVALID_IPC_HANDLE;
			}
			chan_cnt--;
		}
	}

	/* wait until all channels are closed */
	while (chan_cnt) {
		rc = _trusty_wait_any(&event, 10000);
		_EXPECT_EQ (NO_ERROR, rc, test);
		_EXPECT_EQ (IPC_HANDLE_POLL_HUP, event.event, test);

		uint idx = (uint)(uintptr_t)event.cookie - COOKIE_BASE;
		_EXPECT_GT (countof(chans), idx, test);
		_EXPECT_EQ (chans[idx], event.handle, test);
		if (idx < countof(chans)) {
			rc = _trusty_close(chans[idx]);
			_EXPECT_EQ (NO_ERROR, rc, test);
			chans[idx] = INVALID_IPC_HANDLE;
		}
		chan_cnt--;
	}

	_EXPECT_EQ (0, chan_cnt, test);

	_TEST_END
}

static void run_async_connect_test (void)
{
	int rc;
	handle_t chan;
	uevent_t event;
	uuid_t peer_uuid = UUID_INITIAL_VALUE(peer_uuid);
	char path[MAX_PORT_PATH_LEN];

	_TEST_BEGIN(__func__);

	sprintf(path, "%s.main.%s", SRV_PATH_BASE, "async");

	/* connect to non existing port synchronously without wait_for_port */
	rc = _trusty_connect(path, 0);
	_EXPECT_EQ (ERR_NOT_FOUND, rc, "async");
	rc = _trusty_close((handle_t)rc);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "async");

	/* connect to non existing port asynchronously without wait_for_port */
	rc = _trusty_connect(path, IPC_CONNECT_ASYNC);
	_EXPECT_EQ (ERR_NOT_FOUND, rc, "async");
	rc = _trusty_close((handle_t)rc);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "async");

	/* connect to non existing port asynchronously with wait_for_port */
	rc = _trusty_connect(path,  IPC_CONNECT_ASYNC | IPC_CONNECT_WAIT_FOR_PORT);
	_EXPECT_GT_ZERO(rc, "async");
	if (rc >= 0) {
		chan = (handle_t) rc;

		/* wait on channel */
		rc = _trusty_wait(chan, &event, 1000);
		_EXPECT_EQ (ERR_TIMED_OUT, rc, "async");

		/* and close it */
		rc = _trusty_close(chan);
		_EXPECT_EQ (NO_ERROR, rc, "async");
	}

	/* connect to non-existing port asyncronously with wait_for_port */
	rc = _trusty_connect(path,  IPC_CONNECT_ASYNC | IPC_CONNECT_WAIT_FOR_PORT);
	_EXPECT_GT_ZERO (rc, "async");
	chan = (handle_t) rc;

	if (rc >= 0) {
		handle_t port;
		uint32_t exp_event;

		/* wait on channel for connect */
		rc = _trusty_wait(chan, &event, 100);
		_EXPECT_EQ (ERR_TIMED_OUT, rc, "async");

		/* now create port */
		rc = _trusty_port_create(path, 1, 64, IPC_PORT_ALLOW_TA_CONNECT);
		_EXPECT_GT_ZERO (rc, "async");
		if (rc >= 0) {
			port = (handle_t) rc;

			/* and wait for incomming connections */
			exp_event = IPC_HANDLE_POLL_READY;
			rc = _trusty_wait(port, &event, 1000);
			_EXPECT_EQ (NO_ERROR, rc, "async");
			_EXPECT_EQ (exp_event, event.event, "async");

			if (rc == NO_ERROR) {
				handle_t srv_chan;

				/* got one, accept it */
				rc = _trusty_accept(port, &peer_uuid);
				_EXPECT_GT_ZERO (rc, "async");
				srv_chan = (handle_t) rc;

				/* and close it */
				_trusty_close(srv_chan);

				/* now wait on original chan:
				 * there should be READY and HUP events
				 */
				exp_event = IPC_HANDLE_POLL_READY |
					    IPC_HANDLE_POLL_HUP;
				rc = _trusty_wait(chan, &event, 1000);
				_EXPECT_EQ (NO_ERROR, rc, "async");
				_EXPECT_EQ (exp_event, event.event, "async");
			}
			_trusty_close(port);
		}
		_trusty_close(chan);
	}

	_TEST_END
}


static void run_connect_selfie_test (void)
{
	int rc, rc1;
	uuid_t peer_uuid = UUID_INITIAL_VALUE(peer_uuid);
	uuid_t zero_uuid = UUID_INITIAL_VALUE(zero_uuid);
	char path[MAX_PORT_PATH_LEN];
	lk_time_t connect_timeout = 1000;  // 1 sec

	_TEST_BEGIN(__func__);

	/* Try to connect to port that we register ourself.
	   It is not very usefull scenario, just to make sure that
	   nothing bad is happening */
	sprintf(path, "%s.main.%s", SRV_PATH_BASE, "selfie");
	rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE,
			         IPC_PORT_ALLOW_TA_CONNECT);
	_EXPECT_GT_ZERO (rc, "selfie");

	if (rc >= 0) {
		handle_t test_port = rc;

		/* Since we are single threaded and cannot accept connection
		 * we will always timeout.
		 */

		/* with non-zero timeout  */
		rc = sync_connect (path, connect_timeout);
		_EXPECT_EQ (ERR_TIMED_OUT, rc, "selfie");

		/* with zero timeout */
		rc = sync_connect (path, 0);
		_EXPECT_EQ (ERR_TIMED_OUT, rc, "selfie");

		/* since we did not call wait on port yet we have
		 * 2 connection requests pending (attached to port)
		 * teared down by peer (us).
		 */
		uevent_t event;
		uint exp_event = IPC_HANDLE_POLL_READY;

		int rc = _trusty_wait_any(&event, -1);
		_EXPECT_EQ (NO_ERROR, rc, "wait on port");
		_EXPECT_EQ (test_port, event.handle, "wait on port");
		_EXPECT_EQ (exp_event, event.event,  "wait on port");

		if (rc == NO_ERROR && (event.event & IPC_HANDLE_POLL_READY)) {

			/* we have pending connection, but it is already closed */
			rc = _trusty_accept(test_port, &peer_uuid);
			_EXPECT_EQ (ERR_CHANNEL_CLOSED, rc, "accept");

			rc1 = memcmp(&peer_uuid, &zero_uuid, sizeof(zero_uuid));
			_EXPECT_EQ (0, rc1, "accept")

			/* the second one is closed too */
			rc = _trusty_accept(test_port, &peer_uuid);
			_EXPECT_EQ (ERR_CHANNEL_CLOSED, rc, "accept");

			rc1 = memcmp(&peer_uuid, &zero_uuid, sizeof(zero_uuid));
			_EXPECT_EQ (0, rc1, "accept")

			/* There should be no more pending connections so next
			   accept should return ERR_NO_MSG */
			rc = _trusty_accept(test_port, &peer_uuid);
			_EXPECT_EQ (ERR_NO_MSG, rc, "accept");

			rc1 = memcmp(&peer_uuid, &zero_uuid, sizeof(zero_uuid));
			_EXPECT_EQ (0, rc1, "accept")
		}

		/* add couple connections back and destroy them along with port */
		rc = sync_connect (path, 0);
		_EXPECT_EQ (ERR_TIMED_OUT, rc, "selfie");

		rc = sync_connect (path, 0);
		_EXPECT_EQ (ERR_TIMED_OUT, rc, "selfie");

		/* close selfie port  */
		rc = _trusty_close(test_port);
		_EXPECT_EQ (NO_ERROR, rc, "close selfie");
	}

	_TEST_END
}

static void run_connect_access_test(void)
{
	int rc;
	char path[MAX_PORT_PATH_LEN];

	_TEST_BEGIN(__func__);

	/* open connection to NS only accessible service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "ns_only");
	rc = sync_connect(path, 1000);

	/* It is expected to fail */
	_EXPECT_EQ(ERR_ACCESS_DENIED, rc, "connect to ns_only");

	if (rc >= 0)
		_trusty_close((handle_t)rc);

	/* open connection to TA only accessible service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "ta_only");
	rc = sync_connect(path, 1000);

	/* it is expected to succeed */
	_EXPECT_GT_ZERO(rc, "connect to ta_only");

	if (rc >= 0)
		_trusty_close((handle_t)rc);

	_TEST_END
}

/****************************************************************************/

/*
 *  Accept negative test
 */
static void run_accept_negative_test(void)
{
	int  rc, rc1;
	char path[MAX_PORT_PATH_LEN];
	handle_t chan;
	uuid_t peer_uuid = UUID_INITIAL_VALUE(peer_uuid);
	uuid_t zero_uuid = UUID_INITIAL_VALUE(zero_uuid);

	_TEST_BEGIN(__func__);

	/* accept on invalid (negative value) handle */
	rc = _trusty_accept(INVALID_IPC_HANDLE, &peer_uuid);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "accept on invalid handle");

	rc1 = memcmp(&peer_uuid, &zero_uuid, sizeof(zero_uuid));
	_EXPECT_EQ (0, rc1, "accept")

	/*
	 *   calling accept on an invalid (out of range) handle
	 *
	 *   check handling of the following cases:
	 *     - handle is on the upper boundary of valid handle range
	 *     - handle is above of the upper boundary of valid handle range
	 *     - handle is below of valid handle range
	 *
	 *   in all cases, the expected result is ERR_BAD_HANDLE error.
	 */
	rc = _trusty_accept(handle_base + MAX_USER_HANDLES, &peer_uuid);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "accept on invalid handle");

	rc = _trusty_accept(handle_base + MAX_USER_HANDLES + 1, &peer_uuid);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "accept on invalid handle");

	rc = _trusty_accept(handle_base - 1, &peer_uuid);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "accept on invalid handle");

	rc1 = memcmp(&peer_uuid, &zero_uuid, sizeof(zero_uuid));
	_EXPECT_EQ (0, rc1, "accept")

	/* accept on non-existing handle that is in valid range */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		rc = _trusty_accept(handle_base + i, &peer_uuid);
		_EXPECT_EQ (ERR_NOT_FOUND, rc, "accept on invalid handle");

		rc1 = memcmp(&peer_uuid, &zero_uuid, sizeof(zero_uuid));
		_EXPECT_EQ (0, rc1, "accept")
	}

	/* connect to datasink service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE, "datasink");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");
	chan = (handle_t) rc;

	/* call accept on channel handle which is an invalid operation */
	rc = _trusty_accept(chan, &peer_uuid);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "accept on channel");

	rc1 = memcmp(&peer_uuid, &zero_uuid, sizeof(zero_uuid));
	_EXPECT_EQ (0, rc1, "accept")

	rc = _trusty_close(chan);
	_EXPECT_EQ (NO_ERROR, rc, "close channnel")

	_TEST_END
}

static void run_accept_test (void)
{
	int  rc, rc1;
	uevent_t event;
	char path[MAX_PORT_PATH_LEN];
	handle_t ports[MAX_USER_HANDLES];
	uuid_t peer_uuid = UUID_INITIAL_VALUE(peer_uuid);
	uuid_t zero_uuid = UUID_INITIAL_VALUE(zero_uuid);

	_TEST_BEGIN(__func__);

	#define COOKIE_BASE 100

	/* create maximum number of ports */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		sprintf(path, "%s.port.accept%d", SRV_PATH_BASE, i);
		rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE,
				         IPC_PORT_ALLOW_TA_CONNECT);
		_EXPECT_GT_ZERO (rc, "max ports");
		ports[i] = (handle_t) rc;

		rc = _trusty_set_cookie(ports[i], (void *)(uintptr_t)(COOKIE_BASE + ports[i]));
		_EXPECT_EQ (NO_ERROR, rc, "set cookie on port");
	}

	/* poke connect service to initiate connections to us */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE, "connect");
	rc = sync_connect (path, 1000);
	if (rc >= 0)
		_trusty_close((handle_t)rc);

	/* handle incoming connections */
	for (uint i = 2; i < MAX_USER_HANDLES; i++ ) {

		rc = _trusty_wait_any(&event, 1000);
		_EXPECT_EQ (NO_ERROR, rc, "accept test");
		_EXPECT_EQ (IPC_HANDLE_POLL_READY, event.event, "accept test");

		/* check port cookie */
		void *exp_cookie = (void *)(COOKIE_BASE + (uintptr_t)event.handle);
		_EXPECT_EQ ((uintptr_t)exp_cookie, (uintptr_t)event.cookie, "accept test");

		/* accept connection - should fail because we do not
		   have any room for handles */
		rc = _trusty_accept(event.handle, &peer_uuid);
		_EXPECT_EQ (ERR_NO_RESOURCES, rc, "accept test");

		/* check peer uuid */
		rc1 = memcmp(&peer_uuid, &zero_uuid, sizeof(zero_uuid));
		_EXPECT_EQ (0, rc1, "accept test")
	}

	/* free 1 handle  so we have room and repeat test */
	rc = _trusty_close(ports[2]);
	_EXPECT_EQ(NO_ERROR, 0, "close accept test");
	ports[2] = INVALID_IPC_HANDLE;

	/* poke connect service to initiate connections to us */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE, "connect");
	rc = sync_connect (path, 1000);
	if (rc >= 0)
		_trusty_close((handle_t)rc);

	/* handle incoming connections */
	for (uint i = 2; i < MAX_USER_HANDLES-1; i++ ) {

		rc = _trusty_wait_any(&event, 3000);
		_EXPECT_EQ (NO_ERROR, rc, "accept test");
		_EXPECT_EQ (IPC_HANDLE_POLL_READY, event.event, "accept test");

		/* check port cookie */
		void *exp_cookie = (void *)(COOKIE_BASE + (uintptr_t)event.handle);
		_EXPECT_EQ ((uintptr_t)exp_cookie, (uintptr_t)event.cookie, "accept test");

		rc = _trusty_accept(event.handle, &peer_uuid);
		_EXPECT_EQ(handle_base + 2, rc, "accept test");

        // TODO(GR-0003): Not support uuid test except zero_uuid currently.
		/* check peer uuid */
		//rc1 = memcmp(&peer_uuid, &srv_app_uuid, sizeof(srv_app_uuid));
		//_EXPECT_EQ (0, rc1, "accept test")

		rc = _trusty_close(rc);
		_EXPECT_EQ (NO_ERROR, rc, "accept test");
	}

	/* close them all */
	for (uint i = 3; i < MAX_USER_HANDLES; i++) {
		/* close a valid port  */
		rc = _trusty_close(ports[i]);
		_EXPECT_EQ (NO_ERROR, rc, "close port");
		ports[i] = INVALID_IPC_HANDLE;
	}

	_TEST_END
}

/****************************************************************************/

static void run_get_msg_negative_test(void)
{
	int rc;
	ipc_msg_info_t inf;
	handle_t port;
	handle_t chan;
	char path[MAX_PORT_PATH_LEN];

	_TEST_BEGIN(__func__);

	/* get_msg on invalid (negative value) handle. */
	rc = _trusty_get_msg(INVALID_IPC_HANDLE, &inf);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "get_msg on invalid handle");

	/*
	 *   calling get_msg on an invalid (out of range) handle
	 *
	 *   check handling of the following cases:
	 *     - handle is on the upper boundary of valid handle range
	 *     - handle is above of the upper boundary of valid handle range
	 *     - handle is below of valid handle range
	 *
	 *   in all cases, the expected result is ERR_BAD_HANDLE error.
	 */
	rc = _trusty_get_msg(handle_base + MAX_USER_HANDLES, &inf);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "get_msg on invalid handle");

	rc = _trusty_get_msg(handle_base + MAX_USER_HANDLES + 1, &inf);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "get_msg on invalid handle");

	rc = _trusty_get_msg(handle_base - 1, &inf);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "get_msg on invalid handle");

	/* get_msg on non-existing handle that is in valid range. */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		rc = _trusty_get_msg(handle_base + i, &inf);
		_EXPECT_EQ (ERR_NOT_FOUND, rc, "get_msg on invalid handle");
	}

	/* calling get_msg on port handle should fail
	   because get_msg is only applicable to channels */
	sprintf(path, "%s.main.%s", SRV_PATH_BASE,  "datasink");
	rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE,
			         IPC_PORT_ALLOW_TA_CONNECT);
	_EXPECT_GT_ZERO (rc, "create datasink port");
	port = (handle_t) rc;

	rc = _trusty_get_msg(port, &inf);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "get_msg on port");
	_trusty_close(port);

	/* call get_msg on channel that do not have any pending messages */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "datasink");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");
	chan = (handle_t) rc;

	rc = _trusty_get_msg(chan, &inf);
	_EXPECT_EQ (ERR_NO_MSG, rc, "get_msg on empty channel");

	rc = _trusty_close(chan);
	_EXPECT_EQ (NO_ERROR, rc, "close channnel");

	_TEST_END
}


static void run_put_msg_negative_test(void)
{
	int rc;
	handle_t port;
	handle_t chan;
	char path[MAX_PORT_PATH_LEN];

	_TEST_BEGIN(__func__);

	/* put_msg on invalid (negative value) handle */
	rc = _trusty_put_msg(INVALID_IPC_HANDLE, 0);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "put_msg on invalid handle");

	/*
	 *   calling put_msg on an invalid (out of range) handle
	 *
	 *   check handling of the following cases:
	 *     - handle is on the upper boundary of valid handle range
	 *     - handle is above of the upper boundary of valid handle range
	 *     - handle is below of valid handle range
	 *
	 *   in all cases, the expected result is ERR_BAD_HANDLE error.
	 */
	rc = _trusty_put_msg(handle_base + MAX_USER_HANDLES, 0);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "put_msg on invalid handle");

	rc = _trusty_put_msg(handle_base + MAX_USER_HANDLES + 1, 0);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "put_msg on invalid handle");

	rc = _trusty_put_msg(handle_base - 1, 0);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "put_msg on invalid handle");

	/* put_msg on non-existing handle that is in valid range */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		rc = _trusty_put_msg(handle_base + i, 0);
		_EXPECT_EQ (ERR_NOT_FOUND, rc, "put_msg on invalid handle");
	}

	/* calling put_msg on port handle should fail
	   because put_msg is only applicable to channels */
	sprintf(path, "%s.main.%s", SRV_PATH_BASE,  "datasink");
	rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE,
			         IPC_PORT_ALLOW_TA_CONNECT);
	_EXPECT_GT_ZERO (rc, "create datasink port");
	port = (handle_t) rc;

	rc = _trusty_put_msg(port, 0);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "put_msg on port");
	rc = _trusty_close(port);
	_EXPECT_EQ (NO_ERROR, rc, "close port");

	/* call put_msg on channel that do not have any pending messages */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "datasink");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");
	chan = (handle_t) rc;

	rc = _trusty_put_msg(chan, 0);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "put_msg on empty channel");
	rc = _trusty_close(chan);
	_EXPECT_EQ (NO_ERROR, rc, "close channel");

	_TEST_END
}


/*
 *  Send 10000 messages to datasink service
 */
static void run_send_msg_test(void)
{
	int rc;
	handle_t chan;
	char path[MAX_PORT_PATH_LEN];
	uint8_t   buf0[64];
	uint8_t   buf1[64];
	iovec_t   iov[2];
	ipc_msg_t msg;

	_TEST_BEGIN(__func__);

	/* prepare test buffer */
	fill_test_buf(buf0, sizeof(buf0), 0x55);
	fill_test_buf(buf1, sizeof(buf1), 0x44);

	iov[0].base = buf0;
	iov[0].len  = sizeof(buf0);
	iov[1].base = buf1;
	iov[1].len =  sizeof(buf1);
	msg.num_handles = 0;
	msg.handles = NULL;
	msg.num_iov = 2;
	msg.iov   = iov;

	/* open connection to datasink service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "datasink");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");

	if (rc >= 0) {
		chan = (handle_t) rc;
		for (uint i = 0; i < 10000; i++) {
			rc = _trusty_send_msg(chan, &msg);
			if (rc == ERR_NOT_ENOUGH_BUFFER) { /* wait for room */
				uevent_t  uevt;
				uint exp_event = IPC_HANDLE_POLL_SEND_UNBLOCKED;
				rc = _trusty_wait(chan, &uevt, 1000);
				_EXPECT_EQ (NO_ERROR, rc, "waiting for space");
				_EXPECT_EQ (chan, uevt.handle, "waiting for space");
				_EXPECT_EQ (exp_event, uevt.event,  "waiting for space");
			} else {
				_EXPECT_EQ (64, rc, "send_msg bulk")
			}
			if (!_all_ok) {
				TLOGI("%s: abort (rc = %d) test\n", __func__, rc);
				break;
			}
		}
		rc = _trusty_close(chan);
		_EXPECT_EQ (NO_ERROR, rc, "close channel");
	}

	_TEST_END
}


static void run_send_msg_negative_test(void)
{
	int rc;
	handle_t port;
	handle_t chan;
	char path[MAX_PORT_PATH_LEN];
	uint8_t buf[64];
	iovec_t iov[2];
	ipc_msg_t msg;

	_TEST_BEGIN(__func__);

	/* init msg to empty message */
	memset (&msg, 0, sizeof(msg));

	/* send_msg on invalid (negative value) handle */
	rc = _trusty_send_msg(INVALID_IPC_HANDLE, &msg);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "send_msg on invalid handle");

	/* calling send_msg with NULL msg should fail for any handle */
	rc = _trusty_send_msg(INVALID_IPC_HANDLE, NULL);
	_EXPECT_EQ (ERR_FAULT, rc, "send_msg on NULL msg");

	/*
	 *   calling send_msg on an invalid (out of range) handle
	 *
	 *   check handling of the following cases:
	 *     - handle is on the upper boundary of valid handle range
	 *     - handle is above of the upper boundary of valid handle range
	 *     - handle is below of valid handle range
	 *
	 *   in all cases, the expected result is ERR_BAD_HANDLE error.
	 */
	rc = _trusty_send_msg(handle_base + MAX_USER_HANDLES, &msg);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "send_msg on invalid handle");

	rc = _trusty_send_msg(handle_base + MAX_USER_HANDLES + 1, &msg);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "send_msg on invalid handle");

	rc = _trusty_send_msg(handle_base - 1, &msg);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "send_msg on invalid handle");

	/* calling send_msg with NULL msg should fail for any handle */
	rc = _trusty_send_msg(MAX_USER_HANDLES, NULL);
	_EXPECT_EQ (ERR_FAULT, rc, "send_msg on NULL msg");

	/* send_msg on non-existing handle that is in valid range */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		rc = _trusty_send_msg(handle_base + i, &msg);
		_EXPECT_EQ (ERR_NOT_FOUND, rc, "send on invalid handle");

		/* calling send_msg with NULL msg should fail for any handle */
		rc = _trusty_send_msg(handle_base + i, NULL);
		_EXPECT_EQ (ERR_FAULT, rc, "send_msg on NULL msg");
	}

	/* calling send_msg on port handle should fail
	   because send_msg is only applicable to channels */
	sprintf(path, "%s.main.%s", SRV_PATH_BASE,  "datasink");
	rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE,
			         IPC_PORT_ALLOW_TA_CONNECT);
	_EXPECT_GT_ZERO (rc, "create datasink port");
	port = (handle_t) rc;

	rc = _trusty_send_msg(port, &msg);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "send_msg on port");
	_trusty_close(port);

	/* open connection to datasink service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "datasink");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");
	chan = (handle_t) rc;

	/* send message with handles pointing to NULL */
	msg.num_handles = 1;
	msg.handles  = NULL;
	rc = _trusty_send_msg(chan, &msg);
	_EXPECT_EQ (ERR_FAULT, rc, "sending handles");

	/* reset handles */
	msg.num_handles = 0;
	msg.handles  = NULL;

	/* set num_iov to non zero but keep iov ptr NULL */
	msg.num_iov = 1;
	msg.iov  = NULL;
	rc = _trusty_send_msg(chan, &msg);
	_EXPECT_EQ (ERR_FAULT, rc, "sending bad iovec array");

	/*  send msg with iovec with bad base ptr */
	iov[0].len  = sizeof(buf) /  2;
	iov[0].base = NULL;
	iov[1].len  = sizeof(buf) /  2;
	iov[1].base = NULL;
	msg.num_iov = 2;
	msg.iov  = iov;
	rc = _trusty_send_msg(chan, &msg);
	_EXPECT_EQ (ERR_FAULT, rc, "sending bad iovec");

	/*  send msg with iovec with bad base ptr */
	iov[0].len  = sizeof(buf) /  2;
	iov[0].base = buf;
	iov[1].len  = sizeof(buf) /  2;
	iov[1].base = NULL;
	msg.num_iov = 2;
	msg.iov  = iov;
	rc = _trusty_send_msg(chan, &msg);
	_EXPECT_EQ (ERR_FAULT, rc, "sending bad iovec");

	rc = _trusty_close(chan);
	_EXPECT_EQ (NO_ERROR, rc, "close channel");

	_TEST_END
}

static void run_read_msg_negative_test(void)
{
	int rc;
	handle_t port;
	handle_t chan;
	uevent_t  uevt;
	char path[MAX_PORT_PATH_LEN];
	uint8_t tx_buf[64];
	uint8_t rx_buf[64];
	ipc_msg_info_t inf;
	ipc_msg_t   tx_msg;
	iovec_t     tx_iov;
	ipc_msg_t   rx_msg;
	iovec_t     rx_iov[2];

	_TEST_BEGIN(__func__);

	/* init msg to empty message */
	memset (&rx_msg, 0, sizeof(rx_msg));
	memset (&tx_msg, 0, sizeof(tx_msg));

	/* read_msg on invalid (negative value) handle */
	rc = _trusty_read_msg(INVALID_IPC_HANDLE, 0, 0, &rx_msg);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "read_msg on invalid handle");

	rc = _trusty_read_msg(INVALID_IPC_HANDLE, 0, 0, NULL);
	_EXPECT_EQ (ERR_FAULT, rc, "read_msg on invalid handle");

	/*
	 *   calling read_msg on an invalid (out of range) handle
	 *
	 *   check handling of the following cases:
	 *     - handle is on the upper boundary of valid handle range
	 *     - handle is above of the upper boundary of valid handle range
	 *     - handle is below of valid handle range
	 *
	 *   in all cases, the expected result is ERR_BAD_HANDLE error.
	 */
	rc = _trusty_read_msg(handle_base + MAX_USER_HANDLES, 0, 0, &rx_msg);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "read_msg on bad handle");

	rc = _trusty_read_msg(handle_base + MAX_USER_HANDLES + 1, 0, 0, &rx_msg);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "read_msg on bad handle");

	rc = _trusty_read_msg(handle_base - 1, 0, 0, &rx_msg);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "read_msg on bad handle");

	/* calling read_msg with NULL msg should fail for any handle */
	rc = _trusty_read_msg(handle_base + MAX_USER_HANDLES, 0, 0, NULL);
	_EXPECT_EQ (ERR_FAULT, rc, "read_msg on NULL msg");

	/* send_msg on non-existing handle that is in valid range */
	for (uint i = 2; i < MAX_USER_HANDLES; i++) {
		rc = _trusty_read_msg(handle_base + i, 0, 0, &rx_msg);
		_EXPECT_EQ (ERR_NOT_FOUND, rc, "read_msg on non existing handle");

		/* calling send_msg with NULL msg should fail for any handle */
		rc = _trusty_read_msg(handle_base + i, 0, 0, NULL);
		_EXPECT_EQ (ERR_FAULT, rc, "read_msg on NULL msg");
	}

	/* calling read_msg on port handle should fail
	   because read_msg is only applicable to channels */
	sprintf(path, "%s.main.%s", SRV_PATH_BASE,  "datasink");
	rc = _trusty_port_create(path, 2, MAX_PORT_BUF_SIZE,
			         IPC_PORT_ALLOW_TA_CONNECT);
	_EXPECT_GT_ZERO (rc, "create datasink port");
	port = (handle_t) rc;

	rc = _trusty_read_msg(port, 0, 0, &rx_msg);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "read_msg on port");
	_trusty_close(port);

	/* open connection to echo service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "echo");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");
	chan = (handle_t) rc;

	/* NULL msg on valid channel */
	rc = _trusty_read_msg(chan, 0, 0, NULL);
	_EXPECT_EQ (ERR_FAULT, rc, "read_msg on NULL msg");

	/* read_msg on invalid msg id */
	rc = _trusty_read_msg(chan, 0, 0, &rx_msg);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "read_msg on invalid msg id");

	rc = _trusty_read_msg(chan, 1000, 0, &rx_msg);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "read_msg on invalid msg id");

	/* send a message to echo service */
	memset(tx_buf, 0x55, sizeof(tx_buf));
	tx_iov.base = tx_buf;
	tx_iov.len  = sizeof(tx_buf);
	tx_msg.num_iov = 1;
	tx_msg.iov     = &tx_iov;
	tx_msg.num_handles = 0;
	tx_msg.handles = NULL;

	rc = _trusty_send_msg(chan, &tx_msg);
	_EXPECT_EQ (64, rc, "sending msg to echo");

	/* and wait for response */
	rc = _trusty_wait(chan, &uevt, 1000);
	_EXPECT_EQ (NO_ERROR, rc, "waiting on echo response");
	_EXPECT_EQ (chan, uevt.handle, "wait on channel");

	rc = _trusty_get_msg(chan, &inf);
	_EXPECT_EQ (NO_ERROR, rc, "getting echo msg");
	_EXPECT_EQ (sizeof(tx_buf), inf.len, "echo message reply length");

	/* now we have valid message with valid id */

	rx_iov[0].len  = sizeof(rx_buf) / 2;
	rx_iov[1].len  = sizeof(rx_buf) / 2;

	/* read message with invalid iovec array */
	rx_msg.iov     = NULL;
	rx_msg.num_iov = 2;
	rc = _trusty_read_msg(chan, inf.id, 0, &rx_msg);
	_EXPECT_EQ (ERR_FAULT, rc, "read with invalid iovec array");

	/* read with invalid iovec entry */
	rx_iov[0].base = NULL;
	rx_iov[1].base = NULL;
	rx_msg.iov     = rx_iov;
	rc = _trusty_read_msg(chan, inf.id, 0, &rx_msg);
	_EXPECT_EQ (ERR_FAULT, rc, "read with invalid iovec");

	rx_iov[0].base = rx_buf;
	rx_iov[1].base = NULL;
	rc = _trusty_read_msg(chan, inf.id, 0, &rx_msg);
	_EXPECT_EQ (ERR_FAULT, rc, "read with invalid iovec");

	rx_iov[0].base = rx_buf;
	rx_iov[1].base = rx_buf + sizeof(rx_buf) / 2;

	/* read with invalid offset with valid iovec array */
	rc = _trusty_read_msg(chan, inf.id, inf.len + 1, &rx_msg);
	_EXPECT_EQ (ERR_INVALID_ARGS, rc, "read with invalid offset");

	/* cleanup */
	rc = _trusty_put_msg(chan, inf.id);
	_EXPECT_EQ (NO_ERROR, rc, "putting echo msg");

	rc = _trusty_close(chan);
	_EXPECT_EQ (NO_ERROR, rc, "close channel");

	_TEST_END
}

static void run_end_to_end_msg_test(void)
{
	int rc;
	handle_t chan;
	uevent_t uevt;
	char path[MAX_PORT_PATH_LEN];
	uint8_t tx_buf[64];
	uint8_t rx_buf[64];
	ipc_msg_info_t inf;
	ipc_msg_t   tx_msg;
	iovec_t     tx_iov;
	ipc_msg_t   rx_msg;
	iovec_t     rx_iov;

	_TEST_BEGIN(__func__);

	tx_iov.base = tx_buf;
	tx_iov.len  = sizeof(tx_buf);
	tx_msg.num_iov = 1;
	tx_msg.iov     = &tx_iov;
	tx_msg.num_handles = 0;
	tx_msg.handles = NULL;

	rx_iov.base = rx_buf;
	rx_iov.len  = sizeof(rx_buf);
	rx_msg.num_iov = 1;
	rx_msg.iov     = &rx_iov;
	rx_msg.num_handles = 0;
	rx_msg.handles = NULL;

	memset (tx_buf, 0x55, sizeof(tx_buf));
	memset (rx_buf, 0xaa, sizeof(rx_buf));

	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "echo");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to echo");

	if (rc >= 0) {
		uint tx_cnt = 0;
		uint rx_cnt = 0;

		chan = (handle_t) rc;

		/* send 10000 messages synchronously, waiting for reply
		   for each one
		 */
		tx_cnt = 10000;
		while (tx_cnt) {
			/* send a message */
			rc = _trusty_send_msg(chan, &tx_msg);
			_EXPECT_EQ (64, rc, "sending msg to echo");

			/* wait for response */
			rc = _trusty_wait(chan, &uevt, 1000);
			_EXPECT_EQ (NO_ERROR, rc, "waiting on echo response");
			_EXPECT_EQ (chan, uevt.handle, "wait on channel");

			/* get a reply */
			rc = _trusty_get_msg(chan, &inf);
			_EXPECT_EQ (NO_ERROR, rc, "getting echo msg");

			/* read reply data */
			rc = _trusty_read_msg(chan, inf.id, 0, &rx_msg);
			_EXPECT_EQ (64, rc, "reading echo msg");

			/* discard reply */
			rc = _trusty_put_msg(chan, inf.id);
			_EXPECT_EQ (NO_ERROR, rc, "putting echo msg");

			tx_cnt--;
		}

		/* send/receive 10000 messages asynchronously. */
		rx_cnt = tx_cnt = 10000;
		while (tx_cnt || rx_cnt) {

			/* send messages until all buffers are full */
			while (tx_cnt) {
				rc = _trusty_send_msg(chan, &tx_msg);
				if (rc == ERR_NOT_ENOUGH_BUFFER)
					break;  /* no more space */
				_EXPECT_EQ(64, rc, "sending msg to echo");
				if (rc != 64)
					goto abort_test;
				tx_cnt--;
			}

			/* wait for reply msg or room */
			rc = _trusty_wait(chan, &uevt, 1000);
			_EXPECT_EQ (NO_ERROR, rc, "waiting for reply");
			_EXPECT_EQ (chan, uevt.handle, "wait on channel");

			/* drain all messages */
			while (rx_cnt) {
				/* get a reply */
				rc = _trusty_get_msg(chan, &inf);
				if (rc == ERR_NO_MSG)
					break;  /* no more messages  */

				_EXPECT_EQ (NO_ERROR, rc, "getting echo msg");

				/* read reply data */
				rc = _trusty_read_msg(chan, inf.id, 0, &rx_msg);
				_EXPECT_EQ (64, rc, "reading echo msg");

				/* discard reply */
				rc = _trusty_put_msg(chan, inf.id);
				_EXPECT_EQ (NO_ERROR, rc, "putting echo msg");

				rx_cnt--;
			}

			if (!_all_ok)
				break;
		}

abort_test:
		_EXPECT_EQ (0, tx_cnt, "tx_cnt");
		_EXPECT_EQ (0, rx_cnt, "rx_cnt");

		rc = _trusty_close(chan);
		_EXPECT_EQ (NO_ERROR, rc, "close channel");
	}

	_TEST_END
}


/****************************************************************************/

static void run_hset_create_test(void)
{
	handle_t hset1;
	handle_t hset2;

	_TEST_BEGIN(__func__);

	hset1 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset1, "create handle set1");

	hset2 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset2, "create handle set2");

	_trusty_close(hset1);
	_trusty_close(hset2);

	_TEST_END
}

static void run_hset_add_mod_del_test(void)
{
	int rc;
	handle_t hset1;
	handle_t hset2;

	_TEST_BEGIN(__func__);

	hset1 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset1, "create handle set1");

	hset2 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset2, "create handle set2");

	_ABORT_IF_NOT_OK(abort_test);

	uevent_t evt = {
		.handle = hset2,
		.event  = ~0,
		.cookie = NULL,
	};

	/* add handle to handle set */
	rc = _trusty_handle_set_ctrl(hset1, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "hset add");

	/* modify handle attributes in handle set */
	rc = _trusty_handle_set_ctrl(hset1, HSET_MOD, &evt);
	_EXPECT_EQ(0, rc, "hset mod");

	/* remove handle from handle set */
	rc = _trusty_handle_set_ctrl(hset1, HSET_DEL, &evt);
	_EXPECT_EQ(0, rc, "hset del");

abort_test:
	_trusty_close(hset1);
	_trusty_close(hset2);

	_TEST_END
}

static void run_hset_add_self_test(void)
{
	int rc;
	handle_t hset1;

	_TEST_BEGIN(__func__);

	hset1 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset1, "create handle set1");

	_ABORT_IF_NOT_OK(abort_test);

	uevent_t evt = {
		.handle = hset1,
		.event  = ~0,
		.cookie = NULL,
	};

	/* add handle to handle set */
	rc = _trusty_handle_set_ctrl(hset1, HSET_ADD, &evt);
	_EXPECT_EQ(ERR_INVALID_ARGS, rc, "hset add self");

abort_test:
	_trusty_close(hset1);

	_TEST_END
}

static void run_hset_add_loop_test(void)
{
	int rc;
	handle_t hset1;
	handle_t hset2;
	handle_t hset3;

	_TEST_BEGIN(__func__);

	hset1 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset1, "create handle set1");

	hset2 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset2, "create handle set2");

	hset3 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset3, "create handle set3");

	_ABORT_IF_NOT_OK(abort_test);

	uevent_t evt = {
		.handle = hset2,
		.event  = ~0,
		.cookie = NULL,
	};

	/* add hset2 to hset1 */
	rc = _trusty_handle_set_ctrl(hset1, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "add hset2 to hset1");

	/* add hset3 to hset2 */
	evt.handle = hset3;
	rc = _trusty_handle_set_ctrl(hset2, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "add hset3 to hset2");

	/* add hset1 to hset3 */
	evt.handle = hset1;
	rc = _trusty_handle_set_ctrl(hset3, HSET_ADD, &evt);
	_EXPECT_EQ(ERR_INVALID_ARGS, rc, "add hset1 to hset3");

abort_test:
	_trusty_close(hset2);
	_trusty_close(hset1);
	_trusty_close(hset3);

	_TEST_END
}

static void run_hset_add_duplicate_test(void)
{
	int rc;
	handle_t hset1;
	handle_t hset2;

	_TEST_BEGIN(__func__);

	hset1 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset1, "create handle set1");

	hset2 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset2, "create handle set2");

	_ABORT_IF_NOT_OK(abort_test);

	uevent_t evt = {
		.handle = hset2,
		.event  = ~0,
		.cookie = NULL,
	};

	/* add hset2 to hset1 */
	rc = _trusty_handle_set_ctrl(hset1, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "add hset2 to hset1");

	/* add hset2 to hset1 again */
	rc = _trusty_handle_set_ctrl(hset1, HSET_ADD, &evt);
	_EXPECT_EQ(ERR_ALREADY_EXISTS, rc, "add hset2 to hset1");

abort_test:
	_trusty_close(hset1);
	_trusty_close(hset2);

	_TEST_END
}

static void run_hset_wait_on_empty_set_test(void)
{
	int rc;
	uevent_t evt;
	handle_t hset1;

	_TEST_BEGIN(__func__);

	hset1 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset1, "create hset");

	_ABORT_IF_NOT_OK(abort_test);

	/* wait with zero timeout */
	rc = _trusty_wait(hset1, &evt, 0);
	_EXPECT_EQ(ERR_NOT_FOUND, rc, "wait on empty hset");

	/* wait with non-zero timeout */
	rc = _trusty_wait(hset1, &evt, 100);
	_EXPECT_EQ(ERR_NOT_FOUND, rc, "wait on empty hset");

	_trusty_close(hset1);

abort_test:
	_TEST_END
}



static void run_hset_wait_on_non_empty_set_test(void)
{
	int rc;
	handle_t hset1;
	handle_t hset2;

	_TEST_BEGIN(__func__);

	hset1 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset1, "create handle set1");

	hset2 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset2, "create handle set2");

	_ABORT_IF_NOT_OK(abort_test);

	uevent_t evt = {
		.handle = hset2,
		.event  = ~0,
		.cookie = NULL,
	};

	/* add hset2 to hset1 */
	rc = _trusty_handle_set_ctrl(hset1, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "add hset2 to hset1");

	/* wait with zero timeout on hset1 */
	rc = _trusty_wait(hset1, &evt, 0);
	_EXPECT_EQ(ERR_TIMED_OUT, rc, "wait on empty hset");

	/* wait with non-zero timeout on hset1 */
	rc = _trusty_wait(hset1, &evt, 100);
	_EXPECT_EQ(ERR_TIMED_OUT, rc, "wait on empty hset");

abort_test:
	_trusty_close(hset1);
	_trusty_close(hset2);

	_TEST_END
}


static void run_hset_add_chan_test(void)
{
	int rc;
	uevent_t evt;
	handle_t hset1;
	handle_t hset2;
	handle_t chan1;
	handle_t chan2;
	void *cookie1  = (void *)"cookie1";
	void *cookie2  = (void *)"cookie2";
	void *cookie11 = (void *)"cookie11";
	void *cookie12 = (void *)"cookie12";
	void *cookie21 = (void *)"cookie21";
	void *cookie22 = (void *)"cookie22";
	void *cookiehs2 = (void *)"cookiehs2";
	uint8_t   buf0[64];
	iovec_t   iov;
	ipc_msg_t msg;

	_TEST_BEGIN(__func__);

	/* prepare test buffer */
	fill_test_buf(buf0, sizeof(buf0), 0x55);

	chan1 = sync_connect( SRV_PATH_BASE ".srv.echo", 1000);
	_EXPECT_GT_ZERO ((int)chan1, "connect to echo chan1");

	rc = _trusty_set_cookie(chan1, cookie1);
	_EXPECT_EQ(0, rc, "cookie1");

	chan2 = sync_connect( SRV_PATH_BASE ".srv.echo", 1000);
	_EXPECT_GT_ZERO ((int)chan2, "connect to echo chan2");

	rc = _trusty_set_cookie(chan2, cookie2);
	_EXPECT_EQ(0, rc, "cookie2");

	/* send message over chan1 and chan2 */
	iov.base = buf0;
	iov.len  = sizeof(buf0);
	msg.num_handles = 0;
	msg.handles = NULL;
	msg.num_iov = 1;
	msg.iov = &iov;

	rc = _trusty_send_msg(chan1, &msg);
	_EXPECT_EQ(64, rc, "send over chan1");

	rc = _trusty_send_msg(chan2, &msg);
	_EXPECT_EQ(64, rc, "send over chan2");

	hset1 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset1, "create handle set1");

	hset2 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset2, "create handle set2");

	_ABORT_IF_NOT_OK(abort_test);

	/* chan1 to hset2 */
	evt.handle = chan1;
	evt.event  = ~0;
	evt.cookie = cookie12;
	rc = _trusty_handle_set_ctrl(hset2, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "add hset2 to hset1");

	/* chan2 to hset2 */
	evt.handle = chan2;
	evt.event  = ~0;
	evt.cookie = cookie22;
	rc = _trusty_handle_set_ctrl(hset2, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "add hset2 to hset1");

	/* add hset2 to hset1 */
	evt.handle = hset2;
	evt.event  = ~0;
	evt.cookie = cookiehs2;
	rc = _trusty_handle_set_ctrl(hset1, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "add hset2 to hset1");

	/* chan1 to hset1 */
	evt.handle = chan1;
	evt.event  = ~0;
	evt.cookie = cookie11;
	rc = _trusty_handle_set_ctrl(hset1, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "add hset2 to hset1");

	/* chan2 to hset1 */
	evt.handle = chan2;
	evt.event  = ~0;
	evt.cookie = cookie21;
	rc = _trusty_handle_set_ctrl(hset1, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "add hset2 to hset1");

	/* wait on chan1 directly */
	rc = _trusty_wait(chan1, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait on chan1");
	_EXPECT_EQ(chan1, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookie1, (uintptr_t)evt.cookie, "event.cookie");

	/* wait on chan2 directly */
	rc = _trusty_wait(chan2, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait on chan2");
	_EXPECT_EQ(chan2, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookie2, (uintptr_t)evt.cookie, "event.cookie");

	/* wait on hset1 */
	rc = _trusty_wait(hset1, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait on hset1");
	_EXPECT_EQ(hset2, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookiehs2, (uintptr_t)evt.cookie, "event.cookie");

	/* wait on hset1 again (chan1 should be ready) */
	rc = _trusty_wait(hset1, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait on hset1");
	_EXPECT_EQ(chan1, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookie11, (uintptr_t)evt.cookie, "event.cookie");

	/* wait on hset1 again (chan2 should be ready) */
	rc = _trusty_wait(hset1, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait on hset1");
	_EXPECT_EQ(chan2, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookie21, (uintptr_t)evt.cookie, "event.cookie");

	/* wait on hset1 again (hset2 should be ready) */
	rc = _trusty_wait(hset1, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait on hset1");
	_EXPECT_EQ(hset2, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookiehs2, (uintptr_t)evt.cookie, "event.cookie");

	/* wait on hset2 (chan1 should be ready) */
	rc = _trusty_wait(hset2, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait on hset2");
	_EXPECT_EQ(chan1, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookie12, (uintptr_t)evt.cookie, "event.cookie");

	/* wait on hset2 again (chan2 should be ready) */
	rc = _trusty_wait(hset2, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait on hset2");
	_EXPECT_EQ(chan2, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookie22, (uintptr_t)evt.cookie, "event.cookie");

	/* wait on hset2 again (chan1 should be ready) */
	rc = _trusty_wait(hset2, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait on chan1");
	_EXPECT_EQ(chan1, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookie12, (uintptr_t)evt.cookie, "event.cookie");

abort_test:
	_trusty_close(chan1);
	_trusty_close(chan2);
	_trusty_close(hset1);
	_trusty_close(hset2);

	_TEST_END
}


static void run_hset_event_mask_test(void)
{
	int rc;
	uevent_t evt;
	handle_t hset1;
	handle_t chan1;
	void *cookie1  = (void *)"cookie1";
	void *cookie11 = (void *)"cookie11";
	uint8_t   buf0[64];
	iovec_t   iov;
	ipc_msg_t msg;

	_TEST_BEGIN(__func__);

	/* prepare test buffer */
	fill_test_buf(buf0, sizeof(buf0), 0x55);

	chan1 = sync_connect( SRV_PATH_BASE ".srv.echo", 1000);
	_EXPECT_GT_ZERO ((int)chan1, "connect to echo");

	rc = _trusty_set_cookie(chan1, cookie1);
	_EXPECT_EQ(0, rc, "cookie1");

	/* send message over chan1 and chan2 */
	iov.base = buf0;
	iov.len  = sizeof(buf0);
	msg.num_handles = 0;
	msg.handles = NULL;
	msg.num_iov = 1;
	msg.iov = &iov;

	rc = _trusty_send_msg(chan1, &msg);
	_EXPECT_EQ(64, rc, "send over chan1");

	hset1 = _trusty_handle_set_create();
	_EXPECT_GE_ZERO((int)hset1, "create handle set1");

	_ABORT_IF_NOT_OK(abort_test);

	/* chan1 to hset1 */
	evt.handle = chan1;
	evt.event  = ~0;
	evt.cookie = cookie11;
	rc = _trusty_handle_set_ctrl(hset1, HSET_ADD, &evt);
	_EXPECT_EQ(0, rc, "add chan1 to hset1");

	/* wait of chan1 handle */
	rc = _trusty_wait(chan1, &evt, 100);
	_EXPECT_EQ(0, rc, "wait on chan1");
	_EXPECT_EQ(chan1, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookie1, (uintptr_t)evt.cookie, "event.cookie");

	/* wait on hset1 (should get chan1) */
	rc = _trusty_wait(hset1, &evt, 100);
	_EXPECT_EQ(0, rc, "wait on hset1");
	_EXPECT_EQ(chan1, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookie11, (uintptr_t)evt.cookie, "event.cookie");

	/* mask off chan1 in hset1 */
	evt.handle = chan1;
	evt.event  = 0;
	evt.cookie = cookie11;
	rc = _trusty_handle_set_ctrl(hset1, HSET_MOD, &evt);
	_EXPECT_EQ(0, rc, "mod chan1 in hset1");

	/* wait on hset1 (should get chan1) */
	rc = _trusty_wait(hset1, &evt, 100);
	_EXPECT_EQ(ERR_TIMED_OUT, rc, "wait on hset1");

	/* unmask off chan1 in hset1 */
	evt.handle = chan1;
	evt.event  = ~0;
	evt.cookie = cookie11;
	rc = _trusty_handle_set_ctrl(hset1, HSET_MOD, &evt);
	_EXPECT_EQ(0, rc, "mod chan1 in hset1");

	/* wait on hset1 (should get chan1) */
	rc = _trusty_wait(hset1, &evt, 100);
	_EXPECT_EQ(0, rc, "wait on hset1");
	_EXPECT_EQ(chan1, evt.handle, "event.handle");
	_EXPECT_EQ((uintptr_t)cookie11, (uintptr_t)evt.cookie, "event.cookie");

abort_test:
	_trusty_close(chan1);
	_trusty_close(hset1);
	_TEST_END
}


/****************************************************************************/

static void run_send_handle_test(void)
{
	int       rc;
	iovec_t   iov;
	ipc_msg_t msg;
	handle_t  hchan1;
	handle_t  hchan2;
	uint8_t   buf0[64];
	char path[MAX_PORT_PATH_LEN];

	_TEST_BEGIN(__func__);

	/* prepare test buffer */
	fill_test_buf(buf0, sizeof(buf0), 0x55);

	/* open connection to datasink service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "datasink");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");
	_ABORT_IF_NOT_OK(err_connect1);
	hchan1 = (handle_t)rc;

	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");
	_ABORT_IF_NOT_OK(err_connect2);
	hchan2 = (handle_t)rc;

	/* send hchan2 handle over hchan1 connection */
	iov.base = buf0;
	iov.len  = sizeof(buf0);
	msg.iov  = &iov;
	msg.num_iov = 1;
	msg.handles = &hchan2;
	msg.num_handles = 1;

	/* send and wait a bit */
	rc = _trusty_send_msg(hchan1, &msg);
	_EXPECT_EQ (64, rc, "send handle");
	_trusty_nanosleep(0, 0, 100 * MSEC);

	/* send it again and close it */
	rc = _trusty_send_msg(hchan1, &msg);
	_EXPECT_EQ (64, rc, "send handle");
	rc = _trusty_close(hchan2);
	_EXPECT_EQ (NO_ERROR, rc, "close chan2");

err_connect2:
	rc = _trusty_close(hchan1);
	_EXPECT_EQ (NO_ERROR, rc, "close chan1");
err_connect1:
	_TEST_END
}


static void run_send_handle_negative_test(void)
{
	int       rc;
	ipc_msg_t msg;
	handle_t  hchan;
	handle_t  hsend[10];
	char path[MAX_PORT_PATH_LEN];

	_TEST_BEGIN(__func__);

	/* open connection to datasink service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "datasink");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");
	_ABORT_IF_NOT_OK(err_connect);
	hchan = (handle_t)rc;

	for (uint i = 0; i < countof(hsend); i++)
		hsend[i] = hchan;

	/* send 8 copies of yourself to datasync (should be fine) */
	msg.iov  = NULL;
	msg.num_iov = 0;
	msg.handles = &hsend[0];
	msg.num_handles = 8;
	rc = _trusty_send_msg(hchan, &msg);
	_EXPECT_EQ (0, rc, "send handle");

	/* send 8 copies of yourself to datasync with handle poiting to NULL*/
	msg.iov  = NULL;
	msg.num_iov = 0;
	msg.handles = NULL;
	msg.num_handles = 8;
	rc = _trusty_send_msg(hchan, &msg);
	_EXPECT_EQ (ERR_FAULT, rc, "send handle");

	/* call with invalid handle should return ERR_FAULT */
	msg.handles = (handle_t *)0x100;
	msg.num_handles = 8;
	rc = _trusty_send_msg(hchan, &msg);
	_EXPECT_EQ (ERR_FAULT, rc, "send handle");

	/* send more then 8, should fail */
	msg.handles = &hsend[0];
	msg.num_handles = 10;
	rc = _trusty_send_msg(hchan, &msg);
	_EXPECT_EQ (ERR_TOO_BIG, rc, "send handle");

	rc = _trusty_close(hchan);
	_EXPECT_EQ (NO_ERROR, rc, "close chan");
err_connect:
	_TEST_END
}

static void run_recv_handle_test(void)
{
	int       rc;
	handle_t  hchan1;
	handle_t  hchan2;
	handle_t  hrecv[2];
	uint8_t   buf0[64];
	iovec_t   iov;
	ipc_msg_t msg;
	uevent_t  evt;
	ipc_msg_info_t inf;
	char path[MAX_PORT_PATH_LEN];

	_TEST_BEGIN(__func__);

	/* prepare test buffer */
	fill_test_buf(buf0, sizeof(buf0), 0x55);

	/* open connection to echo service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE, "echo");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to echo");
	_ABORT_IF_NOT_OK(err_connect1);
	hchan1 = (handle_t)rc;

	/* open second connection to echo service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE, "echo");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to echo");
	_ABORT_IF_NOT_OK(err_connect2);
	hchan2 = (handle_t)rc;

	/* send message with handle */
	iov.base = buf0;
	iov.len  = sizeof(buf0);
	msg.iov  = &iov;
	msg.num_iov = 1;
	msg.handles = &hchan2;
	msg.num_handles = 1;

	rc = _trusty_send_msg(hchan1, &msg);
	_EXPECT_EQ (64, rc, "send_handle");

	/* wait for reply */
	rc = _trusty_wait(hchan1, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait for reply");
	_EXPECT_EQ(hchan1, evt.handle, "event.handle");

	/* get reply message */
	rc = _trusty_get_msg(hchan1, &inf);
	_EXPECT_EQ (NO_ERROR, rc, "getting echo reply");
	_EXPECT_EQ (sizeof(buf0), inf.len, "reply len");
	_EXPECT_EQ (1, inf.num_handles, "reply num_handles");

	/* read reply data and no handles */
	hrecv[0] = INVALID_IPC_HANDLE;
	hrecv[1] = INVALID_IPC_HANDLE;
	msg.handles = &hrecv[0];
	msg.num_handles = 0;
	rc = _trusty_read_msg(hchan1, inf.id, 0, &msg);
	_EXPECT_EQ (64, rc, "reading echo reply");

	rc = _trusty_close(hrecv[0]);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "close reply handle");

	rc = _trusty_close(hrecv[1]);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "close reply handle");

	/* read reply data and 1 handle */
	hrecv[0] = INVALID_IPC_HANDLE;
	hrecv[1] = INVALID_IPC_HANDLE;
	msg.handles = &hrecv[0];
	msg.num_handles = 1;
	rc = _trusty_read_msg(hchan1, inf.id, 0, &msg);
	_EXPECT_EQ (64, rc, "reading echo reply");

	rc = _trusty_close(hrecv[0]);
	_EXPECT_EQ (0, rc, "close reply handle");

	rc = _trusty_close(hrecv[1]);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "close reply handle");

	/* read reply data and 2 handles (second one should be invalid) */
	hrecv[0] = INVALID_IPC_HANDLE;
	hrecv[1] = INVALID_IPC_HANDLE;
	msg.handles = &hrecv[0];
	msg.num_handles = 2;
	rc = _trusty_read_msg(hchan1, inf.id, 0, &msg);
	_EXPECT_EQ (64, rc, "reading echo reply");

	rc = _trusty_close(hrecv[0]);
	_EXPECT_EQ (0, rc, "close reply handle");

	rc = _trusty_close(hrecv[1]);
	_EXPECT_EQ (ERR_BAD_HANDLE, rc, "close reply handle");

	/* read 1 handle with no data */
	hrecv[0] = INVALID_IPC_HANDLE;
	hrecv[1] = INVALID_IPC_HANDLE;
	msg.num_iov = 0;
	msg.handles = &hrecv[0];
	msg.num_handles = 1;
	rc = _trusty_read_msg(hchan1, inf.id, 0, &msg);
	_EXPECT_EQ (0, rc, "reading echo reply");

	_EXPECT_EQ (INVALID_IPC_HANDLE, hrecv[1], "reading echo reply");

	/* read same handle for the second time */
	msg.handles = &hrecv[1];
	msg.num_handles = 1;
	rc = _trusty_read_msg(hchan1, inf.id, 0, &msg);
	_EXPECT_EQ (0, rc, "reading echo reply");

	rc = _trusty_close(hrecv[0]);
	_EXPECT_EQ (0, rc, "close reply handle");

	rc = _trusty_close(hrecv[1]);
	_EXPECT_EQ (0, rc, "close reply handle");

	/* discard reply */
	rc = _trusty_put_msg(hchan1, inf.id);
	_EXPECT_EQ (NO_ERROR, rc, "putting echo reply");

	_trusty_close(hchan2);
	_EXPECT_EQ (NO_ERROR, rc, "close chan2");
err_connect2:
	_trusty_close(hchan1);
	_EXPECT_EQ (NO_ERROR, rc, "close chan1");
err_connect1:
	_TEST_END
}


static void run_recv_handle_negative_test(void)
{
	int       rc;
	handle_t  hchan1;
	ipc_msg_t msg;
	uevent_t  evt;
	ipc_msg_info_t inf;
	char path[MAX_PORT_PATH_LEN];

	_TEST_BEGIN(__func__);

	/* open connection to echo service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE, "echo");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to echo");
	_ABORT_IF_NOT_OK(err_connect1);
	hchan1 = (handle_t)rc;

	/* send message with handle attached */
	msg.iov = NULL;
	msg.num_iov = 0;
	msg.handles = &hchan1;
	msg.num_handles = 1;

	rc = _trusty_send_msg(hchan1, &msg);
	_EXPECT_EQ (0, rc, "send_handle");

	/* wait for reply */
	rc = _trusty_wait(hchan1, &evt, 1000);
	_EXPECT_EQ(0, rc, "wait for reply");
	_EXPECT_EQ(hchan1, evt.handle, "event.handle");

	/* get reply message */
	rc = _trusty_get_msg(hchan1, &inf);
	_EXPECT_EQ (NO_ERROR, rc, "getting echo reply");
	_EXPECT_EQ (0, inf.len, "reply len");
	_EXPECT_EQ (1, inf.num_handles, "reply num_handles");

	/* read reply data with handles pointing to NULL */
	msg.handles = NULL;
	msg.num_handles = 1;
	rc = _trusty_read_msg(hchan1, inf.id, 0, &msg);
	_EXPECT_EQ (ERR_FAULT, rc, "reading echo reply");

	/* read reply data and bad handle ptr */
	msg.handles = (handle_t *)0x100;
	msg.num_handles = 1;
	rc = _trusty_read_msg(hchan1, inf.id, 0, &msg);
	_EXPECT_EQ (ERR_FAULT, rc, "reading echo reply");

	/* discard reply */
	rc = _trusty_put_msg(hchan1, inf.id);
	_EXPECT_EQ (NO_ERROR, rc, "putting echo reply");

	rc = _trusty_close(hchan1);
	_EXPECT_EQ (NO_ERROR, rc, "close chan1");
err_connect1:
	_TEST_END
}


static void run_send_handle_bulk_test(void)
{
	int       rc;
	iovec_t   iov;
	ipc_msg_t msg;
	handle_t  hchan1;
	handle_t  hchan2;
	uint8_t   buf0[64];
	char path[MAX_PORT_PATH_LEN];

	_TEST_BEGIN(__func__);

	/* prepare test buffer */
	fill_test_buf(buf0, sizeof(buf0), 0x55);

	/* open connection to datasink service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE,  "datasink");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");
	_ABORT_IF_NOT_OK(err_connect1);
	hchan1 = (handle_t)rc;

	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to datasink");
	_ABORT_IF_NOT_OK(err_connect2);
	hchan2 = (handle_t)rc;

	/* send hchan2 handle over hchan1 connection */
	iov.base = buf0;
	iov.len  = sizeof(buf0);
	msg.iov  = &iov;
	msg.num_iov = 1;
	msg.handles = &hchan2;
	msg.num_handles = 1;

	for (uint i = 0; (i < 10000) && _all_ok; i++) {
		while (_all_ok) {
			rc = _trusty_send_msg(hchan1, &msg);
			if (rc == ERR_NOT_ENOUGH_BUFFER) { /* wait for room */
				uevent_t uevt;
				uint exp_event = IPC_HANDLE_POLL_SEND_UNBLOCKED;
				rc = _trusty_wait(hchan1, &uevt, 10000);
				_EXPECT_EQ (NO_ERROR, rc, "waiting for space");
				_EXPECT_EQ (hchan1, uevt.handle, "waiting for space");
				_EXPECT_EQ (exp_event, uevt.event,  "waiting for space");
			} else {
				_EXPECT_EQ (64, rc, "send_msg bulk");
				break;
			}
		}
	}
	rc = _trusty_close(hchan2);
	_EXPECT_EQ (NO_ERROR, rc, "close chan2");

	/* repeate the same while closing handle after sending it */
	for (uint i = 0; (i < 10000) && _all_ok; i++) {

		rc = sync_connect(path, 1000);
		_EXPECT_GT_ZERO (rc, "connect to datasink");
		_ABORT_IF_NOT_OK(err_connect2);
		hchan2 = (handle_t)rc;

		/* send hchan2 handle over hchan1 connection */
		iov.base = buf0;
		iov.len  = sizeof(buf0);
		msg.iov  = &iov;
		msg.num_iov = 1;
		msg.handles = &hchan2;
		msg.num_handles = 1;

		while (_all_ok) {
			rc = _trusty_send_msg(hchan1, &msg);
			if (rc == ERR_NOT_ENOUGH_BUFFER) { /* wait for room */
				uevent_t uevt;
				uint exp_event = IPC_HANDLE_POLL_SEND_UNBLOCKED;
				rc = _trusty_wait(hchan1, &uevt, 10000);
				_EXPECT_EQ (NO_ERROR, rc, "waiting for space");
				_EXPECT_EQ (hchan1, uevt.handle, "waiting for space");
				_EXPECT_EQ (exp_event, uevt.event,  "waiting for space");
			} else {
				_EXPECT_EQ (64, rc, "send_msg bulk");
				break;
			}
		}
		rc = _trusty_close(hchan2);
		_EXPECT_EQ (NO_ERROR, rc, "close chan2");
	}

err_connect2:
	rc = _trusty_close(hchan1);
	_EXPECT_EQ (NO_ERROR, rc, "close chan1");
err_connect1:
	_TEST_END
}


static void run_echo_handle_bulk_test(void)
{
	int       rc;
	handle_t  hchan1;
	handle_t  hchan2;
	handle_t  hrecv;
	uint8_t   buf0[64];
	iovec_t   iov;
	ipc_msg_t msg;
	uevent_t  evt;
	ipc_msg_info_t inf;
	char path[MAX_PORT_PATH_LEN];

	_TEST_BEGIN(__func__);

	/* prepare test buffer */
	fill_test_buf(buf0, sizeof(buf0), 0x55);

	/* open connection to echo service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE, "echo");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to echo");
	_ABORT_IF_NOT_OK(err_connect1);
	hchan1 = (handle_t)rc;

	/* open second connection to echo service */
	sprintf(path, "%s.srv.%s", SRV_PATH_BASE, "echo");
	rc = sync_connect(path, 1000);
	_EXPECT_GT_ZERO (rc, "connect to echo");
	_ABORT_IF_NOT_OK(err_connect2);
	hchan2 = (handle_t)rc;

	/* send the same handle 10000 times */
	for (uint i = 0; (i < 10000) && _all_ok; i++) {
		/* send message with handle */
		iov.base = buf0;
		iov.len  = sizeof(buf0);
		msg.iov  = &iov;
		msg.num_iov = 1;
		msg.handles = &hchan2;
		msg.num_handles = 1;

		while (_all_ok) {
			rc = _trusty_send_msg(hchan1, &msg);
			_EXPECT_EQ (64, rc, "send_handle");
			if (rc == ERR_NOT_ENOUGH_BUFFER) { /* wait for room */
				uevent_t uevt;
				uint exp_event = IPC_HANDLE_POLL_SEND_UNBLOCKED;
				rc = _trusty_wait(hchan1, &uevt, 10000);
				_EXPECT_EQ (NO_ERROR, rc, "waiting for space");
				_EXPECT_EQ (hchan1, uevt.handle, "waiting for space");
				_EXPECT_EQ (exp_event, uevt.event,  "waiting for space");
			} else {
				_EXPECT_EQ (64, rc, "send_msg bulk");
				break;
			}
		}

		/* wait for reply */
		rc = _trusty_wait(hchan1, &evt, 1000);
		_EXPECT_EQ(0, rc, "wait for reply");
		_EXPECT_EQ(hchan1, evt.handle, "event.handle");

		/* get reply message */
		rc = _trusty_get_msg(hchan1, &inf);
		_EXPECT_EQ (NO_ERROR, rc, "getting echo reply");
		_EXPECT_EQ (sizeof(buf0), inf.len, "reply len");
		_EXPECT_EQ (1, inf.num_handles, "reply num_handles");

		/* read reply data and 1 handle */
		hrecv = INVALID_IPC_HANDLE;
		msg.handles = &hrecv;
		msg.num_handles = 1;
		rc = _trusty_read_msg(hchan1, inf.id, 0, &msg);
		_EXPECT_EQ (64, rc, "reading echo reply");

		/* discard reply */
		rc = _trusty_put_msg(hchan1, inf.id);
		_EXPECT_EQ (NO_ERROR, rc, "putting echo reply");

		/* close received handle */
		rc = _trusty_close(hrecv);
		_EXPECT_EQ (0, rc, "close reply handle");
	}

	rc = _trusty_close(hchan2);
	_EXPECT_EQ (NO_ERROR, rc, "close chan2");
err_connect2:
	rc = _trusty_close(hchan1);
	_EXPECT_EQ (NO_ERROR, rc, "close chan1");
err_connect1:
	_TEST_END
}

/****************************************************************************/

/*
 *
 */
static void run_all_tests (void)
{
	TLOGI ("Run all unittest\n");

	/* reset test state */
	_tests_total  = 0;
	_tests_failed = 0;
#if 1
	/* handle sets: part 1 */
	run_hset_create_test();
	run_hset_add_mod_del_test();
	run_hset_add_self_test();
	run_hset_add_loop_test();
	run_hset_add_duplicate_test();
	run_hset_wait_on_empty_set_test();
	run_hset_wait_on_non_empty_set_test();

	/* positive tests */
	run_port_create_test();
	run_wait_on_port_test();
	run_async_connect_test();
	run_connect_close_test();
	run_accept_test();
	run_send_msg_test();
	run_end_to_end_msg_test();

	run_connect_close_by_peer_test("closer1");
#endif
	run_connect_close_by_peer_test("closer2");
#if 1
	run_connect_close_by_peer_test("closer3");
	run_connect_selfie_test();
	run_connect_access_test();

	/* negative tests */
	run_wait_negative_test();
	run_close_handle_negative_test();
	run_set_cookie_negative_test();
	run_port_create_negative_test();
	run_connect_negative_test();
	run_accept_negative_test();
	run_get_msg_negative_test();
	run_put_msg_negative_test();
	run_send_msg_negative_test();
	run_read_msg_negative_test();

	/* handle sets: part 2 */
	run_hset_add_chan_test();
	run_hset_event_mask_test();

	/* send handle */
	run_send_handle_test();
	run_send_handle_negative_test();
	run_recv_handle_test();
	run_recv_handle_negative_test();

	run_send_handle_bulk_test();
	run_echo_handle_bulk_test();
#endif

	TLOGI("Conditions checked: %d\n", _tests_total);
	TLOGI("Conditions failed:  %d\n", _tests_failed);
	if (_tests_failed == 0)
		TLOGI("All tests PASSED\n");
	else
		TLOGI("Some tests FAILED\n");
}

int main(int argc, char** argv) {
	int rc;
	char path[MAX_PORT_PATH_LEN];
	uuid_t peer_uuid;

	TLOGI ("Welcome to IPC unittest!!!\n");

	/* create control port and just wait on it */
        sprintf(path, "%s.%s", SRV_PATH_BASE, "ctrl");
	rc = _trusty_port_create(path,  1, MAX_PORT_BUF_SIZE,
			         IPC_PORT_ALLOW_NS_CONNECT);
	if (rc < 0) {
		TLOGI("failed (%d) to create ctrl port\n", rc );
		return rc;
	}
	handle_base = (handle_t)rc;

	/* and just wait forever for now */
	TLOGI("waiting forever\n");
	for (;;) {
		uevent_t uevt;
		int rc = _trusty_wait_any(&uevt, -1);
		TLOGI("got event (rc=%d): ev=%x handle=%d\n",
		       rc, uevt.event, uevt.handle);
		if (rc == NO_ERROR) {
			if (uevt.event & IPC_HANDLE_POLL_READY) {
				/* get connection request */
				rc = _trusty_accept(uevt.handle, &peer_uuid);
				if (rc >= 0) {
					/* then run unittest test */
					run_all_tests();

					/* and close it */
					_trusty_close(rc);
				}
			}
		}
		if (rc < 0)
			break;
	}

	return rc;
}
