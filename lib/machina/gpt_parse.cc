// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// GPT data structure definitions
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>

#define GPT_SIGNATURE "EFI PART"
#define GPT_SIGNATURE_SIZE 8

// GPT header structure
typedef struct __attribute__((packed)) {
    char signature[8];              // "EFI PART"
    uint32_t revision;              // Revision
    uint32_t header_size;           // Header size (usually 92 bytes)
    uint32_t header_crc32;          // Header CRC32
    uint32_t reserved;              // Reserved
    uint64_t current_lba;           // Current header LBA
    uint64_t backup_lba;            // Backup header LBA
    uint64_t first_usable_lba;      // First usable LBA
    uint64_t last_usable_lba;       // Last usable LBA
    uint8_t disk_guid[16];          // Disk GUID
    uint64_t partition_entries_lba; // Partition entries start LBA
    uint32_t num_partition_entries; // Number of partition entries
    uint32_t partition_entry_size;  // Partition entry size
    uint32_t partition_array_crc32; // Partition array CRC32
} gpt_header_t;

// GPT partition entry structure
typedef struct __attribute__((packed)) {
    uint8_t partition_type_guid[16];  // Partition type GUID
    uint8_t unique_partition_guid[16]; // Unique partition GUID
    uint64_t starting_lba;            // Starting LBA
    uint64_t ending_lba;              // Ending LBA
    uint64_t attributes;              // Attribute flags
    uint16_t partition_name[36];      // Partition name (UTF-16LE)
} gpt_partition_entry_t;

