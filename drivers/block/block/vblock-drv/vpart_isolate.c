// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vpart_isolate.h"

#include <assert.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <threads.h>
#include <zircon/device/block.h>
#include <zircon/hw/gpt.h>
#include <zircon/process.h>

#include "vblock-drv.h"

/* GPT Macros */
#define GPT_PRIMARY_MBR_LBA 0      // MBR LBA 0
#define GPT_PRIMARY_HEADER_LBA 1   // GPT Header LBA 1
#define GPT_PARTITION_TABLE_LBA 2  // Partiton table LBA 2
#define GPT_FIXED_BLOCKS 3         // MBR + GPT Header + Backup GPT Header

#define GPT_REVISION 0x00010000ULL  // GPT 1.0
#define GPT_SIZE 92
#define GPT_SIGNATURE 0x5452415020494645ULL  // "EFI PART"

#define VPI_MAX_VIRTUAL_DISKS 3
#define VPI_MAX_PARTITIONS 128
#define VPI_MIN_BLOCK_SIZE 512
#define VPI_NAME "vm"
#define GPT_NAME_LEN 72

#ifndef GPT_GUID_LEN
#define GPT_GUID_LEN 16
#endif

#define PARTITION_SHARE_ANDROID_TBOX (3ULL << 56)
#define PARTITION_SHARE_YOCTO_ANDROID (5ULL << 56)
#define PARTITION_SHARE_YOCTO_TBOX (6ULL << 56)
#define PARTITION_SHARE_ALL (7ULL << 56)

#define PARTITION_RW_PERM_OS(x) \
  ((x) == 0 ? (1ULL << 50)      \
            : ((x) == 1 ? (1ULL << 48) : ((x) == 2 ? (1ULL << 49) : 0ULL)))
#define PARTITION_SHARE_OS(x) \
  ((x) == 0 ? (1ULL << 58)    \
            : ((x) == 1 ? (1ULL << 56) : ((x) == 2 ? (1ULL << 57) : 0ULL)))
#define VPI_BIT(x) ((1UL) << (x))

enum {
  VPI_MAX_OS_YOCTO = GRT_BLOCK_SOS_BACKEND_VMID,
  VPI_MAX_OS_ANDROID = GRT_BLOCK_ALPS_BACKEND_VMID,
  VPI_MAX_OS_TBOX = GRT_BLOCK_TBOX_BACKEND_VMID,
  VPI_MAX_OS_NUM = GRT_BLOCK_MAX_BACKEND_VMIDS,
};

enum {
  SHARE_ANDROID_TBOX = 3,
  SHARE_YOCTO_ANDROID = 5,
  SHARE_YOCTO_TBOX = 6,
  SHARE_ALL = 7,
};

// GPT
#pragma pack(push, 1)

typedef struct {
  uint8_t boot_flag;
  uint8_t start_chs[3];
  uint8_t type;
  uint8_t end_chs[3];
  uint32_t start_lba;
  uint32_t sector_count;
} mbr_partition_entry_t;

// MBR
typedef struct {
  uint8_t boot_code[440];
  uint32_t disk_signature;
  uint16_t reserved;
  mbr_partition_entry_t partition[4];
  uint16_t signature;  // 0x55AA
} mbr_t;

typedef struct {
  uint64_t magic;                  // "EFI PART"
  uint32_t revision;               // revision
  uint32_t header_size;            // header size (92)
  uint32_t header_crc32;           // CRC32 Checksum of the header
  uint32_t reserved;               // Must be 0
  uint64_t current_lba;            // current position of the head
  uint64_t backup_lba;             // Backup position of the head
  uint64_t first_usable_lba;       // First Usable LBA
  uint64_t last_usable_lba;        // Last Usable LBA
  uint8_t guid[16];                // GUID
  uint64_t partition_entries_lba;  // Starting LBA of the Partition Table
  uint32_t num_partition_entries;  // Number of Partition Entries
  uint32_t
      size_of_partition_entry;  // Size per Partition Entry (usually 128 bytes)
  uint32_t partition_entries_crc32;  // CRC32 Checksum of the Partition Table
  uint8_t reserved2[420];            // Reserved
} gpt_hdr_t;

typedef struct {
  uint8_t partition_type_guid[16];       // Partition Type GUID
  uint8_t unique_partition_guid[16];     // Unique Partition GUID
  uint64_t starting_lba;                 // Starting LBA
  uint64_t ending_lba;                   // Ending LBA
  uint64_t attributes;                   // Attribute Flags
  uint8_t partition_name[GPT_NAME_LEN];  // Partition Name (UTF-16LE)
} gpt_partition_entry_t;

#pragma pack(pop)

typedef struct {
  uint64_t vstart_lba;  // Virtual Starting LBA
  uint64_t pstart_lba;  // Physical Starting LBA
  uint64_t nblocks;     // Block Count
  uint32_t part_idx;    // Partition Index
  uint32_t share_type;  // share type 0 Non-shared, 7 3os Share, 6 yocto and
                        // tbox, 5 yocto and android, 3 android and tbox
  bool rw_perm;         // 1 rw, 0 Read-only
  uint8_t name[GPT_NAME_LEN];
} partition_mapping_t;

typedef struct {
  void* entries_buffer;
  uint32_t entry_size;
  uint32_t num_entries;
  uint32_t num_entries_os[VPI_MAX_OS_NUM];
  uint32_t num_os;
  bool has_partition_isolation;
} physical_partition_info_t;

