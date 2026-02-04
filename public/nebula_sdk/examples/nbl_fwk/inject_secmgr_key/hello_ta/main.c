#include "lib.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
	printf("hello ha.\n");

	if (argc > 1) {
		for(int i=1; i<argc; i++) {
			print(argv[i]);
		}
	}

	return 0;
}
