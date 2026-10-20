#include "ufsinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <trusty_std.h>
#define MAX_LINE_LENGTH 1024
#define MAX_NAME_LENGTH 256
#define MAX_VALUE_LENGTH 768
#define TEMP_FILE_SUFFIX ".tmp"



static int ensure_directory(const char* path) {
    char dir_path[512];
    char* p;

    strncpy(dir_path, path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';

    for (p = dir_path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(dir_path, 0755);
            *p = '/';
        }
    }

    return 0;
}

static bool validate_name(const char* name) {
    if (!name || name[0] == '\0') {
        return false;
    }

    for (int i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (c == '=' || c == ',' || c == '\n' || c == '\r') {
            return false;
        }
    }

    return true;
}

static void escape_string(const char* src, char* dst, size_t dst_size) {
    size_t j = 0;

    for (size_t i = 0; src[i] != '\0' && j < dst_size - 1; i++) {
        char c = src[i];
        if (c == '\\' || c == ',' || c == '=') {
            if (j < dst_size - 2) {
                dst[j++] = '\\';
                dst[j++] = c;
            }
        } else if (c == '\n') {
            if (j < dst_size - 2) {
                dst[j++] = '\\';
                dst[j++] = 'n';
            }
        } else if (c == '\r') {
            if (j < dst_size - 2) {
                dst[j++] = '\\';
                dst[j++] = 'r';
            }
        } else {
            dst[j++] = c;
        }
    }

    dst[j] = '\0';
}

static void unescape_string(const char* src, char* dst, size_t dst_size) {
    size_t j = 0;

    for (size_t i = 0; src[i] != '\0' && j < dst_size - 1; i++) {
        char c = src[i];

        if (c == '\\' && src[i + 1] != '\0') {
            char next = src[++i];
            switch (next) {
                case 'n': dst[j++] = '\n'; break;
                case 'r': dst[j++] = '\r'; break;
                case '\\': dst[j++] = '\\'; break;
                case ',': dst[j++] = ','; break;
                case '=': dst[j++] = '='; break;
                default: dst[j++] = next; break;
            }
        } else {
            dst[j++] = c;
        }
    }

    dst[j] = '\0';
}

static int parse_key_value(const char* line, char* name, char* value,
                          size_t name_size, size_t value_size) {
    if (!line || !name || !value || name_size == 0 || value_size == 0) {
        return UFS_ERROR_PARAM;
    }

    const char* equal_ptr = strchr(line, '=');
    if (!equal_ptr) {
        return UFS_ERROR_PARSE;
    }

    size_t name_len = equal_ptr - line;
    if (name_len == 0) {
        return UFS_ERROR_PARSE;
    }

    if (name_len >= name_size) {
        return UFS_ERROR_PARAM;
    }

    char escaped_name[name_len + 1];
    strncpy(escaped_name, line, name_len);
    escaped_name[name_len] = '\0';
    unescape_string(escaped_name, name, name_size);

    const char* value_start = equal_ptr + 1;

    if (*value_start == '\0') {
        value[0] = '\0';
        return UFS_SUCCESS;
    }

    size_t value_len = strlen(value_start);
    int has_comma = 0;
    if (value_len > 0 && value_start[value_len - 1] == ',') {
        value_len--;
        has_comma = 1;
    }

    if (value_len == 0) {
        value[0] = '\0';
        return UFS_SUCCESS;
    }

    if (value_len >= value_size) {
        return UFS_ERROR_PARAM;
    }

    char escaped_value[value_len + 1];
    strncpy(escaped_value, value_start, value_len);
    escaped_value[value_len] = '\0';
    unescape_string(escaped_value, value, value_size);

    if (!has_comma && value_len < strlen(value_start)) {
        return UFS_SUCCESS;
    }

    return UFS_SUCCESS;
}

#define DEFAULT_FILENAME "/data/ufs"
ufs_handle_t* ufs_open(const char* filename) {
    if (!filename) {
        filename = DEFAULT_FILENAME;
    }

    ensure_directory(filename);

    ufs_handle_t* handle = malloc(sizeof(ufs_handle_t));
    if (!handle) {
        return NULL;
    }

    handle->filename = NULL;
    handle->file = NULL;
    handle->needs_rebuild = 0;

    handle->filename = strdup(filename);
    if (!handle->filename) {
        free(handle);
        return NULL;
    }

    return handle;
}

