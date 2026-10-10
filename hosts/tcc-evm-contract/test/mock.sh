#!/bin/sh
# The mock ERC-20 gate (slice K4d, C-K4d-3). `build/asmtool mock` gives the
# runtime of the mock token (its comment in test/asmtool.c gives the modes).
# Each row calls the mock once with `evm run`: holder a has 100, holder b
# has 5 and slot 0 has the mode. The row checks the result, the storage
# after the call and the logs. The last row calls an address with no code:
# the call succeeds with empty data (EVM behavior; the CALL lowering decides
# what the contract does with it). It needs evm (go-ethereum), od and
# build/asmtool. Run it from the kit root.
set -u
fail=0
rows=0
tmp=${TMPDIR:-/tmp}/langc-mock.$$
mkdir -p "$tmp"
. test/evmchain.sh

a=00000000000000000000000000000000000000a1
b=00000000000000000000000000000000000000b2
c=00000000000000000000000000000000000000c3
one=$(pad 1)
zero=$(pad 0)
topic=$(keccak "$(printf '%s' 'Transfer(address,address,uint256)' | od -An -tx1 | tr -d ' \n')")
build/asmtool mock >"$tmp/mock" || { echo "FAIL mock: asmtool mock"; exit 1; }

# tin TO AMOUNT, fin FROM TO AMOUNT: the call data of transfer and transferFrom.
tin() { echo "a9059cbb$(pad "$1")$(pad "$2")"; }
fin() { echo "23b872dd$(pad "$1")$(pad "$2")$(pad "$3")"; }

# lg FROM TO AMOUNT: the logs line of one Transfer LOG3.
lg() { echo "$topic,$(pad "$1"),$(pad "$2") $(pad "$3")"; }

# check NAME CALLER INPUT RESULT LOG: one step on tmp/code and tmp/raw. The
# storage after the step must equal tmp/want, the logs must equal LOG (one
# line, or empty for no LOG).
check() {
  rows=$((rows + 1))
  if ! step 0 "0x$2" "$3"; then
    echo "FAIL mock $1: evm: $(head -n 1 "$tmp/dump")"
    fail=$((fail + 1))
    return
  fi
  [ "$result" = "$4" ] || { echo "FAIL mock $1: want $4, got $result"; fail=$((fail + 1)); }
  cmp -s "$tmp/want" "$tmp/got" || {
    echo "FAIL mock $1: storage: want $(tr '\n' ' ' <"$tmp/want"), got $(tr '\n' ' ' <"$tmp/got")"
    fail=$((fail + 1))
  }
  logs "0x$2" "$3" || { echo "FAIL mock $1: evm --debug"; fail=$((fail + 1)); return; }
  _log=$(cat "$tmp/gotlogs")
  [ "$_log" = "$5" ] || { echo "FAIL mock $1: logs: want ${5:--}, got ${_log:--}"; fail=$((fail + 1)); }
}

# row NAME MODE CALLER INPUT RESULT A B LOG: a call of the mock with mode
# MODE. A and B are the balances of a and b after the call (hexnorm).
row() {
  cp "$tmp/mock" "$tmp/code"
  { [ "$2" = 0 ] || echo "$zero $(pad "$2")"; echo "$(pad $a) $(pad 64)"; echo "$(pad $b) $(pad 5)"; } >"$tmp/raw"
  { [ "$2" = 0 ] || echo "$zero $2"; echo "$(pad $a) $6"; echo "$(pad $b) $7"; } | sort >"$tmp/want"
  check "$1" "$3" "$4" "$5" "$8"
}

row transfer-true 0 $a "$(tin $b 1e)" "out $one" 46 23 "$(lg $a $b 1e)"
row transfer-false 1 $a "$(tin $b 1e)" "out $zero" 64 5 ""
row transfer-empty 2 $a "$(tin $b 1e)" ok 46 23 "$(lg $a $b 1e)"
row transfer-revert 3 $a "$(tin $b 1e)" revert 64 5 ""
row transfer-mode-9 9 $a "$(tin $b 1e)" revert 64 5 ""
row from-true 0 $c "$(fin $a $b 1e)" "out $one" 46 23 "$(lg $a $b 1e)"
row from-false 1 $c "$(fin $a $b 1e)" "out $zero" 64 5 ""
row from-empty 2 $c "$(fin $a $b 1e)" ok 46 23 "$(lg $a $b 1e)"
row from-revert 3 $c "$(fin $a $b 1e)" revert 64 5 ""
row self 0 $a "$(tin $a 1e)" "out $one" 64 5 "$(lg $a $a 1e)"
row wrap 0 $a "$(tin $b c8)" "out $one" "$(printf '%62s' '' | tr ' ' f)9c" cd "$(lg $a $b c8)"
row selector 0 $a "095ea7b3$(pad $b)$(pad 1e)" revert 64 5 ""

: >"$tmp/code"
: >"$tmp/raw"
: >"$tmp/want"
check no-code $a "$(tin $b 1e)" ok ""

rm -rf "$tmp"
echo "mock gate: $rows rows, $fail failures"
[ "$fail" -eq 0 ]
