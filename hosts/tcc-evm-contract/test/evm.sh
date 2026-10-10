#!/bin/sh
# The chain gate (slice K4c, O-s11-1). For each call script below, deploy the
# program with `evm run --create`, then do one `evm run --prestate --dump`
# step for each call of the script. After each step, the storage must equal
# the state that `langc run` prints for the calls up to that step, and the
# result must agree with the result of the call: ok, revert (`none`) or trap
# (the Trap() selector). A view call must return the 32-byte word of the view
# value (slice K4c, C-K4-10). The logs of the step (`evm run --debug`) must
# equal the events that `langc run` prints for the call: one LOG1 for each
# event, in order (slice K4c, C-K4-13). The TIMESTAMP of the block is NOW (the
# prestate timestamp) and the sender is CALLER. It needs evm (go-ethereum), bc, od and build/slottool.
# Run it from the kit root.
set -u
fail=0
steps=0
tmp=${TMPDIR:-/tmp}/langc-evm.$$
mkdir -p "$tmp"
. test/evmchain.sh

# wantlogs PROG K: one line for each event that `langc run` prints for call K
# (tmp/run): topic 0 (from the `langc abi` event line, C-K4-16) and the data
# words. test/abi.expect holds the cast check of the topics.
wantlogs() {
  : >"$tmp/wantlogs"
  build/langc abi "$1" >"$tmp/abi" || : >"$tmp/abi"
  awk -v k="$2" '/^[^ ]/ { p = ($1 == k); next } p && $1 == "event"' "$tmp/run" >"$tmp/events"
  while read -r _ev _evname _evargs; do
    _topic=$(awk -v n="$_evname" '$3 == "event" && index($2, n "(") == 1 { print substr($1, 3) }' "$tmp/abi")
    _data=
    for _a in $_evargs; do _data=$_data$(pad "$(word "$_a")"); done
    echo "$_topic ${_data:--}" >>"$tmp/wantlogs"
  done <"$tmp/events"
}

# logs CALLER INPUT: the logs of the step on tmp/pre.json (the LOGS block of
# `evm run --debug`), one line for each LOG: the topics and the data.
logs() {
  evm run --debug --prestate "$tmp/pre.json" --receiver "0x$receiver" --sender "$1" --input "$2" \
    >"$tmp/debug" 2>&1 </dev/null || return 1
  awk '
    /^#### LOGS ####/ { p = 1; next }
    /^####/ { p = 0 }
    !p { next }
    /^LOG[0-4]:/ { if (n) print t " " (d == "" ? "-" : d); n = 1; t = ""; d = ""; next }
    length($2) == 64 { t = t (t == "" ? "" : ",") $2; next }
    { for (i = 2; i <= NF && $i ~ /^[0-9a-f][0-9a-f]$/; i++) d = d $i }
    END { if (n) print t " " (d == "" ? "-" : d) }' "$tmp/debug" >"$tmp/gotlogs"
}

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
    _value=$(awk -v k="$_k" '$1 == k { print $4 }' "$tmp/run")
    case $(awk -v k="$_k" '$1 == k { print $3 }' "$tmp/run") in
      ok) _want=ok ;;
      revert) _want=revert ;;
      trap) _want=trap ;;
      =) _want="out $(word "$_value")" ;;
      *) _want=unknown ;;
    esac
    want "$(awk '$1 == "state" { sub(/^state /, ""); print }' "$tmp/run")"
    _input=$(input "$_prog" "$_name" "$@")
    if ! step "$_now" "$_caller" "$_input"; then
      echo "FAIL evm $_script:$_k $_name: evm: $(head -n 1 "$tmp/dump")"
      fail=$((fail + 1))
      return
    fi
    case $result in
      "out "*) _hex=${result#out }
        [ ${#_hex} -eq 64 ] && result="out $(hexnorm "$_hex")" ;;
    esac
    if [ "$result" != "$_want" ] || ! cmp -s "$tmp/want" "$tmp/got"; then
      echo "FAIL evm $_script:$_k $_name: want $_want, got $result"
      diff "$tmp/want" "$tmp/got" | head -n 6
      fail=$((fail + 1))
    fi
    wantlogs "$_prog" "$_k"
    if ! logs "$_caller" "$_input" || ! cmp -s "$tmp/wantlogs" "$tmp/gotlogs"; then
      echo "FAIL evm $_script:$_k $_name: logs"
      diff "$tmp/wantlogs" "$tmp/gotlogs" | head -n 6
      fail=$((fail + 1))
    fi
  done
}

# The `langc abi` lines (C-K4-16) of each chain program and of one refused
# program equal test/abi.expect. Each selector and topic there equals the
# output of `cast sig` or `cast sig-event` (foundry cast 0.3.0, K4c s6). The
# gate does not run cast.
for _p in examples/contract.lang examples/map.lang examples/residuals.lang examples/events.lang \
  test/lower/emit.lang; do
  echo "# $_p"
  build/langc abi "$_p" 2>&1
  echo "exit $?"
done >"$tmp/abi.got"
# The JSON ABI (Q-K4-3) of the two programs with events. `cast interface`
# reads each array (K4c s8). The gate does not run cast.
for _p in examples/contract.lang examples/events.lang; do
  echo "# $_p --json"
  build/langc abi "$_p" --json 2>&1
  echo "exit $?"
done >>"$tmp/abi.got"
if ! cmp -s test/abi.expect "$tmp/abi.got"; then
  echo "FAIL evm abi: the langc abi lines differ from test/abi.expect"
  diff test/abi.expect "$tmp/abi.got" | head -n 6
  fail=$((fail + 1))
fi

chain examples/contract.lang test/run/basic.script
chain examples/map.lang test/run/map.script
chain examples/residuals.lang test/run/residuals.script
chain examples/events.lang test/run/events.script
chain examples/lists.lang test/run/lists.script

rm -rf "$tmp"
echo "evm steps: $steps checked"
echo "evm gate: $fail failures"
[ "$fail" -eq 0 ]
