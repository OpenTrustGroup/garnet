#ifndef _UFSINFO_H_
#define _UFSINFO_H_

#include <stdbool.h>
#include <stddef.h>
#include "set_info.h"

#define UFS_SUCCESS 0
#define UFS_ERROR_FILE_OPEN -1
#define UFS_ERROR_FILE_READ -2
#define UFS_ERROR_FILE_WRITE -3
#define UFS_ERROR_MEMORY -4
#define UFS_ERROR_PARAM -5
#define UFS_ERROR_NOT_FOUND -6
#define UFS_ERROR_PARSE -7
struct ufs_handle {
    char* filename;
    FILE* file;
    int needs_rebuild;
};
typedef struct ufs_handle ufs_handle_t;
int check_ufs_info(void);
ufs_handle_t* ufs_open(const char* filename);
int ufs_close(ufs_handle_t* handle);

int ufs_set(ufs_handle_t* handle, const char* name, const char* value);

int ufs_get(ufs_handle_t* handle, const char* name, char* buffer, size_t buffer_size);

int ufs_get_all(ufs_handle_t* handle, char* buffer, size_t buffer_size);

int ufs_list_names(ufs_handle_t* handle, char* buffer, size_t buffer_size);

int ufs_delete(ufs_handle_t* handle, const char* name);

int ufs_clear(ufs_handle_t* handle);

int ufs_stats(ufs_handle_t* handle, size_t* count, size_t* file_size);

#endif  // _UFSINFO_H_