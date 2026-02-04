#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "lib.h"
#include "test.h"

int main() {
  srand((int)time(NULL));

  // Create a new list
  struct Node *L;
  L = single_list_create();

  // Add some nodes into list.
  for (int i = 0; i < 10; i++) {
    single_list_insert(L, rand() % 100);
  }

  test(L);

  // Release all resources in list.
  free(L);

  return 0;
}
