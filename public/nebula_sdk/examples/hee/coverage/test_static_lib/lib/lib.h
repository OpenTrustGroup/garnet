/**
 * Single link list.
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Node {
  struct Node *next;
  int data;
};

/**
 * Create a new single list.
 */
struct Node *single_list_create();

/**
 * Print all nodes of list.
 */
void single_list_print(struct Node *L);

/**
 * insert one data into list at tail.
 */
void single_list_insert(struct Node *L, int n);

/**
 * Remove all nodes in list.
 */
struct Node *single_list_clean(struct Node *L);

/**
 * Delete odd data in list.s
 */
struct Node *single_insert_del_odd(struct Node *L);

/**
 * Bubble sort the list.
 */
void single_list_bubble_sort(struct Node *L);

#ifdef __cplusplus
}
#endif
