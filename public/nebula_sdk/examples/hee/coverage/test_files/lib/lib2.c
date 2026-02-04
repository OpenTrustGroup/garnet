#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "lib.h"

void single_list_bubble_sort(struct Node *L) {
  struct Node *p, *q, *tail;
  tail = NULL;
  if (L->next != NULL) {
    while ((L->next->next) != tail) {
      p = L;
      q = L->next;
      while (q->next != tail) {
        if ((q->data) > (q->next->data)) {
          p->next = q->next;
          q->next = q->next->next;
          p->next->next = q;
          q = p->next;
        }
        q = q->next;
        p = p->next;
      }
      tail = q;
    }
  }
}
