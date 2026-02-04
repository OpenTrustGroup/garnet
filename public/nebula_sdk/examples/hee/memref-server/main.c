/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uapi/err.h>
#include <trusty_std.h>
#include <trusty/sys/types.h>

#define LOG_TAG "memref_sample_server"

#define CMD_SEND_MEMREF "cmd_send_memref"
#define ACK_OK "ok"
#define MSG_WITH_TIPC "This message is sent with tipc."
#define MSG_WITH_MEMREF "This message is sent with memref."
#define MSG_ACK_WITH_MEMREF "This message is returned by HEE with memref."

#define MAX_PORT_BUF_SIZE  4096    /* max size of per port buffer    */
#define MAX_HANDLE_COUNT 8 /* max handle of per channel */

#define TLOGI(fmt, ...) \
    fprintf(stderr, "%s: %d: " fmt, LOG_TAG, __LINE__,  ## __VA_ARGS__)

#define TLOGE(fmt, ...) \
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
	if (_e != _a) {                                         \
		TLOGI("%s: expected " #expected " (%d), "      \
		    "actual " #actual " (%d)\n",               \
		    msg, (int)_e, (int)_a);                     \
		_all_ok = 0;                                \
	}                                                       \
}

#define _EXPECT_GT(expected, actual, msg)                        \
{                                                               \
	__typeof__(actual) _e = expected;                       \
	__typeof__(actual) _a = actual;                         \
	if (_e <= _a) {                                         \
		TLOGI("%s: expected " #expected " (%d), "      \
		    "actual " #actual " (%d)\n",                \
		    msg, (int)_e, (int)_a);                     \
		_all_ok = 0;                                \
	}                                                       \
}

