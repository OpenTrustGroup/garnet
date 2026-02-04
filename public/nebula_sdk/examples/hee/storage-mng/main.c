// Copyright 2018 GoldenRiver Technologies Co., Ltd. All rights reserved.
//
// The present software is the confidential and proprietary information of
// GoldenRiver Technologies Co., Ltd. You shall not disclose the present
// software and shall use it only in accordance with the terms of the license
// agreement you entered into with GoldenRiver Technologies Co., Ltd.
// This software may be subject to export or import laws in certain countries.

/**
 * This test application creates and search file in its own security storage.
 * The developers use it mainly for testing persistent storage event if the system is restarted.
 */

#include <assert.h>
#include <uapi/err.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <libgen.h>

#include <getopt.h>

#include <lib/storage/storage.h>

static inline int MIN(int x, int y)
{
	return x < y ? x: y;
}

#define LOG_TAG "storage_mng"
#define TLOGE(fmt, ...) \
    fprintf(stderr, "[E]%s:%d:" fmt, LOG_TAG, __LINE__,  ## __VA_ARGS__)

void print_usage(char* self) {
    printf("Usage: %s, manage the files in secure storage.\n", self);
    printf("  -h show help information.\n");
    printf("  -c <file name> create a file\n");
    printf("  -d <data> input the data into the given file by -c/-s\n");
    printf("  -s <file name> show state, if not find, return 1.\n");
    printf("  -w <file name> search a file, if find, show content of it.\n");
    printf("  -l list all files of this HA.\n");
}

typedef enum
{
	CREATE = 1,
	RENAME = 2,
	REMOVE = 3,
	STATE  = 4,
	LIST   = 5,
	SHOW   = 6,
} OpCode;

static const struct option opts[] = {
	{"create", required_argument, NULL, 'c'},
	{"rename", required_argument, NULL, 'n'},
	{"remove", required_argument, NULL, 'r'},
	{"state",  required_argument, NULL, 's'},
	{"data",   required_argument, NULL, 'd'},
	{"list",   no_argument,       NULL, 'l'},
	{"show",   required_argument, NULL, 'w'},
	{"help",   no_argument,       NULL, 'h'},
	{0, 0, 0, 0}
};

int do_remove(storage_session_t session, const char * fn)
{
	return storage_delete_file(session, fn,STORAGE_OP_COMPLETE);
}

int do_create(storage_session_t session, const char * fn, const char * data)
{
	int rlt;
	file_handle_t handle;
	uint32_t flags = STORAGE_FILE_OPEN_CREATE | STORAGE_FILE_OPEN_TRUNCATE;
	uint32_t opflags = STORAGE_OP_COMPLETE;

	rlt = storage_open_file(session, &handle, fn, flags, opflags);
	if (rlt < 0) {
		return -1;
	}

	// if has data, should write data into file.
	if (strlen(data)) {
		rlt = storage_write(handle, 0, data, strlen(data), STORAGE_OP_COMPLETE);
		if (rlt < 0) {
			goto end;
		}
	}

end:
	storage_close_file(handle);

	return rlt;
}

int do_list(storage_session_t session)
{
	int rlt;
	char fn[STORAGE_MAX_NAME_LENGTH_BYTES + 1];

	struct storage_open_dir_state *state;
	rlt = storage_open_dir(session, "", &state);

	do {
		uint8_t flags;
		memset(fn, 0, sizeof(fn));
		rlt = storage_read_dir(session, state, &flags, fn, sizeof(fn));
		if (rlt < 0) {
			break;
		}
		printf("name:%#x:%d:\"%s\".\n", flags, rlt, fn);

	} while(1);

	storage_close_dir(session, state);

	return 0;
}

int do_show(storage_session_t session, const char * fn)
{
	int rlt;
	file_handle_t handle;
	uint32_t flags = 0;
	uint32_t opflags = STORAGE_OP_COMPLETE;

	rlt = storage_open_file(session, &handle, fn, flags, opflags);
	if (rlt < 0) {
		printf("this file doesn't exist.\n");
		return -1;
	}

	ssize_t size;
	char buf[16];
	storage_off_t off = 0;
	do {
		memset(buf, 0, sizeof(buf));
		size = storage_read(handle, off, buf, sizeof(buf)-1);
		printf("%s", buf);
		off += size;
	} while(size > 0);
	printf("\n");

	storage_close_file(handle);

	return rlt;
}

int do_state(storage_session_t session, const char * fn)
{
	int rlt;
	file_handle_t handle;
	uint32_t flags = 0;
	uint32_t opflags = STORAGE_OP_COMPLETE;

	rlt = storage_open_file(session, &handle, fn, flags, opflags);
	if (rlt < 0) {
		printf("this file doesn't exist.\n");
		return -1;
	}

	storage_off_t size;
	rlt = storage_get_file_size(handle, &size);
	printf("size=%lu\n", size);

	storage_close_file(handle);

	return rlt;
}

int main(int argc, char* argv[])
{
	int rlt;
	int opt;
	OpCode opcode = 0;
	char fn[STORAGE_MAX_NAME_LENGTH_BYTES + 1];
	char data[1024];
	char * program = basename(argv[0]);

	memset(fn, 0, sizeof(fn));
	memset(data, 0, sizeof(data));

	// parse the args
	while ((opt = getopt_long(argc, argv, "c:n:r:s:d:lhw:", opts, NULL)) != -1) {
		switch (opt) {
		case 'c':
			opcode = CREATE;
			memcpy(fn, optarg, MIN(sizeof(fn), strlen(optarg)+1));
			break;
		case 'n':
			opcode = RENAME;
			memcpy(fn, optarg, MIN(sizeof(fn), strlen(optarg)+1));
			break;
		case 'r':
			opcode = REMOVE;
			memcpy(fn, optarg, MIN(sizeof(fn), strlen(optarg)+1));
			break;
		case 's':
			opcode = STATE;
			memcpy(fn, optarg, MIN(sizeof(fn), strlen(optarg)+1));
			break;
		case 'd': // save the data.
			memcpy(data, optarg, MIN(sizeof(data), strlen(optarg)+1));
			break;
		case 'l':
			opcode = LIST;
			break;
		case 'w':
			opcode = SHOW;
			memcpy(fn, optarg, MIN(sizeof(fn), strlen(optarg)+1));
			break;
		case 'h':
			print_usage(program);
			return 0;
			break;
		default:
			print_usage(program);
			return 1;
		}
	}

	if (!opcode) {
		print_usage(program);
		return 1;
	}

	storage_session_t session;
	rlt = storage_open_session(&session, STORAGE_CLIENT_TD_PORT);
	if (rlt < 0) {
		TLOGE("Failed to open session.\n");
		return 1;
	}

	switch(opcode) {
	case CREATE:
		do_create(session, fn, data);
		break;
	case RENAME:
		//do_rename(session, old_fn, new_fn);
		break;
	case REMOVE:
		do_remove(session, fn);
		break;
	case STATE:
		do_state(session, fn);
		break;
	case LIST:
		do_list(session);
		break;
	case SHOW:
		do_show(session, fn);
		break;
	}

	storage_close_session(session);

	return 0;
}