typedef struct {
  block_protocol_t* block_proto;
  block_info_t* block_info;
  size_t block_op_size;
} vpart_backend_t;

struct vpart_iso_drv {
  partition_mapping_t* cache_map;
  /*Mapping Table*/
  partition_mapping_t part_maps[VPI_MAX_PARTITIONS];
  uint32_t part_count;  // partition entries num
  uint64_t
      part_total_blocks;  // The number of blocks occupied by the data partition
  uint64_t partition_table_blocks;  // The number of blocks occupied by the
                                    // partition table
  uint64_t vstart_lba;
  uint64_t vend_lba;

  /*Device Information*/
  block_info_t virt_info;

  /* vritual GPT */
  size_t capa;  // (MBR + GPT Header + GPT partition table + Backup GPT
                // partition table + Backup GPT Header) * block_size
  uint64_t
      total_blocks;  // MBR + GPT Header + GPT partition table + Data partition
                     // + Backup GPT partition table + Backup GPT Header
  zx_handle_t rd_vmo;
  void* rd_vaddr;

  int vmid;

  bool has_partition_isolation;
};

#define VPI_OP_IS_RW(op) \
  ((op)->command == BLOCK_OP_READ || (op)->command == BLOCK_OP_WRITE)

#define VPI_OP_IS_ERASE(op)                    \
  (((op)->command == BLOCK_OP_DISCARD) ||      \
   ((op)->command == BLOCK_OP_SECURE_ERASE) || \
   ((op)->command == BLOCK_OP_WRITE_ZEROES))

static const uint8_t partition_yocto_guid[16] = {
    0x7A, 0x93, 0xCF, 0x3B, 0xF4, 0xCA, 0x7D, 0x42,
    0xA4, 0x3D, 0xB7, 0xF0, 0x75, 0xEE, 0xAF, 0x15};

static const uint8_t partition_android_guid[16] = {
    0x85, 0x12, 0x6F, 0x25, 0x7D, 0xE2, 0xCA, 0x48,
    0xAA, 0x23, 0xCE, 0xF9, 0xE5, 0x85, 0x0A, 0xB9};

static const uint8_t partition_tbox_guid[16] = {
    0x33, 0x86, 0xCC, 0x84, 0xA8, 0x95, 0xF0, 0x43,
    0xA1, 0x13, 0x31, 0x5F, 0x47, 0x65, 0x1C, 0x94};

static uint32_t crc32_table[256];
static once_flag crc32_once = ONCE_FLAG_INIT;

static void crc32_init(void) {
  uint32_t polynomial = 0xEDB88320;

  for (uint32_t i = 0; i < 256; i++) {
    uint32_t crc = i;

    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ polynomial;
      } else {
        crc >>= 1;
      }
    }

    crc32_table[i] = crc;
  }
}

static uint32_t crc32(uint32_t crc, const uint8_t* buf, size_t len) {
  call_once(&crc32_once, crc32_init);

  crc = ~crc;

  for (size_t i = 0; i < len; i++) {
    uint8_t byte = buf[i];
    uint8_t table_index = (uint8_t)(crc ^ byte);
    crc = (crc >> 8) ^ crc32_table[table_index];
  }

  return ~crc;
}

static void generate_random_guid(uint8_t* guid) {
  size_t sz;
  if (zx_cprng_draw(guid, GPT_GUID_LEN, &sz) == ZX_OK && sz == GPT_GUID_LEN) {
    return;
  }

  for (int i = 0; i < 16; i++) {
    guid[i] = (uint8_t)(rand() & 0xFF);
  }

  guid[7] = (guid[7] & 0x0F) | 0x40;
  guid[8] = (guid[8] & 0x3F) | 0x80;
}

static void sync_read_completion(block_op_t* bop, zx_status_t status) {
  sync_read_ctx_t* ctx = (sync_read_ctx_t*)bop->cookie;
  ctx->status = status;
  completion_signal(&ctx->completion);
}

static zx_status_t sync_read_blocks(vpart_backend_t* backend,
                                    uint64_t block_addr,
                                    void* buffer,
                                    uint32_t block_count) {
  zx_status_t status;

  size_t block_op_size = backend->block_op_size;
  block_op_t* bop = calloc(1, block_op_size);
  if (!bop) {
    zxlogf(ERROR, "[VPI]: NULL pointer in input parameters\n");
    return ZX_ERR_NO_MEMORY;
  }

  size_t buffer_size = block_count * backend->block_info->block_size;
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  status = zx_vmo_create(buffer_size, 0, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[VPI]: Failed to create vmo\n");
    free(bop);
    return status;
  }

  status = zx_vmo_set_size(vmo, buffer_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[VPI]: Failed to set vmo size\n");
    zx_handle_close(vmo);
    free(bop);
    return status;
  }

  uintptr_t vaddr;
  status = zx_vmar_map(zx_vmar_root_self(),
                       ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_READ, 0, vmo, 0,
                       buffer_size, &vaddr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[VPI]: Failed to map vmo\n");
    zx_handle_close(vmo);
    free(bop);
    return status;
  }

  sync_read_ctx_t ctx;

  bop->command = BLOCK_OP_READ;
  bop->rw.vmo = vmo;
  bop->rw.length = block_count;
  bop->rw.offset_dev = block_addr;
  bop->rw.offset_vmo = 0;
  bop->completion_cb = sync_read_completion;
  bop->cookie = &ctx;

  completion_reset(&ctx.completion);
  backend->block_proto->ops->queue(backend->block_proto->ctx, bop);
  completion_wait(&ctx.completion, ZX_TIME_INFINITE);

  if (ctx.status != ZX_OK) {
    zxlogf(ERROR, "[VPI]: Block I/O callback failed, ret: %d\n", ctx.status);
    zx_vmar_unmap(zx_vmar_root_self(), vaddr, buffer_size);
    zx_handle_close(vmo);
    free(bop);
    return ctx.status;
  }

  memcpy(buffer, (void*)vaddr, buffer_size);

  zx_vmar_unmap(zx_vmar_root_self(), vaddr, buffer_size);
  zx_handle_close(vmo);
  free(bop);

  return ctx.status;
}

