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
# mock and the logs. Part 2 runs the 5 call scripts of test/evm.sh with mode
# 0 (C-K4d-8), test/run/basic.script on examples/contract.lang. Each step
# must give the result of `langc run`. At the end, the contract storage must
# equal the state of `langc run` and the mock balances must equal the sums of
# the pay and pull rows of `langc run`, mod 2^256 (bc, C-K4d-9). It needs evm
# (go-ethereum), bc, od, build/langc, build/asmtool and build/slottool. Run it
# from the kit root.
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

# sums: the mock storage rows (norm, no zero rows) of the pay and pull rows of
# tmp/run: `pull TOKEN FROM TO AMOUNT` moves AMOUNT from FROM to TO and `pay
# TOKEN TO AMOUNT` moves AMOUNT from the contract to TO, mod 2^256 (bc).
sums() {
  awk -v c="$receiver" '
    function mv(f, t, n) { sub(/u$/, "", n); s[f] = s[f] "-" n; s[t] = s[t] "+" n }
    $1 == "pull" { mv(substr(tolower($3), 3), substr(tolower($4), 3), $5) }
    $1 == "pay" { mv(c, substr(tolower($3), 3), $4) }
    END { for (a in s) print a, s[a] }' "$tmp/run" |
    while read -r _a _e; do
      echo "$(pad "$_a") $(echo "obase=16; m = 2^256; ((0$_e) % m + m) % m" | bc)"
    done | norm | awk '$2 != "0"'
}

# script PROG SCRIPT: part 2 for one call script, with the mock in mode 0 at
# the token. step() puts the storage of all accounts in tmp/raw, thus after
# each step tmp/raw gets only the contract storage again and tmp/m the mock
# storage, which the next step puts at the token.
script() {
  _prog=$1 _script=$2
  rows=$((rows + 1))
  if ! deploy "$_prog"; then
    echo "FAIL diff $_script: deploy: $(head -n 1 "$tmp/dump")"
    fail=$((fail + 1))
    return
  fi
  awk 'NF && $1 != "--"' "$_script" >"$tmp/calls"
  if ! build/langc run "$_prog" "$tmp/calls" >"$tmp/run" 2>"$tmp/err"; then
    echo "FAIL diff $_script: langc run: $(head -n 1 "$tmp/err")"
    fail=$((fail + 1))
    return
  fi
  : >"$tmp/m"
  _calls=$(awk 'END { print NR }' "$tmp/calls")
  _k=0
  while [ "$_k" -lt "$_calls" ]; do
    _k=$((_k + 1))
    set -- $(awk -v k="$_k" 'NR == k' "$tmp/calls")
    _now=$1 _caller=$2 _name=$3
    shift 3
    alloc=",\"0x$token\":{\"balance\":\"0x0\",\"code\":\"0x$(cat "$tmp/mock")\",\"storage\":{$(
      awk '{ printf "%s\"0x%s\":\"0x%s\"", (NR > 1 ? "," : ""), $1, $2 }' "$tmp/m")}}"
    _input=$(input "$_prog" "$_name" "$@")
    if ! step "$_now" "$_caller" "$_input"; then
      echo "FAIL diff $_script:$_k $_name: evm: $(head -n 1 "$tmp/dump")"
      fail=$((fail + 1))
      return
    fi
    _want=$(awk -v k="$_k" '$1 == k { print $3 }' "$tmp/run")
    [ "$_want" = = ] && _want="out $(word "$(awk -v k="$_k" '$1 == k { print $4 }' "$tmp/run")")"
    case $result in
      "out "*) _hex=${result#out }
        [ ${#_hex} -eq 64 ] && result="out $(hexnorm "$_hex")" ;;
    esac
    [ "$result" = "$_want" ] || { echo "FAIL diff $_script:$_k $_name: want $_want, got $result"; fail=$((fail + 1)); }
    acct "$receiver" >"$tmp/raw"
    acct "$token" >"$tmp/m"
  done
  norm <"$tmp/raw" >"$tmp/c"
  want "$(awk '$1 == "state" { sub(/^state /, ""); print }' "$tmp/run")"
  cmp -s "$tmp/want" "$tmp/c" || {
    echo "FAIL diff $_script: contract storage: want $(tr '\n' ' ' <"$tmp/want"), got $(tr '\n' ' ' <"$tmp/c")"
    fail=$((fail + 1))
  }
  sums >"$tmp/twant"
  norm <"$tmp/m" >"$tmp/tgot"
  cmp -s "$tmp/twant" "$tmp/tgot" || {
    echo "FAIL diff $_script: mock balances: want $(tr '\n' ' ' <"$tmp/twant"), got $(tr '\n' ' ' <"$tmp/tgot")"
    fail=$((fail + 1))
  }
}

# Part 2: the 5 call scripts of test/evm.sh with mode 0.
script examples/contract.lang test/run/basic.script
script examples/map.lang test/run/map.script
script examples/residuals.lang test/run/residuals.script
script examples/events.lang test/run/events.script
script examples/lists.lang test/run/lists.script

rm -rf "$tmp"
echo "diff gate: $rows rows, $fail failures"
[ "$fail" -eq 0 ]
