// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>
#include <cinttypes>

#include "garnet/lib/machina/arch/x86/page_table.h"
#include "garnet/lib/machina/phys_mem_fake.h"
#include "gtest/gtest.h"

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define ASSERT_EPT_EQ(actual, expected, size, ...) \
  do {                                             \
    int cmp = memcmp(actual, expected, size);      \
    if (cmp != 0)                                  \
      hexdump_result(actual, expected);            \
    ASSERT_EQ(cmp, 0, ##__VA_ARGS__);              \
  } while (0)

#define INITIALIZE_PAGE_TABLE \
  {                           \
    {                         \
      { 0 }                   \
    }                         \
  }

static void* page_addr(void* base, size_t page) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(base);
  addr += PAGE_SIZE * page;
  return reinterpret_cast<void*>(addr);
}

static void hexdump_ex(const void* ptr, size_t len, uint64_t disp_addr) {
  uintptr_t address = (uintptr_t)ptr;
  size_t count;

  for (count = 0; count < len; count += 16) {
    union {
      uint32_t buf[4];
      uint8_t cbuf[16];
    } u;
    size_t s = ROUNDUP(MIN(len - count, 16), 4);
    size_t i;

    printf(((disp_addr + len) > 0xFFFFFFFF) ? "0x%016" PRIx64 ": "
                                            : "0x%08" PRIx64 ": ",
           disp_addr + count);

    for (i = 0; i < s / 4; i++) {
      u.buf[i] = ((const uint32_t*)address)[i];
      printf("%08x ", u.buf[i]);
    }
    for (; i < 4; i++) {
      printf("         ");
    }
    printf("|");

    for (i = 0; i < 16; i++) {
      char c = u.cbuf[i];
      if (i < s && isprint(c)) {
        printf("%c", c);
      } else {
        printf(".");
      }
    }
    printf("|\n");
    address += 16;
  }
}

static void hexdump_result(void* actual, void* expected) {
  printf("\nactual:\n");
  hexdump_ex(page_addr(actual, 0), 16, PAGE_SIZE * 0);
  hexdump_ex(page_addr(actual, 1), 16, PAGE_SIZE * 1);
  hexdump_ex(page_addr(actual, 2), 16, PAGE_SIZE * 2);
  hexdump_ex(page_addr(actual, 3), 32, PAGE_SIZE * 3);
  printf("expected:\n");
  hexdump_ex(page_addr(expected, 0), 16, PAGE_SIZE * 0);
  hexdump_ex(page_addr(expected, 1), 16, PAGE_SIZE * 1);
  hexdump_ex(page_addr(expected, 2), 16, PAGE_SIZE * 2);
  hexdump_ex(page_addr(expected, 3), 32, PAGE_SIZE * 3);
}

typedef struct {
  uint64_t entries[512];
} page_table;

enum {
  X86_PTE_P = 0x01,  /* P    Valid           */
  X86_PTE_RW = 0x02, /* R/W  Read/Write      */
  X86_PTE_PS = 0x80, /* PS   Page size       */
};

TEST(PageTableTest, 1gb) {
  page_table actual[4] = INITIALIZE_PAGE_TABLE;
  page_table expected[4] = INITIALIZE_PAGE_TABLE;

  ASSERT_EQ(machina::create_page_table(
                machina::PhysMemFake((uintptr_t)actual, 1 << 30)),
            ZX_OK);

  // pml4
  expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
  // pdp
  expected[1].entries[0] = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
  ASSERT_EPT_EQ(actual, expected, sizeof(actual));
}

TEST(PageTableTest, 2mb) {
  page_table actual[4] = INITIALIZE_PAGE_TABLE;
  page_table expected[4] = INITIALIZE_PAGE_TABLE;

  ASSERT_EQ(machina::create_page_table(
                machina::PhysMemFake((uintptr_t)actual, 2 << 20)),
            ZX_OK);

  // pml4
  expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
  // pdp
  expected[1].entries[0] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
  // pd
  expected[2].entries[0] = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
  ASSERT_EPT_EQ(actual, expected, sizeof(actual));
}

TEST(PageTableTest, 4kb) {
  page_table actual[4] = INITIALIZE_PAGE_TABLE;
  page_table expected[4] = INITIALIZE_PAGE_TABLE;

  ASSERT_EQ(machina::create_page_table(
                machina::PhysMemFake((uintptr_t)actual, 4 * 4 << 10)),
            ZX_OK);

  // pml4
  expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
  // pdp
  expected[1].entries[0] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
  // pd
  expected[2].entries[0] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;
  // pt
  expected[3].entries[0] = PAGE_SIZE * 0 | X86_PTE_P | X86_PTE_RW;
  expected[3].entries[1] = PAGE_SIZE * 1 | X86_PTE_P | X86_PTE_RW;
  expected[3].entries[2] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;
  expected[3].entries[3] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;
  ASSERT_EPT_EQ(actual, expected, sizeof(actual));
}

TEST(PageTableTest, MixedPages) {
  page_table actual[4] = INITIALIZE_PAGE_TABLE;
  page_table expected[4] = INITIALIZE_PAGE_TABLE;

  ASSERT_EQ(machina::create_page_table(
                machina::PhysMemFake((uintptr_t)actual, (2 << 20) + (4 << 10))),
            ZX_OK);

  // pml4
  expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;
  // pdp
  expected[1].entries[0] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;

  // pd
  expected[2].entries[0] = X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
  expected[2].entries[1] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;

  // pt
  expected[3].entries[0] = (2 << 20) | X86_PTE_P | X86_PTE_RW;
  ASSERT_EPT_EQ(actual, expected, sizeof(actual));
}

// Create a page table for 2gb + 123mb + 32kb bytes.
TEST(PageTableTest, Complex) {
  page_table actual[4] = INITIALIZE_PAGE_TABLE;
  page_table expected[4] = INITIALIZE_PAGE_TABLE;

  // 2gb + 123mb + 32kb of RAM. This breaks down as follows:
  //
  // PML4
  // > 1 pointer to a PDPT
  //
  // PDPT
  // > 2 direct-mapped 1gb regions
  // > 1 ponter to a PD
  //
  // PD
  // > 61 direct-mapped 2mb regions
  // > 1 pointer to a PT
  //
  // PT
  // >  264 mapped pages
  ASSERT_EQ(machina::create_page_table(
                machina::PhysMemFake((uintptr_t)actual, 0x87B08000)),
            ZX_OK);

  // pml4
  expected[0].entries[0] = PAGE_SIZE | X86_PTE_P | X86_PTE_RW;

  // pdp
  expected[1].entries[0] = (0l << 30) | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
  expected[1].entries[1] = (1l << 30) | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
  expected[1].entries[2] = PAGE_SIZE * 2 | X86_PTE_P | X86_PTE_RW;

  // pd - starts at 2GB
  const uint64_t pdp_offset = 2l << 30;
  for (int i = 0; i < 62; ++i) {
    expected[2].entries[i] =
        (pdp_offset + (i << 21)) | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
  }
  expected[2].entries[61] = PAGE_SIZE * 3 | X86_PTE_P | X86_PTE_RW;

  // pt - starts at 2GB + 122MB
  const uint64_t pd_offset = pdp_offset + (61l << 21);
  for (int i = 0; i < 264; ++i) {
    expected[3].entries[i] = (pd_offset + (i << 12)) | X86_PTE_P | X86_PTE_RW;
  }
  ASSERT_EPT_EQ(actual, expected, sizeof(actual));
}