static zx_status_t get_block_pinfo(vpart_backend_t* backend,
                                   physical_partition_info_t* pinfo) {
  memset(pinfo, 0, sizeof(physical_partition_info_t));
  uint32_t block_size = backend->block_info->block_size;
  void* buffer = malloc(block_size);
  if (!buffer)
    return ZX_ERR_NO_MEMORY;
  memset(buffer, 0, block_size);

  // read GPT Header (LBA 1)
  gpt_hdr_t header;
  zx_status_t status = sync_read_blocks(backend, 1, buffer, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[VPI]: Failed to read primary GPT header: %d\n", status);
    free(buffer);
    return status;
  }
  memcpy(&header, buffer, sizeof(gpt_hdr_t));

  if (header.magic != GPT_SIGNATURE) {
    zxlogf(ERROR, "[VPI]: Invalid GPT magic: 0x%016lX\n", header.magic);

    // read backup GPT Header
    uint64_t backup_lba = backend->block_info->block_count - 1;
    status = sync_read_blocks(backend, backup_lba, buffer, 1);
    if (status != ZX_OK) {
      zxlogf(ERROR, "[VPI]: Failed to read backup GPT header: %d\n", status);
      free(buffer);
      return status;
    }
    memcpy(&header, buffer, sizeof(gpt_hdr_t));
    if (header.magic != GPT_SIGNATURE) {
      zxlogf(ERROR, "[VPI]: Backup GPT header also invalid\n");
      free(buffer);
      return ZX_ERR_BAD_STATE;
    }

    zxlogf(INFO, "[VPI]: Using backup GPT header\n");
  }
  free(buffer);

  // debug log
  zxlogf(INFO, "[VPI]: GPT header valid:\n");
  zxlogf(INFO, "  First usable LBA: %lu\n", header.first_usable_lba);
  zxlogf(INFO, "  Last usable LBA: %lu\n", header.last_usable_lba);
  zxlogf(INFO, "  Partition entries LBA: %lu\n", header.partition_entries_lba);
  zxlogf(INFO, "  Number of entries: %u\n", header.num_partition_entries);
  zxlogf(INFO, "  Entry size: %u\n", header.size_of_partition_entry);
  zxlogf(INFO, "[VPI]: Get GPT partition info...\n");

  // read partition table
  uint32_t entry_size = header.size_of_partition_entry;
  uint32_t num_entries = header.num_partition_entries;
  uint64_t entries_lba = header.partition_entries_lba;

  pinfo->entry_size = entry_size;
  pinfo->num_entries = num_entries;

  /* Get the number of blocks occupied by the partition table */
  uint32_t partition_entries_per_block = block_size / entry_size;

  uint32_t blocks_to_read = (num_entries + partition_entries_per_block - 1) /
                            partition_entries_per_block;
  size_t buffer_size = blocks_to_read * block_size;

  void* entries_buffer = malloc(buffer_size);
  if (!entries_buffer) {
    return ZX_ERR_NO_MEMORY;
  }

  status =
      sync_read_blocks(backend, entries_lba, entries_buffer, blocks_to_read);
  if (status != ZX_OK) {
    free(entries_buffer);
    zxlogf(ERROR, "[VPI]: Failed to read partition entries sector: %d\n",
           status);
    return status;
  }
  pinfo->entries_buffer = entries_buffer;
  gpt_partition_entry_t* entries = (gpt_partition_entry_t*)entries_buffer;
  uint32_t flag = 0;

  for (uint32_t i = 0; i < num_entries; i++) {
    gpt_partition_entry_t* entry = &entries[i];
    uint8_t zero_guid[16] = {0};

    if (memcmp(entry->partition_type_guid, zero_guid, 16) == 0) {
      zxlogf(INFO, "[VPI]: partition is_empty %u\n", i);
      continue;
    }

    if ((memcmp(entry->partition_type_guid, partition_yocto_guid, 16) == 0) ||
        (memcmp(entry->partition_type_guid, partition_android_guid, 16) == 0) ||
        (memcmp(entry->partition_type_guid, partition_tbox_guid, 16) == 0)) {
      if (PARTITION_SHARE_OS(VPI_MAX_OS_YOCTO) ==
          (entry->attributes & PARTITION_SHARE_OS(VPI_MAX_OS_YOCTO))) {
        flag |= VPI_BIT(0);
        pinfo->num_entries_os[VPI_MAX_OS_YOCTO]++;
      }

      if (PARTITION_SHARE_OS(VPI_MAX_OS_ANDROID) ==
          (entry->attributes & PARTITION_SHARE_OS(VPI_MAX_OS_ANDROID))) {
        flag |= VPI_BIT(1);
        pinfo->num_entries_os[VPI_MAX_OS_ANDROID]++;
      }

      if (PARTITION_SHARE_OS(VPI_MAX_OS_TBOX) ==
          (entry->attributes & PARTITION_SHARE_OS(VPI_MAX_OS_TBOX))) {
        flag |= VPI_BIT(2);
        pinfo->num_entries_os[VPI_MAX_OS_TBOX]++;
      }
    }
  }

  if (flag == 0) {
    pinfo->num_entries_os[VPI_MAX_OS_YOCTO] = pinfo->num_entries;
    pinfo->num_os = 1;
    pinfo->has_partition_isolation = false;
  } else {
    for (uint32_t i = 0; i < VPI_MAX_OS_NUM; i++) {
      if (VPI_BIT(i) == (flag & VPI_BIT(i)))
        pinfo->num_os++;
      pinfo->has_partition_isolation = true;
    }
  }

  return ZX_OK;
}