int ufs_close(ufs_handle_t* handle) {
    if (!handle) {
        return UFS_ERROR_PARAM;
    }

    if (handle->file) {
        fclose(handle->file);
    }

    free(handle->filename);
    free(handle);

    return UFS_SUCCESS;
}

int ufs_set(ufs_handle_t* handle, const char* name, const char* value) {
    if (!handle || !name || !value) {
        return UFS_ERROR_PARAM;
    }

    if (!validate_name(name)) {
        return UFS_ERROR_PARAM;
    }

    char existing_value[MAX_VALUE_LENGTH];
    int exists = (ufs_get(handle, name, existing_value, sizeof(existing_value)) == UFS_SUCCESS);

    if (exists && strcmp(existing_value, value) == 0) {
        return UFS_SUCCESS;
    }

    if (exists) {
        ufs_delete(handle, name);
    }

    FILE* file = fopen(handle->filename, "a");
    if (!file) {
        file = fopen(handle->filename, "w");
        if (!file) {
            return UFS_ERROR_FILE_OPEN;
        }
    }

    char escaped_name[MAX_NAME_LENGTH * 2];
    char escaped_value[MAX_VALUE_LENGTH * 2];

    escape_string(name, escaped_name, sizeof(escaped_name));
    escape_string(value, escaped_value, sizeof(escaped_value));

    fprintf(file, "%s=%s,\n", escaped_name, escaped_value);

    fclose(file);
    handle->needs_rebuild = 1;

    return UFS_SUCCESS;
}

int ufs_get(ufs_handle_t* handle, const char* name, char* buffer, size_t buffer_size) {
    if (!handle || !name || !buffer || buffer_size == 0) {
        return UFS_ERROR_PARAM;
    }

    FILE* file = fopen(handle->filename, "r");
    if (!file) {
        return UFS_ERROR_NOT_FOUND;
    }

    char line[MAX_LINE_LENGTH];
    char current_name[MAX_NAME_LENGTH];
    char current_value[MAX_VALUE_LENGTH];
    int found = 0;

    while (fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (parse_key_value(line, current_name, current_value,
                           sizeof(current_name), sizeof(current_value)) != UFS_SUCCESS) {
            continue;
        }

        if (strcmp(current_name, name) == 0) {
            found = 1;
            strncpy(buffer, current_value, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            break;
        }
    }

    fclose(file);
    return found ? UFS_SUCCESS : UFS_ERROR_NOT_FOUND;
}

int ufs_get_all(ufs_handle_t* handle, char* buffer, size_t buffer_size) {
    if (!handle || !buffer || buffer_size == 0) {
        return UFS_ERROR_PARAM;
    }

    FILE* file = fopen(handle->filename, "r");
    if (!file) {
        buffer[0] = '\0';
        return UFS_SUCCESS;
    }

    char line[MAX_LINE_LENGTH];
    char current_name[MAX_NAME_LENGTH];
    char current_value[MAX_VALUE_LENGTH];
    size_t offset = 0;

    buffer[0] = '\0';

    while (fgets(line, sizeof(line), file) && offset < buffer_size) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (parse_key_value(line, current_name, current_value,
                           sizeof(current_name), sizeof(current_value)) == UFS_SUCCESS) {
            int written = snprintf(buffer + offset, buffer_size - offset,
                                  "%s=%s\n", current_name, current_value);
            if (written > 0) {
                offset += written;
            }
        }
    }

    fclose(file);
    return UFS_SUCCESS;
}

int ufs_list_names(ufs_handle_t* handle, char* buffer, size_t buffer_size) {
    if (!handle || !buffer || buffer_size == 0) {
        return UFS_ERROR_PARAM;
    }

    FILE* file = fopen(handle->filename, "r");
    if (!file) {
        buffer[0] = '\0';
        return UFS_SUCCESS;
    }

    char line[MAX_LINE_LENGTH];
    char current_name[MAX_NAME_LENGTH];
    char current_value[MAX_VALUE_LENGTH];
    size_t offset = 0;

    buffer[0] = '\0';

    while (fgets(line, sizeof(line), file) && offset < buffer_size) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (parse_key_value(line, current_name, current_value,
                           sizeof(current_name), sizeof(current_value)) == UFS_SUCCESS) {
            int written = snprintf(buffer + offset, buffer_size - offset,
                                  "%s ", current_name);
            if (written > 0) {
                offset += written;
            }
        }
    }

    fclose(file);

    if (offset > 0 && buffer[offset - 1] == ' ') {
        buffer[offset - 1] = '\0';
    }

    return UFS_SUCCESS;
}

