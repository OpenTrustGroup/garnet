#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <uapi/err.h>
#include <zircon/types.h>

#include "fuz_common.h"
#include "main/fuz_tester.h"

//////////////////////////////////////////////////
// global test functions list.
#define MAX_TEST_FUNCS 1024
typedef struct {
  function_description *description;
  const char *name;
  int original_index;
} test_entry_t;
test_entry_t test_entries[MAX_TEST_FUNCS];
int test_funcs_count = 0;

test_entry_t filter_test_entries[MAX_TEST_FUNCS];
int filter_test_funcs_count = 0;

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

//////////////////////////////////////////////////
// Pattern
// Returns true if the wildcard pattern matches the string.  The
// first ':' or '\0' character in pattern marks the end of it.
//
// This recursive algorithm isn't very efficient, but is clear and
// works well enough for matching test names, which are short.

bool PatternMatchesString(const char *pattern, const char *str) {
  switch (*pattern) {
  case '\0':
  case ':': // Either ':' or '\0' marks the end of the pattern.
    return *str == '\0';
  case '?': // Matches any single character.
    return *str != '\0' && PatternMatchesString(pattern + 1, str + 1);
  case '*': // Matches any string (possibly empty) of characters.
    return (*str != '\0' && PatternMatchesString(pattern, str + 1)) ||
           PatternMatchesString(pattern + 1, str);
  default: // Non-special character.  Matches itself.
    return *pattern == *str && PatternMatchesString(pattern + 1, str + 1);
  }
}

bool MatchesFilter(const char *filter, const char *name) {
  const char *cur_pattern = filter;
  for (;;) {
    if (PatternMatchesString(cur_pattern, name)) {
      return true;
    }

    // Finds the next pattern in the filter.
    cur_pattern = strchr(cur_pattern, ':');

    // Returns if no more pattern can be found.
    if (cur_pattern == NULL) {
      return false;
    }

    // Skips the pattern separater (the ':' character).
    cur_pattern++;
  }
}

// Returns true iff the user-specified filter matches the test case
// name and the test name.
bool FilterMatchesTest(const char *filter, const char *test_name) {
  char *f = strdup(filter);
  char *dash = strchr(f, '-');
  char *positive;
  char *negative;
  if (dash == NULL) {
    positive = f;
    negative = "";
  } else {
    positive = f;
    *dash = '\0';
    negative = dash + 1;
    if (strlen(positive) == 0) {
      // Treat '-test1' as the same as '*-test1'
      positive = "*";
    }
  }

  // A filter is a colon-separated list of patterns.  It matches a
  // test if any pattern in it matches the test.
  bool rlt = (MatchesFilter(positive, test_name) &&
              !MatchesFilter(negative, test_name));

  free(f);

  return rlt;
}

//////////////////////////////////////////////////

#define N 10

void print_usage(char *self) {
  printf("Usage: %s, invoke simple fuzzing test tool.\n", self);
  printf("  -h show help information.\n");
  printf("  -m <mode> list/count/round/random/replay.\n");
  printf(
      "  -f <name> function name for test, pattern is same with googletest.\n");
  printf("  -r <Hex data> Random value(8bytes), such as 0x77ff or 77ff.\n");
  printf(
      "     If set function name and replay, then this random is used for this "
      "function invocation.\n");
  printf(
      "     If set function name, then this random is used for seed of invoke "
      "this function.\n");
  printf("     If not set function name, then this random is used as start for "
         "testing.\n");
  printf("  -c <count> run count of one function, default is %d\n", N);
}

static const struct option opts[] = {{"func", required_argument, NULL, 'f'},
                                     {"mode", no_argument, NULL, 'm'},
                                     {"random", required_argument, NULL, 'r'},
                                     {"count", required_argument, NULL, 'c'},
                                     {"help", no_argument, NULL, 'h'},
                                     {0, 0, 0, 0}};

/**
 * Main entry: parse arguments of command, execute task.
 */