static zx_status_t create_partition_mappings(vpart_iso_drv_t* vpi_drv,
                                             const gpt_partition_entry_t* entry,
                                             uint64_t pstart_lba,
                                             uint64_t size_blocks,
                                             uint32_t share_type,
                                             bool rw_perm,
                                             uint32_t ppart_id) {
  if (0 != vpi_drv->part_count) {
    vpi_drv->part_maps[vpi_drv->part_count].vstart_lba =
        vpi_drv->part_maps[vpi_drv->part_count - 1].vstart_lba +
        vpi_drv->part_maps[vpi_drv->part_count - 1].nblocks;
  }

  vpi_drv->part_maps[vpi_drv->part_count].pstart_lba = pstart_lba;
  vpi_drv->part_maps[vpi_drv->part_count].nblocks = size_blocks;
  vpi_drv->part_maps[vpi_drv->part_count].part_idx = vpi_drv->part_count + 1;
  vpi_drv->part_maps[vpi_drv->part_count].share_type = share_type;
  vpi_drv->part_maps[vpi_drv->part_count].rw_perm = rw_perm;

  memcpy(vpi_drv->part_maps[vpi_drv->part_count].name, entry->partition_name,
         GPT_NAME_LEN);

  vpi_drv->part_count++;
  vpi_drv->part_total_blocks += size_blocks;

  char partition_name[GPT_NAME_LEN + 1] = {0};
  for (int i = 0; i < GPT_NAME_LEN / 2; i++)
    partition_name[i] = entry->partition_name[2 * i];

  zxlogf(INFO,
         "[VPI][%d]: ppart_id: %u, vpart_id: %u, partition_name: %s, "
         "pstart_lba: %lu, pend_lba: %lu, vstart_lba: %lu\n",
         vpi_drv->vmid, ppart_id, vpi_drv->part_count, partition_name,
         pstart_lba, entry->ending_lba,
         vpi_drv->part_maps[vpi_drv->part_count - 1].vstart_lba);
  return ZX_OK;
}

static zx_status_t vpi_mapping_partitions(
    vpart_backend_t* backend,
    vpart_iso_drv_t* vpi_drv,
    const physical_partition_info_t* pinfo,
    int vmid) {
  uint32_t block_size = backend->block_info->block_size;
  uint32_t entry_size = pinfo->entry_size;
  uint32_t num_entries = pinfo->num_entries;

  /* Get the number of blocks occupied by the partition table */
  uint32_t partition_entries_per_block = block_size / entry_size;
  uint32_t partition_table_blocks =
      (pinfo->num_entries_os[vmid] + partition_entries_per_block - 1) /
      partition_entries_per_block;

  vpi_drv->partition_table_blocks = partition_table_blocks;
  uint64_t vstart_lba =
      partition_table_blocks +
      GPT_PARTITION_TABLE_LBA;  // MBR(1) + GPT Header(1) + Partition table
  vpi_drv->part_maps[0].vstart_lba = vstart_lba;

  zxlogf(INFO,
         "[VPI][%d]: The number of blocks occupied by the partition table: "
         "%u, vstart_lba: %lu\n",
         vmid, partition_table_blocks, vstart_lba);

  void* entries_buffer = pinfo->entries_buffer;
  gpt_partition_entry_t* entries = (gpt_partition_entry_t*)entries_buffer;

  for (uint32_t i = 0; i < num_entries; i++) {
    gpt_partition_entry_t* entry = &entries[i];
    uint8_t zero_guid[16] = {0};
    uint64_t pstart_lba = entry->starting_lba;
    uint64_t size_blocks = entry->ending_lba - entry->starting_lba + 1;
    uint64_t share_os = entry->attributes & PARTITION_SHARE_ALL;

    if (memcmp(entry->partition_type_guid, zero_guid, 16) == 0) {
      zxlogf(INFO, "[VPI][%d]: partition is_empty\n", vmid);
      continue;
    }

    uint32_t share_type = 0;
    switch (share_os) {
      case PARTITION_SHARE_ALL:
        share_type = SHARE_ALL;
        break;
      case PARTITION_SHARE_YOCTO_TBOX:
        share_type = SHARE_YOCTO_TBOX;
        break;
      case PARTITION_SHARE_YOCTO_ANDROID:
        share_type = SHARE_YOCTO_ANDROID;
        break;
      case PARTITION_SHARE_ANDROID_TBOX:
        share_type = SHARE_ANDROID_TBOX;
        break;
      default:
        break;
    }

    if (PARTITION_SHARE_OS(vmid) ==
        (entry->attributes & PARTITION_SHARE_OS(vmid))) {
      bool rw_perm_os = ((entry->attributes & PARTITION_RW_PERM_OS(vmid)) ==
                         PARTITION_RW_PERM_OS(vmid))
                            ? true
                            : false;
      create_partition_mappings(vpi_drv, entry, pstart_lba, size_blocks,
                                share_type, rw_perm_os, i);
    }
  }

  zxlogf(INFO, "[VPI][%d]: virt part count: %u ?= %u\n", vmid,
         vpi_drv->part_count, pinfo->num_entries_os[vmid]);
  return ZX_OK;
}

