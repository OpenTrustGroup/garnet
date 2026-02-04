#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <zircon/types.h>

#include "fuz_common.h"
#include <uapi/err.h>

#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

//////////////////////////////////////////////////
// global test functions list.
#define MAX_TEST_FUNCS 1024
typedef struct {
  function_description *description;
  const char *name;
} test_entry_t;
test_entry_t test_entries[MAX_TEST_FUNCS];
int test_funcs_count = 0;

void register_func(prepare_func do_prepare, const char *name) {
  if (test_funcs_count >= MAX_TEST_FUNCS) {
    printf("Count of test functions is more than MAX VALUE(%d)\n",
           MAX_TEST_FUNCS);
    exit(1);
  }
  test_entries[test_funcs_count].name = name;
  test_entries[test_funcs_count].description = do_prepare();
  test_funcs_count++;
}

void print_list(void) {
  for (int i = 0; i < test_funcs_count; i++) {
    // get tested function.
    printf("test: %s\n", test_entries[i].name);
  }
}

//////////////////////////////////////////////////

#define N 10

void print_usage(char *self) {
  printf("Usage: %s, invoke simple fuzzing test tool.\n", self);
  printf("  -h show help information.\n");
  printf("  -l show list of test functions.\n");
  printf("  -f <index> function index for test.\n");
  printf("  -r <Hex data> Random value(8bytes), such as 0x77ff or 77ff.\n");
  printf("     If set function name, then this random is used for this "
         "function invocation.\n");
  printf("     If not set function name, then this random is used as start for "
         "testing.\n");
  printf("  -c <count> run count of one function, default is %d\n", N);
  printf("  -n <number> run count of one function, default is %d\n", N);
}

static const struct option opts[] = {{"func", required_argument, NULL, 'f'},
                                     {"random", required_argument, NULL, 'r'},
                                     {"count", required_argument, NULL, 'c'},
                                     {"number", required_argument, NULL, 'n'},
                                     {"list", no_argument, NULL, 'l'},
                                     {"help", no_argument, NULL, 'h'},
                                     {0, 0, 0, 0}};

/**
 * Kill the process when the test times out.
 * 10 seconds by default
 */
static void *thread_timer(void *arg) {
  uint64_t *temp_time = arg;
  uint64_t time = 10 * 1000000;
  if (*temp_time != 0) {
    time = *temp_time * 1000;
  }
  fuz_print_log("fuzzing thread timer start %ld.\n", time);
  usleep(time);
  fuz_print_log("fuzzing thread timer time out kill process.\n");
  zx_task_kill(zx_process_self());
  return NULL;
}

/**
 * Main entry: parse arguments of command, execute task.
 */
int main(int argc, char *argv[]) {
  int rlt = 0;
  int opt;

  unsigned long run_count = N;
  uint32_t run_no = 0;

  uint32_t function_inx = 0;
  bool set_func_inx = false;

  fuz_seed_t start_random = 0;
  bool set_start_random = false;
  int thread_ret = 0;
  pthread_t threadid;

  // parse the args
  while ((opt = getopt_long(argc, argv, "hln:f:r:c:", opts, NULL)) != -1) {
    switch (opt) {
    case 'h':
      print_usage("fuzzer-tester");
      return 0;
    case 'l':
      print_list();
      return 0;
    case 'f':
      set_func_inx = true;
      function_inx = strtoul(optarg, NULL, 10);
      break;
    case 'c':
      run_count = strtoul(optarg, NULL, 10);
      break;
    case 'n':
      run_no = strtoul(optarg, NULL, 10);
      break;
    case 'r':
      set_start_random = true;
      start_random = strtoul(optarg, NULL, 16);
      break;
    default:
      print_usage("simple-tester");
      return 1;
    }
  }

  if (!set_func_inx || !set_start_random) {
    print_usage("simple-tester");
    rlt = -1;
    goto end;
  }

  if (function_inx >= (uint32_t)test_funcs_count) {
    rlt = -1;
    fuz_print_log("Invalid argument: function index too big.\n");
    goto end;
  }

  rlt = fuz_init_random(start_random);
  if (rlt == -1) {
    fuz_print_log("Cannot start random\n");
    goto end;
  }

  fuz_print_log("\n-------------------no:%ld\n@start fuzzing:%s\n", run_no,
                test_entries[function_inx].name);
  function_description *description = test_entries[function_inx].description;
  fuz_seed_t one_rand_seed = start_random;

  // start timer
  thread_ret =
      pthread_create(&threadid, NULL, thread_timer, &(description->time_out));
  if (thread_ret != 0) {
    fuz_print_log("create pthread timer error!\n");
    exit(1);
  }
  description->timer_threadid = threadid;

  for (unsigned long i = 0; i < run_count; i++) {
    fuz_do_test(one_rand_seed, description);
  }
  if (!description->block) {
    pthread_kill(threadid, SIGKILL);
  }

end:
  return rlt == 0 ? 0 : -1;
}
