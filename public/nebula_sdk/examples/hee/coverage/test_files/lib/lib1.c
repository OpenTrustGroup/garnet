#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "lib.h"

struct Node *single_list_create() {
  struct Node *L;
  L = (struct Node *)malloc(sizeof(struct Node));
  L->data = -1;
  L->next = NULL;

  return L;
}

void single_list_print(struct Node *L) {
  struct Node *q;
  q = L->next;
  while (q != NULL) {
    printf("%d ", q->data);
    q = q->next;
  }
  printf("\n");
}

struct Node *single_insert_del_odd(struct Node *L) {
  struct Node *p;
  p = L;

  while (p->next != NULL) {
    if (p->next->data % 2 != 0) {
      struct Node *t = p->next;
      p->next = t->next;
      free(t);
    } else
      p = p->next;
  }
  return L;
}

void single_list_insert(struct Node *L, int n) {
  struct Node *p, *q;
  p = L;
  q = (struct Node *)malloc(sizeof(struct Node));
  while (p->next != NULL) {
    p = p->next;
  }
  p->next = q;
  q->data = n;
  q->next = NULL;
}

struct Node *single_list_clean(struct Node *L) {
  struct Node *p, *q;
  p = L;
  while (p->next != NULL & p->next->next != NULL) {
    q = p->next;
    p->next = p->next->next;
    free(q);
  }
  free(p->next);
  p->next = NULL;

  return L;
}
