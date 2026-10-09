#!/bin/sh
# The kit gate. `make check` runs it from the kit directory after the build.
# Needs tcc, geth evm and foundry cast. Each step prints its own counts.
# When evm is not on PATH, test/run.sh, test/deploy.sh, test/diff.sh and
# test/laws.sh run no case: each one prints `skip: N cases (evm not on
# PATH)` and adds the line to build/test/skips. The gate empties that file
# first and prints the skip sum after its summary line. A skip is not a
# pass and not a fail.
set -eu
fail=0

step() {
  echo "== $*"
  "$@" || { echo "FAIL $*"; fail=$((fail + 1)); }
}

mkdir -p build/test
: > build/test/skips

step sh test/parse.sh
step sh test/evm.sh
step sh test/check.sh
step sh test/table.sh
step sh test/eval.sh
step sh test/build.sh
step sh test/run.sh
step sh test/deploy.sh
step sh test/diff.sh
step sh test/laws.sh

if rg -n '\x{2013}|\x{2014}' . >/dev/null; then
  echo "FAIL: an em-dash or en-dash is in the kit"
  fail=$((fail + 1))
fi
echo "gate: $fail failures"
awk '{ n += $2 } END { printf "gate: skip sum %d cases%s\n", n, (n > 0 ? " (evm not on PATH)" : "") }' build/test/skips
[ "$fail" -eq 0 ]