// Common partition type GUIDs
static const uint8_t EMPTY_GUID[16] = {0};
static const uint8_t EFI_SYSTEM_PARTITION_GUID[16] =
    {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
     0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
static const uint8_t MICROSOFT_BASIC_DATA_GUID[16] =
    {0xEB, 0xD0, 0xA0, 0xA2, 0xB9, 0xE5, 0x44, 0x33,
     0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
static const uint8_t LINUX_FILESYSTEM_GUID[16] =
    {0x0F, 0xC6, 0x3D, 0xAF, 0x84, 0x83, 0x47, 0x46,
     0x8E, 0x93, 0xD2, 0x52, 0x79, 0x2B, 0x5F, 0xBF};

// Utility functions
void print_guid(const uint8_t* guid) {
    printf("%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
           guid[3], guid[2], guid[1], guid[0],
           guid[5], guid[4],
           guid[7], guid[6],
           guid[8], guid[9],
           guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
}

const char* get_partition_type_name(const uint8_t* type_guid) {
    if (memcmp(type_guid, EMPTY_GUID, 16) == 0) {
        return "Unused";
    } else if (memcmp(type_guid, EFI_SYSTEM_PARTITION_GUID, 16) == 0) {
        return "EFI System Partition";
    } else if (memcmp(type_guid, MICROSOFT_BASIC_DATA_GUID, 16) == 0) {
        return "Microsoft Basic Data";
    } else if (memcmp(type_guid, LINUX_FILESYSTEM_GUID, 16) == 0) {
        return "Linux Filesystem";
    } else {
        return "Unknown";
    }
}

void print_utf16le(const uint16_t* str, size_t max_chars) {
    for (size_t i = 0; i < max_chars; i++) {
        uint16_t ch = le16toh(str[i]);
        if (ch == 0) break;
        if (ch >= 32 && ch <= 126) { // Printable ASCII
            printf("%c", (char)ch);
        } else {
            printf(".");
        }
    }
}

// Parse GPT partition table
int parse_gpt_partitions(const uint8_t* memory, size_t len, size_t blk_sz) {
    printf("Starting GPT partition table analysis...\n");
    printf("Memory address: %p, Size: %zu bytes, Block size: %zu bytes\n\n",
           memory, len, blk_sz);

    // Validate block size
    if (blk_sz == 0 || blk_sz % 512 != 0) {
        printf("Error: Block size must be multiple of 512\n");
        return -1;
    }

    // Check minimum length
    if (len < 2 * blk_sz) {
        printf("Error: Memory region too small for GPT analysis\n");
        return -1;
    }

    // Check protective MBR (at LBA 0)
    const uint8_t* mbr = memory;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        printf("Warning: No valid MBR signature found\n");
    } else {
        printf("Found protective MBR\n");
    }

    // GPT header is at LBA 1 (one block after start)
    const gpt_header_t* gpt_header = (const gpt_header_t*)(memory + blk_sz);

    // Check GPT signature
    if (memcmp(gpt_header->signature, GPT_SIGNATURE, GPT_SIGNATURE_SIZE) != 0) {
        printf("Error: Invalid GPT signature\n");
        return -1;
    }

    printf("Found valid GPT header:\n");
    printf("  Signature: %.8s\n", gpt_header->signature);
    printf("  Revision: %u.%u\n", gpt_header->revision >> 16, gpt_header->revision & 0xFFFF);
    printf("  Header size: %u bytes\n", gpt_header->header_size);
    printf("  Current LBA: %lu\n", (unsigned long)le64toh(gpt_header->current_lba));
    printf("  Backup LBA: %lu\n", (unsigned long)le64toh(gpt_header->backup_lba));
    printf("  First usable LBA: %lu\n", (unsigned long)le64toh(gpt_header->first_usable_lba));
    printf("  Last usable LBA: %lu\n", (unsigned long)le64toh(gpt_header->last_usable_lba));

    printf("  Disk GUID: ");
    print_guid(gpt_header->disk_guid);
    printf("\n");

    printf("  Partition entries LBA: %lu\n", (unsigned long)le64toh(gpt_header->partition_entries_lba));
    printf("  Number of partition entries: %u\n", le32toh(gpt_header->num_partition_entries));
    printf("  Partition entry size: %u bytes\n", le32toh(gpt_header->partition_entry_size));

    // Validate partition entries array location
    uint64_t partition_array_lba = le64toh(gpt_header->partition_entries_lba);
    size_t partition_array_offset = partition_array_lba * blk_sz;
    uint32_t partition_entry_size = le32toh(gpt_header->partition_entry_size);
    uint32_t num_partitions = le32toh(gpt_header->num_partition_entries);

    size_t partition_array_size = num_partitions * partition_entry_size;

    if (partition_array_offset + partition_array_size > len) {
        printf("Error: Partition table extends beyond given memory region\n");
        printf("Required: %zu bytes, Available: %zu bytes\n",
               partition_array_offset + partition_array_size, len);
        return -1;
    }

    // Parse partition entries
    printf("\nPartition list:\n");
    printf("==============================================================================\n");
    printf("#   Type GUID                           Start LBA  End LBA    Size(MB)  Name\n");
    printf("------------------------------------------------------------------------------\n");

    const uint8_t* partition_array = memory + partition_array_offset;
    int valid_partitions = 0;

    for (uint32_t i = 0; i < num_partitions; i++) {
        const gpt_partition_entry_t* entry =
            (const gpt_partition_entry_t*)(partition_array + i * partition_entry_size);

        // Skip empty partitions
        if (memcmp(entry->partition_type_guid, EMPTY_GUID, 16) == 0) {
            continue;
        }

        uint64_t start_lba = le64toh(entry->starting_lba);
        uint64_t end_lba = le64toh(entry->ending_lba);

        // Validate partition bounds
        if (end_lba < start_lba) {
            printf("WARNING: Partition %u has invalid LBA range (%lu-%lu)\n",
                   i + 1, (unsigned long)start_lba, (unsigned long)end_lba);
            continue;
        }

        uint64_t size_sectors = (end_lba - start_lba + 1);
        double size_mb = (size_sectors * blk_sz) / (1024.0 * 1024.0);

        printf("%-3u ", i + 1);
        print_guid(entry->partition_type_guid);
        printf(" %-9lu %-9lu %-9.1f ",
               (unsigned long)start_lba,
               (unsigned long)end_lba,
               size_mb);
        print_utf16le(entry->partition_name, 36);
        printf("\n");

        printf("     Type: %s\n", get_partition_type_name(entry->partition_type_guid));
        printf("     Unique GUID: ");
        print_guid(entry->unique_partition_guid);
        printf("\n");

        if (entry->attributes != 0) {
            printf("     Attributes: 0x%016lX\n", (unsigned long)le64toh(entry->attributes));
        }

        // Calculate exact size in bytes
        uint64_t size_bytes = size_sectors * blk_sz;
        printf("     Size: %lu sectors (%lu bytes)\n",
               (unsigned long)size_sectors, (unsigned long)size_bytes);

        printf("------------------------------------------------------------------------------\n");
        valid_partitions++;
    }

    printf("\nFound %d valid partitions out of %u entries\n", valid_partitions, num_partitions);

    // Summary information
    if (valid_partitions > 0) {
        printf("\nSummary:\n");
        printf("Total partitions: %d\n", valid_partitions);
        printf("Block size: %zu bytes\n", blk_sz);
        printf("GPT header at LBA: %lu\n", (unsigned long)le64toh(gpt_header->current_lba));
        printf("Partition entries at LBA: %lu\n", (unsigned long)partition_array_lba);
    }

    return valid_partitions;
}

