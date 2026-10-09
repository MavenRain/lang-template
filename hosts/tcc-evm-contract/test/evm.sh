#!/bin/sh
# The chain gate (slice K4c, O-s11-1). For each call script below, deploy the
# program with `evm run --create`, then do one `evm run --prestate --dump`
# step for each call of the script. After each step, the storage must equal
# the state that `langc run` prints for the calls up to that step, and the
# result must agree with the result of the call: ok, revert (`none`) or trap
# (the Trap() selector). A view call reverts: slice K4b has no views in the
# dispatcher. The TIMESTAMP of the block is NOW (the prestate timestamp) and
# the sender is CALLER. It needs evm (go-ethereum), bc, od and build/slottool.
# Run it from the kit root.
set -u
fail=0
steps=0
tmp=${TMPDIR:-/tmp}/langc-evm.$$
mkdir -p "$tmp"
. test/evmchain.sh

# chain PROG SCRIPT: deploy PROG, then one step for each call of SCRIPT.
chain() {
  _prog=$1 _script=$2
  if ! deploy "$_prog"; then
    echo "FAIL evm $_prog: deploy: $(head -n 1 "$tmp/dump")"
    fail=$((fail + 1))
    return
  fi
  awk 'NF && $1 != "--"' "$_script" >"$tmp/calls"
  _calls=$(awk 'END { print NR }' "$tmp/calls")
  _k=0
  while [ "$_k" -lt "$_calls" ]; do
    _k=$((_k + 1))
    awk -v k="$_k" 'NR <= k' "$tmp/calls" >"$tmp/prefix"
    set -- $(awk -v k="$_k" 'NR == k' "$tmp/calls")
    _now=$1 _caller=$2 _name=$3
    shift 3
    steps=$((steps + 1))
    if ! build/langc run "$_prog" "$tmp/prefix" >"$tmp/run" 2>"$tmp/err"; then
      echo "FAIL evm $_script:$_k: langc run: $(head -n 1 "$tmp/err")"
      fail=$((fail + 1))
      return
    fi
    case $(awk -v k="$_k" '$1 == k { print $3 }' "$tmp/run") in
      ok) _want=ok ;;
      revert) _want=revert ;;
      trap) _want=trap ;;
      =) _want=revert ;;
      *) _want=unknown ;;
    esac
    want "$(awk '$1 == "state" { sub(/^state /, ""); print }' "$tmp/run")"
    if ! step "$_now" "$_caller" "$(input "$_prog" "$_name" "$@")"; then
      echo "FAIL evm $_script:$_k $_name: evm: $(head -n 1 "$tmp/dump")"
      fail=$((fail + 1))
      return
    fi
    if [ "$result" != "$_want" ] || ! cmp -s "$tmp/want" "$tmp/got"; then
      echo "FAIL evm $_script:$_k $_name: want $_want, got $result"
      diff "$tmp/want" "$tmp/got" | head -n 6
      fail=$((fail + 1))
    fi
  done
}

chain examples/contract.lang test/run/basic.script
chain examples/map.lang test/run/map.script
chain examples/residuals.lang test/run/residuals.script

rm -rf "$tmp"
echo "evm steps: $steps checked"
echo "evm gate: $fail failures"
[ "$fail" -eq 0 ]