int main(int argc, char *argv[]) {
  int rlt = 0;
  int opt;

  char *test_func_name = NULL;
  unsigned long run_count = N;
  bool set_start_random = false;
  fuz_seed_t start_random = 0;
  bool test_fail = false;
  char *test_mode = NULL;

  // parse the args
  while ((opt = getopt_long(argc, argv, "hf:r:c:m:", opts, NULL)) != -1) {
    switch (opt) {
    case 'h':
      print_usage("fuzzer-main");
      return 0;
    case 'f':
      test_func_name = optarg;
      break;
    case 'm':
      test_mode = optarg;
      break;
    case 'c':
      run_count = strtoul(optarg, NULL, 10);
      break;
    case 'r':
      set_start_random = true;
      start_random = strtoul(optarg, NULL, 16);
      break;
    default:
      print_usage("simple-fuzzer");
      return 1;
    }
  }

  // get filtered test entries.
  for (int i = 0; i < test_funcs_count; i++) {
    if (test_func_name) {
      if (FilterMatchesTest(test_func_name, test_entries[i].name) == false) {
        continue;
      }
    }
    filter_test_entries[filter_test_funcs_count] = test_entries[i];
    filter_test_entries[filter_test_funcs_count].original_index = i;
    filter_test_funcs_count++;
  }

  // Start test random seed.
  if (strcmp(test_mode, "list") == 0) {
    for (int i = 0; i < filter_test_funcs_count; i++) {
      printf("test: %s\n", filter_test_entries[i].name);
    }
    printf("sum: %d\n", filter_test_funcs_count);
  } else if (strcmp(test_mode, "replay") == 0) {
    if (!set_start_random) {
      printf("On replay, should input random.\n");
      return 1;
    }

    if (!test_func_name) {
      printf("On replay, should input function name.\n");
      return 1;
    }

    for (int i = 0; i < filter_test_funcs_count; i++) {
      // get tested function description.
      rlt = fuz_start_fuzzing((fuz_seed_t)start_random, i,
                              filter_test_entries[i].original_index, 1);
      if (rlt != 0) {
        test_fail = true;
      }
      fuz_print_log("function:%s result:%d\n", filter_test_entries[i].name,
                    rlt);
      break;
    }
  } else if (strcmp(test_mode, "count") == 0) {
    // initialize random seed.
    fuz_seed_t all_random_seed;
    if (set_start_random) {
      all_random_seed = (fuz_seed_t)start_random;
    } else {
      all_random_seed = (fuz_seed_t)time(0);
    }

    rlt = fuz_init_random(all_random_seed);
    if (rlt == -1) {
      fuz_print_log("Cannot start random\n");
      test_fail = true;
      goto end;
    }

    fuz_print_log("Use start random seed for count mode:%#lx\n",
                  all_random_seed);

    // start test
    fuz_seed_t one_rand_seed = 0;
    for (int i = 0; i < filter_test_funcs_count; i++) {
      // get tested function description.

      for (uint32_t j = 0; j < run_count; j++) {
        // draw new random.
        fuz_draw_random(&one_rand_seed, sizeof(one_rand_seed));
        rlt = fuz_start_fuzzing(one_rand_seed, i * run_count + j,
                                filter_test_entries[i].original_index, 1);
        if (rlt != 0) {
          test_fail = true;
        }
        fuz_print_log("function:%s result:%d\n", filter_test_entries[i].name,
                      rlt);
      }
    }
  } else if (strcmp(test_mode, "round") == 0) { // round
    // initialize random seed.
    fuz_seed_t all_random_seed;
    if (set_start_random) {
      all_random_seed = (fuz_seed_t)start_random;
    } else {
      all_random_seed = (fuz_seed_t)time(0);
    }

    rlt = fuz_init_random(all_random_seed);
    if (rlt == -1) {
      fuz_print_log("Cannot start random\n");
      test_fail = true;
      goto end;
    }

    fuz_print_log("Use start random seed for round mode:%#lx\n",
                  all_random_seed);

    // start test
    fuz_seed_t one_rand_seed = 0;
    for (uint32_t j = 0; j < run_count; j++) {
      for (int i = 0; i < filter_test_funcs_count; i++) {
        // draw new random.
        fuz_draw_random(&one_rand_seed, sizeof(one_rand_seed));
        rlt = fuz_start_fuzzing(one_rand_seed, j * filter_test_funcs_count + i,
                                filter_test_entries[i].original_index, 1);
        if (rlt != 0) {
          test_fail = true;
        }
        fuz_print_log("function:%s result:%d\n", filter_test_entries[i].name,
                      rlt);
      }
    }
  } else if (strcmp(test_mode, "random") == 0) { // random
    // initialize random seed.
    fuz_seed_t all_random_seed;
    if (set_start_random) {
      all_random_seed = (fuz_seed_t)start_random;
    } else {
      all_random_seed = (fuz_seed_t)time(0);
    }

    rlt = fuz_init_random(all_random_seed);
    if (rlt == -1) {
      fuz_print_log("Cannot start random\n");
      test_fail = true;
      goto end;
    }

    fuz_print_log("Use start random seed for random mode:%#lx\n",
                  all_random_seed);

    // start test
    fuz_seed_t one_rand_seed = 0;
    for (uint32_t j = 0; j < run_count; j++) {
      unsigned int i = fuz_rnd_range(filter_test_funcs_count);

      // draw new random.
      fuz_draw_random(&one_rand_seed, sizeof(one_rand_seed));
      rlt = fuz_start_fuzzing(one_rand_seed, j,
                              filter_test_entries[i].original_index, 1);
      if (rlt != 0) {
        test_fail = true;
      }
      fuz_print_log("function:%s result:%d\n", filter_test_entries[i].name,
                    rlt);
    }
  } else {
    fuz_print_log("%s is unknown test mode.\n", test_mode);
  }

end:

  return test_fail ? -1 : 0;
}
