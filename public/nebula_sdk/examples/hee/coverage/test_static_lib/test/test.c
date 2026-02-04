#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "lib.h"
#include "test.h"

void test(struct Node *L) {
  // Print all nodes in list.
  printf("Print all random numbers.\n");
  single_list_print(L);

  // Sort list and print.
  single_list_bubble_sort(L);

  printf("Print all random numbers by order.\n");
  single_list_print(L);

  // Delete odd numbers in list and print.
  single_insert_del_odd(L);
  printf("Delete odd and print last even numbers.\n");
  single_list_print(L);

  // Delete all nodes in list and print.
  single_list_clean(L);
  printf("Delete all numbers in single list and print.\n");
  single_list_print(L);
}