int ufs_delete(ufs_handle_t* handle, const char* name) {
    if (!handle || !name) {
        return UFS_ERROR_PARAM;
    }

    FILE* src_file = fopen(handle->filename, "r");
    if (!src_file) {
        return UFS_ERROR_NOT_FOUND;
    }

    char temp_filename[512];
    snprintf(temp_filename, sizeof(temp_filename), "%s%s",
             handle->filename, TEMP_FILE_SUFFIX);

    FILE* dst_file = fopen(temp_filename, "w");
    if (!dst_file) {
        fclose(src_file);
        return UFS_ERROR_FILE_OPEN;
    }

    char line[MAX_LINE_LENGTH];
    char current_name[MAX_NAME_LENGTH];
    char current_value[MAX_VALUE_LENGTH];
    int deleted = 0;

    while (fgets(line, sizeof(line), src_file)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (parse_key_value(line, current_name, current_value,
                           sizeof(current_name), sizeof(current_value)) == UFS_SUCCESS) {
            if (strcmp(current_name, name) != 0) {
                fprintf(dst_file, "%s\n", line);
            } else {
                deleted = 1;
            }
        } else {
            fprintf(dst_file, "%s\n", line);
        }
    }

    fclose(src_file);
    fclose(dst_file);

    if (deleted) {
        remove(handle->filename);
        rename(temp_filename, handle->filename);
    } else {
        remove(temp_filename);
    }

    return deleted ? UFS_SUCCESS : UFS_ERROR_NOT_FOUND;
}

int ufs_clear(ufs_handle_t* handle) {
    if (!handle) {
        return UFS_ERROR_PARAM;
    }

    FILE* file = fopen(handle->filename, "w");
    if (!file) {
        return UFS_ERROR_FILE_OPEN;
    }

    fclose(file);
    return UFS_SUCCESS;
}

int ufs_stats(ufs_handle_t* handle, size_t* count, size_t* file_size) {
    if (!handle) {
        return UFS_ERROR_PARAM;
    }

    FILE* file = fopen(handle->filename, "r");
    if (!file) {
        if (count) *count = 0;
        if (file_size) *file_size = 0;
        return UFS_SUCCESS;
    }

    size_t item_count = 0;
    char line[MAX_LINE_LENGTH];

    while (fgets(line, sizeof(line), file)) {
        char current_name[MAX_NAME_LENGTH];
        char current_value[MAX_VALUE_LENGTH];

        if (parse_key_value(line, current_name, current_value,
                           sizeof(current_name), sizeof(current_value)) == UFS_SUCCESS) {
            item_count++;
        }
    }

    fclose(file);

    struct stat st;
    size_t size = 0;
    if (stat(handle->filename, &st) == 0) {
        size = st.st_size;
    }

    if (count) *count = item_count;
    if (file_size) *file_size = size;

    return UFS_SUCCESS;
}

int check_ufs_info(void) {
//	printf("start check_ufs_info\n");
	KeyValueStore out;
      _trusty_ioctl(SYS_PLATFORM_FD,
		SYS_PLATFORM_GET_UFS_INFO, (void*)&out);

	if (out.total_pairs_allocated)
		return 0;
	ufs_handle_t* handle = ufs_open(DEFAULT_FILENAME);
	if (!handle) {
		printf("Failed to open UFS file: %s\n", DEFAULT_FILENAME);
		return 1;
	}
//	printf("Thread_print: ioctl out (%s)\n", out.pairs[0].name);

    int i;
    int count = out.count;
//	printf("count %d\n", count);

    for(i = 0; i < count; i++) {
        const char* name = out.pairs[i].name;
        char* value = out.pairs[i].value;
		int ret = ufs_set(handle, name, value);
		if (ret == UFS_SUCCESS) {
//			printf("Set '%s' = '%s'\n", name, value);
		} else {
			printf("Failed to set '%s' ret %d\n", name, ret);
			return 1;
		}
    }

    return 0;
}