#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "blk_ufsinfo/ufsinfo.h"

#define DEFAULT_FILENAME "/data/ufs"
#define MAX_BUFFER_SIZE 4096
#define MAX_CMD_ARGS 10

typedef enum {
    CMD_UNKNOWN,
    CMD_SET,
    CMD_GET,
    CMD_DEL,
    CMD_ALL,
    CMD_CLEAR,
    CMD_STATS,
    CMD_HELP
} command_type_t;

typedef struct {
    const char* cmd;
    const char* alias;
    const char* usage;
    const char* description;
} command_help_t;

static const command_help_t cmd_help[] = {
    {"set",    "s",    "set <name> <value>",    "Set a key-value pair"},
    {"get",    "g",    "get <name>",            "Get value by name"},
    {"delete", "d",    "del <name>",            "Delete a key-value pair"},
    {"all",    "a",    "all",                   "List all key names"},
    {"list",   "l",    "list",                  "List all key-value pairs"},
    {"clear",   "c",   "clear",                 "Clear all data"},
    {"stats",  "stat", "stats",                 "Show statistics"},
    {"help",   "h",    "help",                  "Show this help"},
    {"version","v",    "version",               "Show version"},
    {NULL, NULL, NULL, NULL}
};

static command_type_t parse_command(const char* cmd) {
    if (!cmd) return CMD_UNKNOWN;

    if (strcmp(cmd, "set") == 0 || strcmp(cmd, "s") == 0) return CMD_SET;
    if (strcmp(cmd, "get") == 0 || strcmp(cmd, "g") == 0) return CMD_GET;
    if (strcmp(cmd, "delete") == 0 || strcmp(cmd, "del") == 0 || strcmp(cmd, "d") == 0) return CMD_DEL;
    if (strcmp(cmd, "all") == 0 || strcmp(cmd, "a") == 0) return CMD_ALL;
    if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "c") == 0) return CMD_CLEAR;
    if (strcmp(cmd, "stats") == 0 || strcmp(cmd, "stat") == 0) return CMD_STATS;
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) return CMD_HELP;
    return CMD_UNKNOWN;
}

static void print_detailed_help() {
    printf("Usage: ufsinfo <command> [arguments]\n");
    printf("       ufsinfo [options]\n\n");

    printf("Commands:\n");
    for (int i = 0; cmd_help[i].cmd; i++) {
        printf("  %-10s %-20s %s\n",
               cmd_help[i].cmd,
               cmd_help[i].usage,
               cmd_help[i].description);
    }

    printf("\n");
    printf("Aliases:\n");
    for (int i = 0; cmd_help[i].cmd; i++) {
        if (cmd_help[i].alias) {
            printf("  %s → %s\n", cmd_help[i].alias, cmd_help[i].cmd);
        }
    }

    printf("\n");
    printf("Examples:\n");
    printf("  ufsinfo set username alice           # Set key 'username' to 'alice'\n");
    printf("  ufsinfo s username alice             # Same as above (short form)\n");
    printf("  ufsinfo g username                   # Get value of 'username'\n");
    printf("  ufsinfo get all                      # Get all key-value pairs\n");
    printf("  ufsinfo a                            # List all keys (short form)\n");
    printf("  ufsinfo d username                   # Delete 'username'\n");
    printf("  ufsinfo stats                        # Show statistics\n");

    printf("\n");
    printf("File: %s\n", DEFAULT_FILENAME);
    printf("Format: key1=value1,key2=value2,...\n");
}

static void print_quick_help() {
    printf("Usage: ufsinfo <command> [arguments]\n");
    printf("Commands: set(s), get(g), delete(d/del), list(l), all(a), clear(c), stats, help(h), version(v)\n");
    printf("Try 'ufsinfo help' for more information.\n");
}

static char* combine_arguments(int argc, char** argv, int start_idx) {
    static char buffer[MAX_BUFFER_SIZE];
    int total_len = 0;

    buffer[0] = '\0';

    for (int i = start_idx; i < argc && total_len < MAX_BUFFER_SIZE - 1; i++) {
        int len = strlen(argv[i]);
        if (total_len + len + 1 < MAX_BUFFER_SIZE) {
            if (total_len > 0) {
                buffer[total_len++] = ' ';
            }
            strcpy(buffer + total_len, argv[i]);
            total_len += len;
        }
    }

    buffer[total_len] = '\0';
    return buffer;
}

