#!/bin/sh
# The differential gate (slice K4d, C-K4d-8), part 1: the failure table of
# the CALL of a pay or a pull (C-K4d-6, C-K4d-7, C-K4-14). The prestate puts
# the `asmtool mock` runtime at the token address 0x..ee of
# examples/contract.lang, with the mode in slot 0 and each balance at slot
# ADDR (C-K4d-9: the balances start at 0 and wrap mod 2^256). Each row does
# one step of deposit (a pull) or withdraw (a pay) with mock mode 0 (true),
# 1 (false), 2 (empty return data), 3 (the mock reverts) or with no code at
# the token. The row checks the result, the storage of the contract (the
# state of `langc run` on ok, the old storage on revert), the storage of the
# mock and the logs. The run of the 5 call scripts with mode 0 (C-K4d-8) is
# not here yet. It needs evm (go-ethereum), bc, od, build/langc,
# build/asmtool and build/slottool. Run it from the kit root.
set -u
fail=0
rows=0
tmp=${TMPDIR:-/tmp}/langc-diff.$$
mkdir -p "$tmp"
. test/evmchain.sh

prog=examples/contract.lang
token=00000000000000000000000000000000000000ee
user=00000000000000000000000000000000000000bb
neg=$(printf '%62s' '' | tr ' ' f)e2
topic=$(keccak "$(printf '%s' 'Transfer(address,address,uint256)' | od -An -tx1 | tr -d ' \n')")
build/asmtool mock >"$tmp/mock" || { echo "FAIL diff: asmtool mock"; exit 1; }

# lg FROM TO AMOUNT: the logs line of one Transfer LOG3 of the mock.
lg() { echo "$topic,$(pad "$1"),$(pad "$2") $(pad "$3")"; }

# acct ADDR: the storage rows `SLOT VALUE` of the account ADDR in tmp/dump.
acct() {
  awk -F'"' -v a="0x$1" 'NF == 3 && length($2) == 42 { p = (tolower($2) == a); next }
    p && NF >= 5 && length($2) == 66 && substr($2, 1, 2) == "0x" { print substr($2, 3), $4 }' "$tmp/dump"
}

# norm: the rows `SLOT VALUE` of stdin with VALUE as hexnorm, sorted.
norm() { awk '{ v = tolower($2); sub(/^0+/, "", v); print $1, (v == "" ? "0" : v) }' | sort; }

# bal MODE ROWS: the storage rows of the mock: MODE at slot 0 (no row for 0
# or none) and each ADDR:VALUE of ROWS at slot ADDR.
bal() {
  case $1 in 0|none) ;; *) echo "$(pad 0) $(pad "$1")" ;; esac
  for _r in $2; do echo "$(pad "${_r%:*}") $(pad "${_r#*:}")"; done
}

# row NAME MODE PRE BAL CALLS RESULT BAL2 LOG: the step of the last call of
# CALLS (call lines, `;` between them) on the contract storage in the file
# PRE and the mock storage `bal MODE BAL` (MODE none: no code at the token).
# RESULT is ok or revert. After the step, the contract storage must equal
# the state of `langc run` for CALLS (ok) or PRE (revert), the mock storage
# must equal `bal MODE BAL2` and the logs must equal LOG.
row() {
  rows=$((rows + 1))
  cp "$3" "$tmp/raw"
  alloc=
  [ "$2" = none ] ||
    alloc=",\"0x$token\":{\"balance\":\"0x0\",\"code\":\"0x$(cat "$tmp/mock")\",\"storage\":{$(bal "$2" "$4" |
      awk '{ printf "%s\"0x%s\":\"0x%s\"", (NR > 1 ? "," : ""), $1, $2 }')}}"
  printf '%s\n' "$5" | tr ';' '\n' >"$tmp/calls"
  if [ "$6" = ok ]; then
    build/langc run "$prog" "$tmp/calls" >"$tmp/run" 2>"$tmp/err" ||
      { echo "FAIL diff $1: langc run: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)); return; }
    want "$(awk '$1 == "state" { sub(/^state /, ""); print }' "$tmp/run")"
  else
    norm <"$3" >"$tmp/want"
  fi
  set -- "$@" $(awk 'END { print }' "$tmp/calls")
  _input=$(input "$prog" "${11}" "${12}")
  if ! step "$9" "${10}" "$_input"; then
    echo "FAIL diff $1: evm: $(head -n 1 "$tmp/dump")"
    fail=$((fail + 1))
    return
  fi
  [ "$result" = "$6" ] || { echo "FAIL diff $1: want $6, got $result"; fail=$((fail + 1)); }
  acct "$receiver" | norm >"$tmp/got"
  cmp -s "$tmp/want" "$tmp/got" || {
    echo "FAIL diff $1: contract storage: want $(tr '\n' ' ' <"$tmp/want"), got $(tr '\n' ' ' <"$tmp/got")"
    fail=$((fail + 1))
  }
  bal "$2" "$7" | norm >"$tmp/twant"
  acct "$token" | norm >"$tmp/tgot"
  cmp -s "$tmp/twant" "$tmp/tgot" || {
    echo "FAIL diff $1: mock storage: want $(tr '\n' ' ' <"$tmp/twant"), got $(tr '\n' ' ' <"$tmp/tgot")"
    fail=$((fail + 1))
  }
  logs "${10}" "$_input" || { echo "FAIL diff $1: evm --debug"; fail=$((fail + 1)); return; }
  _log=$(cat "$tmp/gotlogs")
  [ "$_log" = "$8" ] || { echo "FAIL diff $1: logs: want ${8:--}, got ${_log:--}"; fail=$((fail + 1)); }
}

if ! deploy "$prog"; then
  echo "FAIL diff $prog: deploy: $(head -n 1 "$tmp/dump")"
  exit 1
fi
cp "$tmp/raw" "$tmp/s0"
dep="100 0x$user deposit 30u"
wd="$dep;101 0x$user withdraw 30u"
moved="bb:$neg cc:1e"
paid="$moved $receiver:$neg aa:1e"

# deposit: transferFrom(caller, vault, amount) from the contract.
row pull-ok 0 "$tmp/s0" "" "$dep" ok "$moved" "$(lg bb cc 1e)"
acct "$receiver" >"$tmp/s1"
row pull-false 1 "$tmp/s0" "" "$dep" revert "" ""
row pull-empty 2 "$tmp/s0" "" "$dep" ok "$moved" "$(lg bb cc 1e)"
row pull-revert 3 "$tmp/s0" "" "$dep" revert "" ""
row pull-no-code none "$tmp/s0" "" "$dep" revert "" ""
# withdraw after the deposit: transfer(owner, amount); the contract is the sender.
row pay-ok 0 "$tmp/s1" "$moved" "$wd" ok "$paid" "$(lg $receiver aa 1e)"
row pay-false 1 "$tmp/s1" "$moved" "$wd" revert "$moved" ""
row pay-empty 2 "$tmp/s1" "$moved" "$wd" ok "$paid" "$(lg $receiver aa 1e)"
row pay-revert 3 "$tmp/s1" "$moved" "$wd" revert "$moved" ""
row pay-no-code none "$tmp/s1" "" "$wd" revert "" ""

rm -rf "$tmp"
echo "diff gate: $rows rows, $fail failures"
[ "$fail" -eq 0 ]
