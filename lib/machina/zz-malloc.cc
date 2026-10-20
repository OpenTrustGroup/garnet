// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define BLOCK_HEADER_SIZE ALIGN(sizeof(block_header_t))
#define MIN_BLOCK_SIZE (BLOCK_HEADER_SIZE + ALIGNMENT)

#include <stddef.h>
#include <stdint.h>

typedef struct block_header {
  size_t size;
  struct block_header *next;
  int is_free;
} block_header_t;

typedef struct {
  void *memory_start;
  size_t total_size;
  block_header_t *free_list;
} allocator_t;

void allocator_init(allocator_t *allocator, void *memory, size_t size);
void *allocator_alloc(allocator_t *allocator, size_t size);
void allocator_free(allocator_t *allocator, void *ptr);
size_t allocator_get_free_memory(allocator_t *allocator);
void allocator_dump(allocator_t *allocator);

void allocator_init(allocator_t *allocator, void *memory, size_t size) {
  if (!allocator || !memory || size < MIN_BLOCK_SIZE) {
    return;
  }

  uintptr_t start_addr = (uintptr_t)memory;
  uintptr_t aligned_addr = (start_addr + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
  size_t adjust = aligned_addr - start_addr;

  if (size <= adjust + MIN_BLOCK_SIZE) {
    return;
  }

  allocator->memory_start = (void *)aligned_addr;
  allocator->total_size = size - adjust;

  block_header_t *first_block = (block_header_t *)allocator->memory_start;
  first_block->size = allocator->total_size;
  first_block->next = NULL;
  first_block->is_free = 1;

  allocator->free_list = first_block;
}

void *allocator_alloc(allocator_t *allocator, size_t size) {
  if (!allocator || size == 0) {
    return NULL;
  }

  size_t required_size = BLOCK_HEADER_SIZE + ALIGN(size);

  block_header_t *current = allocator->free_list;
  block_header_t *prev = NULL;
  block_header_t *best_fit = NULL;
  block_header_t *best_fit_prev = NULL;

  while (current) {
    if (current->is_free && current->size >= required_size) {
      if (!best_fit || current->size < best_fit->size) {
        best_fit = current;
        best_fit_prev = prev;
      }
    }
    prev = current;
    current = current->next;
  }

  if (!best_fit) {
    return NULL;
  }

  if (best_fit->size >= required_size + MIN_BLOCK_SIZE) {
    block_header_t *new_block =
        (block_header_t *)((char *)best_fit + required_size);
    new_block->size = best_fit->size - required_size;
    new_block->next = best_fit->next;
    new_block->is_free = 1;

    best_fit->size = required_size;
    best_fit->next = new_block;
  }

  if (best_fit_prev) {
    best_fit_prev->next = best_fit->next;
  } else {
    allocator->free_list = best_fit->next;
  }

  best_fit->is_free = 0;
  best_fit->next = NULL;

  return (void *)((char *)best_fit + BLOCK_HEADER_SIZE);
}

void allocator_free(allocator_t *allocator, void *ptr) {
  if (!allocator || !ptr) {
    return;
  }

  block_header_t *block = (block_header_t *)((char *)ptr - BLOCK_HEADER_SIZE);

  if (!block->is_free) {
    block->is_free = 1;

    block_header_t *current = allocator->free_list;
    block_header_t *prev = NULL;

    while (current) {
      if ((char *)current + current->size == (char *)block) {
        current->size += block->size;
        return;
      } else if ((char *)block + block->size == (char *)current) {
        block->size += current->size;
        block->next = current->next;
        if (prev) {
          prev->next = block;
        } else {
          allocator->free_list = block;
        }
        return;
      }
      prev = current;
      current = current->next;
    }

    block->next = allocator->free_list;
    allocator->free_list = block;
  }
}

size_t allocator_get_free_memory(allocator_t *allocator) {
  if (!allocator) {
    return 0;
  }

  size_t free_memory = 0;
  block_header_t *current = allocator->free_list;

  while (current) {
    if (current->is_free) {
      free_memory += current->size;
    }
    current = current->next;
  }

  return free_memory;
}

void allocator_dump(allocator_t *allocator) {
  if (!allocator) {
    return;
  }

  printf("Allocator Dump:\n");
  printf("Memory Start: %p\n", allocator->memory_start);
  printf("Total Size: %zu bytes\n", allocator->total_size);
  printf("Free Memory: %zu bytes\n", allocator_get_free_memory(allocator));

  block_header_t *current = allocator->free_list;
  int block_count = 0;

  printf("Free List:\n");
  while (current) {
    printf("  Block %d: addr=%p, size=%zu, free=%d, next=%p\n", block_count++,
           current, current->size, current->is_free, current->next);
    current = current->next;
  }
}
