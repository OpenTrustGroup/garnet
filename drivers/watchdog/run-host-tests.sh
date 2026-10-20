#!/usr/bin/env bash
set -euo pipefail
wdt_test_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
wdt_test_out=${1:-$(mktemp -d /tmp/coimage-wdt-tests.XXXXXX)}
mkdir -p "$wdt_test_out"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie \
  "$wdt_test_root/drivers/watchdog/supervision-test.c" -o "$wdt_test_out/supervision"
c++ -std=c++14 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie \
  -I"$wdt_test_root/../zircon/system/public" \
  "$wdt_test_root/lib/machina/watchdog_policy-test.cc" -o "$wdt_test_out/policy"
"$wdt_test_out/supervision"
"$wdt_test_out/policy"
