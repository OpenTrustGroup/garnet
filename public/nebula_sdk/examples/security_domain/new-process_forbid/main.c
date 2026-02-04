#include <stdio.h>

#include <zircon/syscalls.h>
#include <zircon/process.h>

int main(int argc, char *argv[])
{
    printf("Create new process by zx_process_create:\n");

    int rlt;
    zx_handle_t proc;
    zx_handle_t vmar;

    rlt = zx_process_create(zx_job_default(), "new-process", 3u, 0, &proc, &vmar);
    if ( rlt == ZX_OK) {
	printf("Success.\n");
	zx_handle_close(proc);
	zx_handle_close(vmar);
    } else if (rlt == ZX_ERR_ACCESS_DENIED) {
	printf("Failed. Access denied\n");
    } else {
	printf("Failed. unknown error=%d\n", rlt);
    }

    return 0;
}