static void mbr_init(mbr_t* mbr, vpart_iso_drv_t* vpi_drv) {
  mbr->partition[0].boot_flag = 0x00;
  mbr->partition[0].start_chs[0] = 0x00;
  mbr->partition[0].start_chs[1] = 0x00;
  mbr->partition[0].start_chs[2] = 0x00;
  mbr->partition[0].type = 0xEE;
  mbr->partition[0].end_chs[0] = 0x00;
  mbr->partition[0].end_chs[1] = 0x00;
  mbr->partition[0].end_chs[2] = 0x00;
  mbr->partition[0].start_lba = 1;
  mbr->partition[0].sector_count = (uint32_t)(vpi_drv->total_blocks - 1);
  mbr->signature = 0xAA55;
}

static void gpt_partition_entries_init(gpt_partition_entry_t* partition_table,
                                       vpart_iso_drv_t* vpi_drv) {
  for (uint32_t i = 0; i < vpi_drv->part_count; i++) {
    gpt_partition_entry_t* partition_entry = &partition_table[i];

    if (VPI_MAX_OS_YOCTO == vpi_drv->vmid) {
      memcpy(partition_entry->partition_type_guid, partition_yocto_guid, 16);
    } else if (VPI_MAX_OS_ANDROID == vpi_drv->vmid) {
      memcpy(partition_entry->partition_type_guid, partition_android_guid, 16);
    } else if (VPI_MAX_OS_TBOX == vpi_drv->vmid) {
      memcpy(partition_entry->partition_type_guid, partition_tbox_guid, 16);
    }

    memcpy(partition_entry->partition_name, vpi_drv->part_maps[i].name,
           GPT_NAME_LEN);

    generate_random_guid(partition_entry->unique_partition_guid);

    partition_entry->starting_lba = vpi_drv->part_maps[i].vstart_lba;
    partition_entry->ending_lba =
        vpi_drv->part_maps[i].vstart_lba + vpi_drv->part_maps[i].nblocks - 1;
    partition_entry->attributes = 0;
  }
}

static void gpt_header_init(gpt_hdr_t* gpt_header,
                            gpt_hdr_t* backup_gpt_header,
                            const gpt_partition_entry_t* partition_table,
                            vpart_iso_drv_t* vpi_drv) {
  gpt_header->magic = GPT_SIGNATURE;  // "EFI PART"
  gpt_header->revision = GPT_REVISION;
  gpt_header->header_size = GPT_SIZE;
  gpt_header->current_lba = GPT_PRIMARY_HEADER_LBA;
  gpt_header->backup_lba = vpi_drv->total_blocks - 1;
  gpt_header->first_usable_lba =
      vpi_drv->partition_table_blocks + GPT_PARTITION_TABLE_LBA;
  gpt_header->last_usable_lba =
      vpi_drv->total_blocks - vpi_drv->partition_table_blocks - 2;
  gpt_header->partition_entries_lba = GPT_PARTITION_TABLE_LBA;
  gpt_header->num_partition_entries = vpi_drv->part_count;
  gpt_header->size_of_partition_entry = sizeof(gpt_partition_entry_t);

  generate_random_guid(gpt_header->guid);

  gpt_header->partition_entries_crc32 =
      crc32(0, (const unsigned char*)partition_table,
            gpt_header->size_of_partition_entry * vpi_drv->part_count);

  gpt_header->header_crc32 =
      crc32(0, (const unsigned char*)gpt_header, gpt_header->header_size);

  memcpy(backup_gpt_header, gpt_header, sizeof(gpt_hdr_t));

  backup_gpt_header->current_lba = vpi_drv->total_blocks - 1;
  backup_gpt_header->backup_lba = GPT_PRIMARY_HEADER_LBA;
  backup_gpt_header->partition_entries_lba =
      vpi_drv->total_blocks - vpi_drv->partition_table_blocks - 1;
  backup_gpt_header->header_crc32 = 0;

  zxlogf(INFO, "[VPI][%d]: Partition table CRCs check: %08x ?= %08x\n",
         vpi_drv->vmid, gpt_header->partition_entries_crc32,
         backup_gpt_header->partition_entries_crc32);

  backup_gpt_header->header_crc32 =
      crc32(0, (const unsigned char*)backup_gpt_header,
            backup_gpt_header->header_size);
}

