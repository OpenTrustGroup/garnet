#include <stdio.h>
#include <string.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// test that invoke the library in system image.
void test_math()
{
	double angle = 1;
	printf("sin(1) + cos(1) =  %f + %f = %f\n", sin(angle), cos(angle), sin(angle) + cos(angle));
}

// test read data files in FAR.
void test_read_data()
{
	char buf[128];

	int fd = open("/pkg/data/test1.dat", O_RDONLY);
	if (fd < 0) {
		printf("Cann't open the data/test1.dat");
		return;
	}

	memset(buf, '\0', sizeof(buf));
	while(read(fd, buf, sizeof(buf)-1) > 0) {
		printf("%s\n", buf);
		memset(buf, '\0', sizeof(buf));
	}

	close(fd);

	fd = open("/pkg/data/test2.dat", O_RDONLY);
	if (fd < 0) {
		printf("Cann't open the data/test2.dat");
		return;
	}

	memset(buf, '\0', sizeof(buf));
	while(read(fd, buf, sizeof(buf)-1) > 0) {
		printf("%s\n", buf);
		memset(buf, '\0', sizeof(buf));
	}

	close(fd);
}

int main(int argc, char *argv[])
{
    printf("argc=%d\n", argc);
	if (argc > 1) {
		for(int i=1; i<argc; i++) {
			printf("ARG[%d]=%s\n", i, argv[i]);
		}
	}
	test_math();
	test_read_data();
	return 0;
}
