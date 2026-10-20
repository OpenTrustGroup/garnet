// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#pragma pack(push, 1)

typedef struct {
    uint64_t signature;           // "EFI PART"
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entries_lba;
    uint32_t num_partition_entries;
    uint32_t partition_entry_size;
    uint32_t partition_array_crc32;
    uint8_t reserved2[420];
} GPTHeader;

typedef struct {
    uint8_t partition_type_guid[16];
    uint8_t unique_partition_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t partition_name[36];
} GPTPartitionEntry;

#pragma pack(pop)

static void guid_to_string(const uint8_t *guid, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             guid[3], guid[2], guid[1], guid[0],
             guid[5], guid[4],
             guid[7], guid[6],
             guid[8], guid[9],
             guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
}

static void utf16_to_ascii(const uint16_t *utf16, char *ascii, size_t max_len) {
    size_t i;
    for (i = 0; i < max_len - 1 && utf16[i] != 0; i++) {
        ascii[i] = (utf16[i] < 128) ? (char)utf16[i] : '?';
    }
    ascii[i] = '\0';
}

static const char *get_partition_type_name(const uint8_t *type_guid) {
    // Microsoft Basic Data Partition
    static const uint8_t basic_data_guid[16] = {
        0xEB, 0xD0, 0xA0, 0xA2, 0xB9, 0xE5, 0x44, 0x33,
        0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
    };

    // EFI System Partition
    static const uint8_t efi_system_guid[16] = {
        0xC1, 0x2A, 0x73, 0x28, 0xF8, 0x1F, 0x11, 0xD2,
        0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
    };

    // Microsoft Reserved Partition
    static const uint8_t ms_reserved_guid[16] = {
        0xE3, 0xC9, 0xE3, 0x16, 0x0B, 0x5C, 0x4D, 0xB8,
        0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE
    };

    if (memcmp(type_guid, basic_data_guid, 16) == 0)
        return "Microsoft Basic Data";
    else if (memcmp(type_guid, efi_system_guid, 16) == 0)
        return "EFI System Partition";
    else if (memcmp(type_guid, ms_reserved_guid, 16) == 0)
        return "Microsoft Reserved";
    else
        return "Unknown";
}

static void print_partition_attributes(uint64_t attributes) {
    printf("Attributes: 0x%016llx\n", (unsigned long long)attributes);

    if (attributes & (1ULL << 0))
        printf("  - Required Partition\n");
    if (attributes & (1ULL << 1))
        printf("  - No Block IO Protocol\n");
    if (attributes & (1ULL << 2))
        printf("  - Legacy BIOS Bootable\n");
    if (attributes & (1ULL << 60))
        printf("  - Read-Only\n");
    if (attributes & (1ULL << 61))
        printf("  - Shadow Copy\n");
    if (attributes & (1ULL << 62))
        printf("  - Hidden\n");
    if (attributes & (1ULL << 63))
        printf("  - No Auto-mount\n");
}

void dump_gpt_header(const GPTHeader *header) {
    printf("=== GPT Header Dump ===\n");

    if (header->signature != 0x5452415020494645ULL) {
        printf("ERROR: Invalid GPT signature, sig: 0x%lx\n",
			header->signature);
        return;
    }

    printf("Signature: EFI PART (valid)\n");
    printf("Revision: %u.%u\n",
           (header->revision >> 16) & 0xFFFF, header->revision & 0xFFFF);
    printf("Header Size: %u bytes\n", header->header_size);
    printf("Header CRC32: 0x%08x\n", header->header_crc32);
    printf("Current LBA: %llu\n", (unsigned long long)header->current_lba);
    printf("Backup LBA: %llu\n", (unsigned long long)header->backup_lba);
    printf("First Usable LBA: %llu\n", (unsigned long long)header->first_usable_lba);
    printf("Last Usable LBA: %llu\n", (unsigned long long)header->last_usable_lba);

    char disk_guid_str[37];
    guid_to_string(header->disk_guid, disk_guid_str, sizeof(disk_guid_str));
    printf("Disk GUID: %s\n", disk_guid_str);

    printf("Partition Entries LBA: %llu\n",
           (unsigned long long)header->partition_entries_lba);
    printf("Number of Partition Entries: %u\n", header->num_partition_entries);
    printf("Partition Entry Size: %u bytes\n", header->partition_entry_size);
    printf("Partition Array CRC32: 0x%08x\n", header->partition_array_crc32);

    uint64_t partition_array_size = (uint64_t)header->num_partition_entries *
                                   header->partition_entry_size;
    printf("Partition Array Size: %llu bytes\n",
           (unsigned long long)partition_array_size);

    uint64_t total_usable_blocks = header->last_usable_lba - header->first_usable_lba + 1;
    printf("Total Usable Blocks: %llu\n",
           (unsigned long long)total_usable_blocks);

    printf("\n");
}

void dump_partition_table(const GPTPartitionEntry *entries,
                         uint32_t num_entries,
                         uint32_t entry_size) {
    printf("=== Partition Table Dump ===\n");
    printf("Found %u partition entries\n\n", num_entries);

    int valid_partitions = 0;

    for (uint32_t i = 0; i < num_entries; i++) {
        const GPTPartitionEntry *entry =
            (const GPTPartitionEntry*)((const uint8_t*)entries + i * entry_size);

        int is_empty = 1;
        for (int j = 0; j < 16; j++) {
            if (entry->partition_type_guid[j] != 0) {
                is_empty = 0;
                break;
            }
        }

        if (is_empty) {
            continue;
        }

        valid_partitions++;
        printf("Partition %d:\n", valid_partitions);
        printf("  Entry Index: %u\n", i);

        char type_guid_str[37];
        guid_to_string(entry->partition_type_guid, type_guid_str, sizeof(type_guid_str));
        printf("  Type GUID: %s\n", type_guid_str);
        printf("  Type Name: %s\n", get_partition_type_name(entry->partition_type_guid));

        char unique_guid_str[37];
        guid_to_string(entry->unique_partition_guid, unique_guid_str, sizeof(unique_guid_str));
        printf("  Unique GUID: %s\n", unique_guid_str);

        printf("  Start LBA: %llu\n", (unsigned long long)entry->starting_lba);
        printf("  End LBA: %llu\n", (unsigned long long)entry->ending_lba);
        printf("  Size: %llu blocks\n",
               (unsigned long long)(entry->ending_lba - entry->starting_lba + 1));

        print_partition_attributes(entry->attributes);

        char partition_name[37];
        utf16_to_ascii(entry->partition_name, partition_name, sizeof(partition_name));
        printf("  Name: %s\n", partition_name);

        printf("\n");
    }

    if (valid_partitions == 0) {
        printf("No valid partitions found.\n");
    }
}