static zx_status_t create_virtual_partition(vpart_backend_t* backend,
                                            vpart_iso_drv_t* vpi_drv) {
  uintptr_t vaddr;
  zx_status_t ret;
  zx_handle_t vmo;

  uint32_t block_size = backend->block_info->block_size;
  vpi_drv->vstart_lba = vpi_drv->part_maps[0].vstart_lba;
  vpi_drv->vend_lba =
      vpi_drv->part_maps[0].vstart_lba + vpi_drv->part_total_blocks - 1;

  /* MBR + GPT Header + GPT partition table + Data partitions + Backup GPT
   * partition table + Backup GPT Header */
  vpi_drv->total_blocks = GPT_FIXED_BLOCKS + vpi_drv->part_total_blocks +
                          vpi_drv->partition_table_blocks * 2;
  zxlogf(INFO, "[VPI][%d]: vpi_drv->total_blocks %lu\n", vpi_drv->vmid,
         vpi_drv->total_blocks);

  memcpy(&vpi_drv->virt_info, backend->block_info, sizeof(block_info_t));
  vpi_drv->virt_info.block_count = vpi_drv->total_blocks;

  /* MBR + GPT Header + GPT partition table + Backup GPT partition table +
   * Backup GPT Header */
  vpi_drv->capa =
      (vpi_drv->total_blocks - vpi_drv->part_total_blocks) * block_size;
  vpi_drv->rd_vmo = ZX_HANDLE_INVALID;
  vpi_drv->rd_vaddr = NULL;

  ret = zx_vmo_create(vpi_drv->capa, 0, &vmo);
  if (ret != ZX_OK) {
    zxlogf(ERROR, "[VPI][%d]: Failed to alloc vmo in virtual partition block\n",
           vpi_drv->vmid);
    return ZX_ERR_NOT_SUPPORTED;
  }
  ret = zx_vmar_map(zx_vmar_root_self(),
                    ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_READ, 0, vmo, 0,
                    vpi_drv->capa, &vaddr);
  if (ret != ZX_OK) {
    zx_handle_close(vmo);
    zxlogf(ERROR, "[VPI][%d]: Failed to map vmo in virtual partition block\n",
           vpi_drv->vmid);
    return ZX_ERR_NOT_SUPPORTED;
  }

  vpi_drv->rd_vmo = vmo;
  vpi_drv->rd_vaddr = (void*)vaddr;
  uint8_t* base_addr = (uint8_t*)vaddr;

  /* init MBR */
  mbr_t* mbr = (mbr_t*)base_addr;
  memset(mbr, 0, block_size);
  mbr_init(mbr, vpi_drv);

  /* init partition table */
  gpt_partition_entry_t* partition_table =
      (gpt_partition_entry_t*)(base_addr +
                               GPT_PARTITION_TABLE_LBA * block_size);
  gpt_partition_entry_t* backup_partition_table =
      (gpt_partition_entry_t*)(base_addr + (GPT_PARTITION_TABLE_LBA +
                                            vpi_drv->partition_table_blocks) *
                                               block_size);

  memset(partition_table, 0, vpi_drv->partition_table_blocks * block_size);
  memset(backup_partition_table, 0,
         vpi_drv->partition_table_blocks * block_size);
  gpt_partition_entries_init(partition_table, vpi_drv);
  memcpy(backup_partition_table, partition_table,
         vpi_drv->partition_table_blocks * block_size);

  /* init gpt header*/
  gpt_hdr_t* gpt_header = (gpt_hdr_t*)(base_addr + block_size);
  gpt_hdr_t* backup_gpt_header =
      (gpt_hdr_t*)(base_addr + vpi_drv->capa - block_size);
  memset(gpt_header, 0, block_size);
  memset(backup_gpt_header, 0, block_size);
  gpt_header_init(gpt_header, backup_gpt_header, partition_table, vpi_drv);

  return ZX_OK;
}

zx_status_t vpart_isolate_create(block_protocol_t* block_proto,
                                 block_info_t* block_info,
                                 size_t block_op_size,
                                 int vmid,
                                 vpart_iso_drv_t** out_vpi,
                                 bool* out_has_partition_isolation) {
  if (!block_proto || !block_proto->ops || !block_proto->ops->queue ||
      !block_info || block_op_size == 0 || !out_vpi ||
      !out_has_partition_isolation) {
    return ZX_ERR_INVALID_ARGS;
  }

  *out_vpi = NULL;
  *out_has_partition_isolation = false;

  vpart_backend_t backend = {
      .block_proto = block_proto,
      .block_info = block_info,
      .block_op_size = block_op_size,
  };
  physical_partition_info_t pinfo;
  zx_status_t status = get_block_pinfo(&backend, &pinfo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[VPI][%d]: failed to parse gpt: %d\n", vmid, status);
    return status;
  }
  zxlogf(INFO, "[VPI][%d]: num_os %u, partition isolation: %s\n", vmid,
         pinfo.num_os, pinfo.has_partition_isolation ? "true" : "false");

  *out_has_partition_isolation = pinfo.has_partition_isolation;

  if (pinfo.has_partition_isolation) {
    vpart_iso_drv_t* vpi_drv = calloc(1, sizeof(*vpi_drv));
    if (!vpi_drv) {
      zxlogf(ERROR, "[VPI][%d]: Failed to alloc memory for vpart_iso_drv_t\n",
             vmid);
      free(pinfo.entries_buffer);
      return ZX_ERR_NO_MEMORY;
    }

    vpi_drv->cache_map = NULL;
    vpi_drv->vmid = vmid;
    vpi_drv->has_partition_isolation = pinfo.has_partition_isolation;

    status = vpi_mapping_partitions(&backend, vpi_drv, &pinfo, vmid);
    if (status != ZX_OK) {
      zxlogf(ERROR, "[VPI][%d]: failed to create partition mappings: %d\n",
             vmid, status);
      free(vpi_drv);
      free(pinfo.entries_buffer);
      return status;
    }

    status = create_virtual_partition(&backend, vpi_drv);
    if (status != ZX_OK) {
      zxlogf(ERROR, "[VPI][%d]: failed to create virtual partition: %d\n", vmid,
             status);
      free(vpi_drv);
      free(pinfo.entries_buffer);
      return status;
    }

    *out_vpi = vpi_drv;
  }

  free(pinfo.entries_buffer);

  return ZX_OK;
}

