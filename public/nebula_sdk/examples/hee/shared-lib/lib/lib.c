#include "lib.h"

# include <stdio.h>

int count = 0;

void print()
{
    printf("Print in shared library of HEE: count=%d.\n", count);
}
