#!/bin/sh
# The kit gate. `make check` runs it from the kit directory after the build.
# Needs tcc, geth evm and foundry cast. test/run.sh, test/deploy.sh,
# test/diff.sh and test/laws.sh exit 0 with no cases when evm is not on
# PATH (README.md, kit debt). Each step prints its own counts.
set -eu
fail=0

step() {
  echo "== $*"
  "$@" || { echo "FAIL $*"; fail=$((fail + 1)); }
}

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
[ "$fail" -eq 0 ]