void vpart_isolate_destroy(vpart_iso_drv_t* vpi_drv) {
  if (!vpi_drv) {
    return;
  }

  if (vpi_drv->rd_vaddr)
    zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)vpi_drv->rd_vaddr,
                  vpi_drv->capa);

  if (vpi_drv->rd_vmo != ZX_HANDLE_INVALID)
    zx_handle_close(vpi_drv->rd_vmo);

  vpi_drv->rd_vaddr = NULL;
  vpi_drv->rd_vmo = ZX_HANDLE_INVALID;
  free(vpi_drv);
}

uint64_t vpart_isolate_get_block_count(vpart_iso_drv_t* vpi_drv) {
  if (!vpi_drv) {
    return 0;
  }
  return vpi_drv->virt_info.block_count;
}

static const partition_mapping_t* find_partition_map(vpart_iso_drv_t* vpi_drv,
                                                     uint64_t vblock) {
  partition_mapping_t* map = vpi_drv->cache_map;
  if (map) {
    if ((vblock >= map->vstart_lba) &&
        (vblock < map->vstart_lba + map->nblocks)) {
      return map;
    }
  }

  int left = 0;
  int right = vpi_drv->part_count - 1;

  while (left <= right) {
    int mid = left + (right - left) / 2;
    map = &vpi_drv->part_maps[mid];

    if (vblock < map->vstart_lba) {
      right = mid - 1;
    } else if (vblock >= map->vstart_lba + map->nblocks) {
      left = mid + 1;
    } else {
      vpi_drv->cache_map = map;
      return map;
    }
  }

  return NULL;
}

static bool vpart_isolate_range_valid(uint64_t start,
                                      uint64_t length,
                                      uint64_t total_blocks) {
  return length != 0 && start < total_blocks && length <= total_blocks - start;
}

static bool vpart_isolate_map_contains(const partition_mapping_t* map,
                                       uint64_t vblock,
                                       uint64_t length) {
  if (!map || vblock < map->vstart_lba) {
    return false;
  }

  uint64_t offset = vblock - map->vstart_lba;
  return offset < map->nblocks && length <= map->nblocks - offset;
}

static zx_status_t virtual_partition_read(vpart_iso_drv_t* vpi_drv,
                                          block_op_t* bop,
                                          uint64_t _dev_off) {
  uint64_t vmo_off, dev_off, len;
  uint32_t block_size = vpi_drv->virt_info.block_size;
  zx_status_t status;

  if (!vpi_drv->rd_vaddr) {
    zxlogf(ERROR, "[VPI][%d]: The virtual GPT address is NULL\n",
           vpi_drv->vmid);
    return ZX_ERR_INVALID_ARGS;
  }
  vmo_off = bop->rw.offset_vmo * block_size;
  if (0 != _dev_off) {
    dev_off = _dev_off * block_size;
  } else {
    dev_off = bop->rw.offset_dev * block_size;
  }
  len = bop->rw.length * block_size;

  if (bop->rw.sg_count > 0) {
    if (bop->rw.sg_count > BLOCK_OP_MAX_SG_REGIONS) {
      return ZX_ERR_INVALID_ARGS;
    }
    uint64_t copied = 0;
    for (uint32_t i = 0; i < bop->rw.sg_count; i++) {
      uint64_t seg_len = bop->rw.sg[i].length * block_size;
      if (copied > len || seg_len == 0 || seg_len > len - copied) {
        return ZX_ERR_INVALID_ARGS;
      }
      status = zx_vmo_write(
          bop->rw.vmo, (uint8_t*)vpi_drv->rd_vaddr + dev_off + copied,
          bop->rw.sg[i].offset_vmo * block_size, seg_len);
      if (status != ZX_OK) {
        zxlogf(ERROR, "[VPI][%d]: failed to sg read to vmo, ret: %d\n",
               vpi_drv->vmid, status);
        return status;
      }
      copied += seg_len;
    }
    return copied == len ? ZX_OK : ZX_ERR_INVALID_ARGS;
  }

  status = zx_vmo_write(bop->rw.vmo, (uint8_t*)vpi_drv->rd_vaddr + dev_off,
                        vmo_off, len);
  if (status != ZX_OK)
    zxlogf(ERROR, "[VPI][%d]: failed to read to vmo, ret: %d\n", vpi_drv->vmid,
           status);

  return status;
}

