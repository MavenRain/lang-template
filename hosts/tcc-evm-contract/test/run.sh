#!/bin/sh
# The run gate (kit slice K3b). `make check` runs it from the kit directory
# after test/gate.sh.
set -eu
fail=0
runs=0
tmp=${TMPDIR:-/tmp}/langc-run.$$
mkdir -p "$tmp"

# For each test/run/NAME.script, `langc run examples/contract.lang` must exit 0
# and print test/run/NAME.out byte for byte.
for script in test/run/*.script; do
  status=0
  build/langc run examples/contract.lang "$script" >"$tmp/out" 2>"$tmp/err" || status=$?
  runs=$((runs + 1))
  if [ "$status" -ne 0 ] || ! cmp -s "${script%.script}.out" "$tmp/out"; then
    echo "FAIL run $script: exit $status: $(head -n 1 "$tmp/err")"
    fail=$((fail + 1))
  fi
done
echo "runs: $runs checked"

# run_refused STATUS CODE PROG LINE: `langc run PROG` on the one-line script
# LINE must exit STATUS and print `langc: CODE: ...` on stderr.
refused=0
run_refused() {
  printf '%s\n' "$4" >"$tmp/bad.script"
  status=0
  build/langc run "$3" "$tmp/bad.script" 2>"$tmp/err" >/dev/null || status=$?
  refused=$((refused + 1))
  case "$status:$(head -n 1 "$tmp/err")" in
    "$1:langc: $2: "*) ;;
    *) echo "FAIL run '$4': want exit $1 and $2, got exit $status: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
  esac
}
caller=0x00000000000000000000000000000000000000bb
run_refused 1 RUN_SCRIPT examples/contract.lang "1 0xbb deposit 5u"
run_refused 1 RUN_SCRIPT examples/contract.lang "x $caller deposit 5u"
run_refused 2 EVAL_ARGS examples/contract.lang "1 $caller deposit 5"
run_refused 2 EVAL_ARGS examples/contract.lang "1 $caller deposit 5u 6u"
run_refused 1 EVAL_ENTRY examples/contract.lang "1 $caller checkpoint init 7u"
run_refused 1 EVAL_ENTRY examples/contract.lang "1 $caller nope"
run_refused 1 RUN_INIT examples/entries.lang "1 $caller addPrice 1 2"
echo "run refusals: $refused checked"

status=0
build/langc run examples/contract.lang 2>/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL run usage: want exit 2, got $status"; fail=$((fail + 1)); }
rm -rf "$tmp"
echo "run gate: $fail failures"
[ "$fail" -eq 0 ]