static int execute_command(command_type_t cmd_type, int argc, char** argv) {
    ufs_handle_t* handle = ufs_open(DEFAULT_FILENAME);
    if (!handle) {
        printf("Failed to open UFS file: %s\n", DEFAULT_FILENAME);
        return 1;
    }

    int result = 0;

    switch (cmd_type) {
        case CMD_SET: {
            if (argc < 4) {
                printf("Usage: ufs set <name> <value>\n");
                result = 1;
            } else {
                const char* name = argv[2];
                char* value = combine_arguments(argc, argv, 3);

                int ret = ufs_set(handle, name, value);
                if (ret == UFS_SUCCESS) {
                    printf("Set '%s' = '%s'\n", name, value);
                } else {
                    printf("Failed to set '%s'\n", name);
                    result = 1;
                }
            }
            break;
        }

        case CMD_GET: {
            if (argc < 3) {
                printf("Usage: ufs get <name>\n");
                printf("       ufs get all\n");
                result = 1;
            } else {
                const char* name = argv[2];

                if (strcmp(name, "all") == 0) {
                    char buffer[MAX_BUFFER_SIZE];
                    int ret = ufs_get_all(handle, buffer, sizeof(buffer));
                    if (ret == UFS_SUCCESS) {
                        if (buffer[0] != '\0') {
                            printf("All stored data:\n");
                            printf("%s", buffer);
                        } else {
                            printf("No data stored\n");
                        }
                    } else {
                        printf("Failed to get all values\n");
                        result = 1;
                    }
                } else {
                    char buffer[MAX_BUFFER_SIZE];
                    int ret = ufs_get(handle, name, buffer, sizeof(buffer));
                    if (ret == UFS_SUCCESS) {
                        printf("%s\n", buffer);
                    } else if (ret == UFS_ERROR_NOT_FOUND) {
                        printf("'%s' not found\n", name);
                    } else {
                        printf("Failed to get '%s'\n", name);
                        result = 1;
                    }
                }
            }
            break;
        }

        case CMD_DEL: {
            if (argc < 3) {
                printf("Usage: ufs delete <name>\n");
                result = 1;
            } else {
                const char* name = argv[2];
                int ret = ufs_delete(handle, name);
                if (ret == UFS_SUCCESS) {
                    printf("Deleted '%s'\n", name);
                } else if (ret == UFS_ERROR_NOT_FOUND) {
                    printf("'%s' not found\n", name);
                } else {
                    printf("Failed to delete '%s'\n", name);
                    result = 1;
                }
            }
            break;
        }

        case CMD_ALL: {
            char buffer[MAX_BUFFER_SIZE];
            int ret = ufs_list_names(handle, buffer, sizeof(buffer));
            if (ret == UFS_SUCCESS) {
                if (buffer[0] != '\0') {
                    printf("All keys:\n");
                    printf("%s\n", buffer);

                    size_t count = 0;
                    char* token = strtok(buffer, " ");
                    while (token) {
                        count++;
                        token = strtok(NULL, " ");
                    }
                    printf("Total: %zu key(s)\n", count);
                } else {
                    printf("No keys stored\n");
                }
            } else {
                printf("Failed to list keys\n");
                result = 1;
            }
            break;
        }

        case CMD_CLEAR: {
            int ret = ufs_clear(handle);
            if (ret == UFS_SUCCESS) {
                printf("Cleared all data\n");
            } else {
                printf("Failed to clear data\n");
                result = 1;
            }
            break;
        }

        case CMD_STATS: {
            size_t count = 0, file_size = 0;
            int ret = ufs_stats(handle, &count, &file_size);
            if (ret == UFS_SUCCESS) {
                printf("Statistics:\n");
                printf("  Items:     %zu\n", count);
                printf("  File size: %zu bytes\n", file_size);
                printf("  File:      %s\n", DEFAULT_FILENAME);

                if (file_size > 0) {
                    printf("  Avg size:  %.1f bytes/item\n",
                           count > 0 ? (float)file_size / count : 0.0);
                }
            } else {
                printf("Failed to get statistics\n");
                result = 1;
            }
            break;
        }

        case CMD_HELP:
            print_detailed_help();
            break;

        default:
            print_quick_help();
            result = 1;
            break;
    }

    ufs_close(handle);
    return result;
}

extern int check_ufs_info(void);

int main(int argc, char** argv) {
	check_ufs_info();
    if (argc == 1) {
        print_detailed_help();
        return 0;
    }

    command_type_t cmd_type = parse_command(argv[1]);

    int result = execute_command(cmd_type, argc, argv);

    return result;
}