zx_status_t vpart_isolate_process(vpart_iso_drv_t* vpi_drv,
                                  block_op_t* bop,
                                  bool* rgpt_flag) {
  if (!vpi_drv || !bop || !rgpt_flag) {
    zxlogf(ERROR, "[VPI]: NULL pointer in input parameters\n");
    return ZX_ERR_INVALID_ARGS;
  }
  *rgpt_flag = false;

  uint64_t vblock = 0;
  uint32_t length = 0;
  uint64_t dev_off = 0;
  const partition_mapping_t* map = NULL;
  bool block_flag = false;
  uint64_t vstart_lba = vpi_drv->vstart_lba;
  uint64_t vend_lba = vpi_drv->vend_lba;

  if (VPI_OP_IS_RW(bop)) {
    if (!vpart_isolate_range_valid(bop->rw.offset_dev, bop->rw.length,
                                   vpi_drv->total_blocks)) {
      if (bop->rw.length == 0) {
        zxlogf(ERROR, "[VPI][%d]: rw length is 0 (invalid)\n",
               vpi_drv->vmid);
        return ZX_ERR_INVALID_ARGS;
      }
      zxlogf(ERROR,
             "[VPI][%d]: out of range: start=%lu length=%u total=%lu\n",
             vpi_drv->vmid, bop->rw.offset_dev, bop->rw.length,
             vpi_drv->total_blocks);
      return ZX_ERR_OUT_OF_RANGE;
    }

    vblock = bop->rw.offset_dev;
    length = bop->rw.length;
  } else if (VPI_OP_IS_ERASE(bop)) {
    if (!vpart_isolate_range_valid(bop->erase.offset_dev, bop->erase.length,
                                   vpi_drv->total_blocks)) {
      if (bop->erase.length == 0) {
        zxlogf(ERROR, "[VPI][%d]: erase length is 0 (invalid)\n",
               vpi_drv->vmid);
        return ZX_ERR_INVALID_ARGS;
      }
      zxlogf(ERROR,
             "[VPI][%d]: out of range: start=%lu length=%u total=%lu\n",
             vpi_drv->vmid, bop->erase.offset_dev, bop->erase.length,
             vpi_drv->total_blocks);
      return ZX_ERR_OUT_OF_RANGE;
    }

    vblock = bop->erase.offset_dev;
    length = bop->erase.length;
  }

  if ((vblock + length - 1) < vstart_lba || vblock > vend_lba) {
    block_flag = true;
    *rgpt_flag = true;
    if (vblock > vend_lba) {
      dev_off = vblock - vpi_drv->part_total_blocks;
    }
  } else {
    map = find_partition_map(vpi_drv, vblock);
    if (!map) {
      zxlogf(ERROR, "[VPI][%d]: failed to find partition map, vblock: %lu\n",
             vpi_drv->vmid, vblock);
      return ZX_ERR_OUT_OF_RANGE;
    }
    bool rw_perm = map->rw_perm;

    if (false == rw_perm &&
        ((bop->command == BLOCK_OP_WRITE) || VPI_OP_IS_ERASE(bop))) {
      zxlogf(ERROR, "[VPI][%d]: This operation is not supported\n",
             vpi_drv->vmid);
      return ZX_ERR_ACCESS_DENIED;
    }

    if (!vpart_isolate_map_contains(map, vblock, length)) {
      zxlogf(ERROR, "[VPI][%d]: Out-of-bounds partition operation. %lu > %lu\n",
             vpi_drv->vmid, vblock + length, map->vstart_lba + map->nblocks);
      return ZX_ERR_OUT_OF_RANGE;
    }
  }

  if (block_flag) {
    if (BLOCK_OP_READ == (bop->command & BLOCK_OP_MASK)) {
      zx_status_t status = virtual_partition_read(vpi_drv, bop, dev_off);
      return status;
    } else {
      zxlogf(ERROR, "[VPI][%d]: Write gpt is not supported\n", vpi_drv->vmid);
      return ZX_ERR_ACCESS_DENIED;
    }
  }

  uint64_t offset_in_part = vblock - map->vstart_lba;
  uint64_t pblock_start = map->pstart_lba + offset_in_part;

  if (VPI_OP_IS_RW(bop)) {
    bop->rw.offset_dev = pblock_start;
  } else if (VPI_OP_IS_ERASE(bop)) {
    bop->erase.offset_dev = pblock_start;
  }

  return ZX_OK;
}

zx_status_t vpart_isolate_init(guest_ctx_t* guest,
                               vblock_drv_t* dev,
                               int vmid) {
  if (!guest || !dev) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = vpart_isolate_create(
      dev->block_proto, dev->block_info, dev->block_op_size, vmid,
      &guest->vpi_drv, &guest->has_partition_isolation);
  if (status != ZX_OK) {
    guest->vpi_drv = NULL;
    guest->has_partition_isolation = false;
  }
  return status;
}

void vpart_isolate_release(guest_ctx_t* guest) {
  if (!guest) {
    return;
  }
  vpart_isolate_destroy(guest->vpi_drv);
  guest->vpi_drv = NULL;
  guest->has_partition_isolation = false;
}

void vpart_isolate_get_vbinfo(guest_ctx_t* guest, uint64_t* vblock_count) {
  if (!guest || !vblock_count) {
    return;
  }
  *vblock_count = vpart_isolate_get_block_count(guest->vpi_drv);
  zxlogf(INFO, "[VPI][%d]: guest->vpi_drv->virt_info.block_count %lu\n",
         guest->vmid, *vblock_count);
}

zx_status_t vpart_isolate_process_bop(guest_ctx_t* guest,
                                      block_op_t* bop,
                                      bool* rgpt_flag) {
  if (!guest) {
    return ZX_ERR_INVALID_ARGS;
  }
  return vpart_isolate_process(guest->vpi_drv, bop, rgpt_flag);
}
