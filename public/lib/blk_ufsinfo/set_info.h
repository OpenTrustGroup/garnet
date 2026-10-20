#ifndef _SET_INFO_H_
#define _SET_INFO_H_

#include <zircon/types.h>
#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define INFO_MAX_ENTRIES 40
#define INFO_NAME_LEN 32
#define INFO_VALUE_LEN 128

typedef struct {
    char name[INFO_NAME_LEN];
    char value[INFO_VALUE_LEN];
} KeyValuePair;

typedef struct {
    KeyValuePair pairs[INFO_MAX_ENTRIES];
    int count;
    int total_pairs_allocated;
} KeyValueStore;

zx_status_t storage_device_info(char *value);

#endif  // _SET_INFO_H_