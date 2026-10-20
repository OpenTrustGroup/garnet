#include "set_info.h"


#ifndef msleep
#define msleep(ms) zx_nanosleep(zx_deadline_after(ZX_MSEC(ms)));
#endif

#include "ufsinfo.h"
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <trusty_std.h>

void init_key_value_store(KeyValueStore* store) {
    if (store == NULL) return;

    store->count = 0;
    store->total_pairs_allocated = INFO_MAX_ENTRIES;

    for (int i = 0; i < INFO_MAX_ENTRIES; i++) {
        store->pairs[i].name[0] = '\0';
        store->pairs[i].value[0] = '\0';
    }
}

bool parse_key_value_string(const char* input, KeyValueStore* store) {
    if (input == NULL || store == NULL) {
        return false;
    }

//    store->count = 0;

    const char* current = input;
    const char* comma_pos;

    while (*current != '\0' && store->count < INFO_MAX_ENTRIES) {

        while (*current == ' ' || *current == '\t') {
            current++;
        }

        if (*current == '\0') {
            break;
        }

        comma_pos = strchr(current, ',');

        const char* segment_end = comma_pos ? comma_pos : current + strlen(current);

        const char* eq_pos = strchr(current, '=');
        if (eq_pos == NULL || eq_pos >= segment_end) {
            if (comma_pos) {
                current = comma_pos + 1;
                continue;
            } else {
                break;
            }
        }

        size_t name_len = eq_pos - current;
        if (name_len > 0 && name_len < INFO_NAME_LEN) {
            strncpy(store->pairs[store->count].name, current, name_len);
            store->pairs[store->count].name[name_len] = '\0';

            char* name_ptr = store->pairs[store->count].name;
            while (name_len > 0 &&
                   (name_ptr[name_len-1] == ' ' || name_ptr[name_len-1] == '\t')) {
                name_ptr[name_len-1] = '\0';
                name_len--;
            }
        } else if (name_len >= INFO_NAME_LEN) {
            strncpy(store->pairs[store->count].name, current, INFO_NAME_LEN - 1);
            store->pairs[store->count].name[INFO_NAME_LEN - 1] = '\0';
        }

        const char* value_start = eq_pos + 1;
        size_t value_len = segment_end - value_start;

        if (value_len > 0 && value_len < INFO_VALUE_LEN) {
            strncpy(store->pairs[store->count].value, value_start, value_len);
            store->pairs[store->count].value[value_len] = '\0';

            char* value_ptr = store->pairs[store->count].value;
            while (value_len > 0 &&
                   (value_ptr[value_len-1] == ' ' || value_ptr[value_len-1] == '\t' ||
                    value_ptr[value_len-1] == '\n')) {
                value_ptr[value_len-1] = '\0';
                value_len--;
            }
        } else if (value_len >= INFO_VALUE_LEN) {
            strncpy(store->pairs[store->count].value, value_start, INFO_VALUE_LEN - 1);
            store->pairs[store->count].value[INFO_VALUE_LEN - 1] = '\0';
        }

        store->count++;

        if (comma_pos) {
            current = comma_pos + 1;
        } else {
            break;
        }
    }

    return true;
}

void print_key_value_store(const KeyValueStore* store) {
    if (store == NULL) {
        printf("Store is NULL\n");
        return;
    }

    printf("KeyValueStore (count: %d, allocated: %d):\n",
           store->count, store->total_pairs_allocated);
    printf("========================================\n");

    for (int i = 0; i < store->count; i++) {
        printf("%2d. Name:  \"%s\"\n", i + 1, store->pairs[i].name);
        printf("    Value: \"%s\"\n", store->pairs[i].value);
    }

    printf("========================================\n");
}

zx_status_t storage_device_info(char *value) {
	KeyValueStore store;
	printf("\n[%s] %s,\n", __func__, value);
	init_key_value_store(&store);
	KeyValueStore out;
      _trusty_ioctl(SYS_PLATFORM_FD,
		SYS_PLATFORM_GET_UFS_INFO, (void*)&out);
	printf("\n[%s] out.count %d,\n", __func__, out.count);
	if (0 != out.count) {
		memcpy(&store, &out, sizeof(KeyValueStore));
	}

	parse_key_value_string(value, &store);

	print_key_value_store(&store);

	_trusty_ioctl(SYS_PLATFORM_FD,
		SYS_PLATFORM_SET_UFS_INFO, (void*)&store);

	return ZX_OK;
}