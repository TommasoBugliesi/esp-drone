#!/bin/sh
# Builds and runs the host tests with the system gcc (no ESP-IDF needed).
set -e
cd "$(dirname "$0")"
out=${TMPDIR:-/tmp}/espdrone-host-tests
mkdir -p "$out"
gcc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined \
    -include stubs/test_config.h -Istubs -I../../components/core/crazyflie/hal/interface \
    test_espnow_link.c -o "$out/test_espnow_link" -lm
"$out/test_espnow_link"