#define _EXPECT_GE_ZERO(actual, msg)                             \
{                                                               \
	__typeof__(actual) _a = actual;                         \
	if (_a < 0) {                                           \
		TLOGI("%s: expected >= 0 "                     \
		    "actual " #actual " (%d)\n", msg, (int)_a); \
		_all_ok = 0;                                \
	}                                                       \
}


#define _EXPECT_GT_ZERO(actual, msg)                             \
{                                                               \
	__typeof__(actual) _a = actual;                         \
	if (_a <= 0) {                                          \
		TLOGI("%s: expected > 0 "                      \
		    "actual " #actual " (%d)\n", msg, (int)_a); \
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


typedef struct my_session {
	// Here can customize some session related variables. ex:uuid
	uuid_t peer_uuid;
} my_session_t;

/*
 *  Message handler
 */
static int handle_msg(handle_t chan, void *cookie) {
	int rc;
	iovec_t iov;
	ipc_msg_t msg;
	ipc_msg_info_t msg_inf;
	my_session_t *session = (my_session_t *)cookie;
	char msg_buf[MAX_PORT_BUF_SIZE];
	handle_t handles[MAX_HANDLE_COUNT];

	/* get message info */
	rc = _trusty_get_msg(chan, &msg_inf);
	if (rc == ERR_NO_MSG)
		return NO_ERROR; /* no new messages */

	if (rc != NO_ERROR) {
		TLOGE("failed (%d) to get_msg for chan (%d)\n", rc, chan);
		return rc;
	}

	iov.base = msg_buf;
	iov.len = msg_inf.len;

	msg.num_iov = 1;
	msg.iov = &iov;
	msg.num_handles = msg_inf.num_handles;
	msg.handles = handles;

	/* read msg content */
	rc = _trusty_read_msg(chan, msg_inf.id, 0, &msg);
	if (rc < 0) {
		TLOGE("failed (%d) to read_msg for chan (%d)\n", rc, chan);
		return rc;
	}

	// Here can do some session related operations.
	TLOGI("uuid time low:%x\n", session->peer_uuid.time_low);

	if (strcmp(msg_buf, CMD_SEND_MEMREF) == 0) {
		size_t len = rc;


		if (msg_inf.num_handles == 1) {
			// The size 4096 can be transfered from msg_buf.
			void *base = (void *) _trusty_mmap(NULL, 4096,
					MMAP_FLAG_PROT_RW | MMAP_FLAG_MEMREF_HANDLE, handles[0]);

			if (base) {
				char *ref_msg = base;

				_TEST_BEGIN("send_msg_with_memref")

				_EXPECT_EQ(0, strcmp(ref_msg, MSG_WITH_MEMREF), "msg mismatch");
				printf("memref msg: %s\n", ref_msg);

				len = strlen(ACK_OK);
				strcpy(msg_buf, ACK_OK);
				strcpy(ref_msg, MSG_ACK_WITH_MEMREF);

				_trusty_munmap(base, 4096);

				_TEST_END
			}
			_trusty_close(handles[0]);
		}

		iov.len = len;
		msg.num_handles = 0;

		/* send message back to the caller */
		rc = _trusty_send_msg(chan, &msg);
		if (rc < 0) {
			TLOGE("failed (%d) to send_msg for chan (%d)\n", rc, chan);
			return rc;
		}
	} else {
		/* update number of bytes received */
		iov.len = (size_t) rc;
		msg.num_handles = 0;

		_TEST_BEGIN("send_msg_with_tipc")

		_EXPECT_EQ(0, strcmp(msg_buf, MSG_WITH_TIPC), "msg mismatch");
		printf("tipc msg: %s\n", msg_buf);

		_TEST_END

		/* send message back to the caller */
		rc = _trusty_send_msg(chan, &msg);
		if (rc < 0) {
			TLOGE("failed (%d) to send_msg for chan (%d)\n", rc, chan);
			return rc;
		}
	}


	/* retire message */
	rc = _trusty_put_msg(chan, msg_inf.id);
	if (rc != NO_ERROR) {
		TLOGE("failed (%d) to put_msg for chan (%d)\n", rc, chan);
		return rc;
	}

	return NO_ERROR;
}

/*
 *  Channel event handler
 */
static void handle_channel_event(const uevent_t *ev) {
	int rc;

	if (ev->event & IPC_HANDLE_POLL_MSG) {
		rc = handle_msg(ev->handle, ev->cookie);
		if (rc != NO_ERROR) {
			/* report an error and close channel */
			TLOGE("failed (%d) to handle event on channel %d\n", rc,
					ev->handle);
			_trusty_close(ev->handle);
		}
		return;
	}
	if (ev->event & IPC_HANDLE_POLL_HUP) {
		if (ev->cookie) {
			free(ev->cookie);
		}

		/* closed by peer. */
		_trusty_close(ev->handle);
		return;
	}
}

/*
 *  Port event handler
 */
static void handle_port_event(const uevent_t *ev) {
	uuid_t peer_uuid;

	if ((ev->event & IPC_HANDLE_POLL_ERROR) || (ev->event & IPC_HANDLE_POLL_HUP)
			|| (ev->event & IPC_HANDLE_POLL_MSG)
			|| (ev->event & IPC_HANDLE_POLL_SEND_UNBLOCKED)) {
		/* should never happen with port handles */
		TLOGE("error event (0x%x) for port (%d)\n", ev->event, ev->handle);
		abort();
	}
	if (ev->event & IPC_HANDLE_POLL_READY) {
		/* incoming connection: accept it */
		int rc = _trusty_accept(ev->handle, &peer_uuid);
		if (rc < 0) {
			TLOGE("failed (%d) to accept on port %d\n", rc, ev->handle);
			return;
		}

		my_session_t *session = malloc(sizeof(my_session_t));
		rc = _trusty_set_cookie(rc, session);
		if (rc < 0) {
			TLOGE("failed (%d) to set cookie on port %d\n", rc, ev->handle);
			return;
		}
	}
}

/****************************************************************************/
int main(int argc, char** argv) {
	int rc;
	handle_t port;

	TLOGI("Welcome to memref_sample_server!!!\n");

	/* create control port and just wait on it */
	rc = _trusty_port_create("com.grt.nbl.memref.sample.server", 1, MAX_PORT_BUF_SIZE,
				 IPC_PORT_ALLOW_NS_CONNECT);
	if (rc < 0) {
		TLOGI("failed (%d) to create ctrl port\n", rc);
		return rc;
	}

	port = rc;

	/* and just wait forever for now */
	TLOGI("waiting forever\n");
	for (;;) {
		uevent_t uevt;
		int rc = _trusty_wait_any(&uevt, -1);
		TLOGI("got event (rc=%d): ev=%x handle=%d\n", rc, uevt.event,
				uevt.handle);
		if (rc == NO_ERROR) {
			/* got an event */
			if (uevt.handle == port) {
				handle_port_event(&uevt);
			} else {
				handle_channel_event(&uevt);
			}
		}
		if (rc < 0)
			break;
	}

	return rc;
}
