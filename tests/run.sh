#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bin=$(mktemp /tmp/vvvf-tests.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=undefined,address \
  -Iinclude tests/test_control.c src/control/control.c src/control/programmed.c src/profiles/presets.c src/control/profiles.c src/control/voltage.c src/control/modulator.c -lm -o "$bin"
# LeakSanitizer cannot run under the workspace ptrace supervisor; no heap is used.
ASAN_OPTIONS=detect_leaks=0 "$bin"

python3 tests/test_sources.py

cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=undefined,address \
  -Iinclude tests/test_amplitude.c src/control/control.c src/control/programmed.c src/profiles/presets.c src/control/profiles.c src/control/voltage.c src/control/modulator.c -lm -o "$bin"
ASAN_OPTIONS=detect_leaks=0 "$bin"

cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=undefined,address \
  -Iinclude tests/test_profiles.c src/control/control.c src/profiles/presets.c src/control/profiles.c src/control/voltage.c -lm -o "$bin"
ASAN_OPTIONS=detect_leaks=0 "$bin"
