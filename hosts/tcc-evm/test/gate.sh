#!/bin/sh
# The kit gate. `make check` runs it from the kit directory after the build.
set -eu
fail=0
count=0
for prog in examples/*.lang; do
  out=$(build/langc check "$prog" 2>&1) || true
  count=$((count + 1))
  if [ "$out" != "ok" ]; then
    echo "FAIL $prog: $out"
    fail=$((fail + 1))
  fi
done
echo "examples: $count checked, $fail failed"

tmp=${TMPDIR:-/tmp}/langc-gate.$$
mkdir -p "$tmp"
awk 'BEGIN { printf "def deep : Nat := " } { } END {
  for (i = 0; i < 1100; i++) printf "("; printf "1"; for (i = 0; i < 1100; i++) printf ")"; print "" }' </dev/null >"$tmp/deep.lang"

# refusals DIR LABEL: each line of DIR/expect.txt is `FILE CODE`. `langc check`
# must exit 1 and print `langc: CODE: ...` on stderr.
refusals() {
  checked=0
  while read -r file code; do
    path="$1/$file"
    [ "$file" = "deep.lang" ] && path="$tmp/deep.lang"
    status=0
    build/langc check "$path" 2>"$tmp/err" >/dev/null || status=$?
    first=$(head -n 1 "$tmp/err")
    checked=$((checked + 1))
    case "$status:$first" in
      "1:langc: $code: "*) ;;
      *) echo "FAIL $file: want exit 1 and $code, got exit $status: $first"; fail=$((fail + 1)) ;;
    esac
  done <"$1/expect.txt"
  echo "$2 refusals: $checked checked"
}
refusals test/parse parse
refusals test/check check

# A refl refusal shows both normal forms.
build/langc check test/check/bad-refl.lang 2>"$tmp/err" >/dev/null || true
if ! rg -q -F 'the left side normalizes to 1201, the right side normalizes to 1200' "$tmp/err"; then
  echo "FAIL bad-refl.lang: the message does not show both normal forms"
  fail=$((fail + 1))
fi

# Each line of test/eval/expect.txt is `FILE NAME [ARGS] => OUTPUT`. `langc eval`
# must exit 0 and print OUTPUT. A trap also prints `langc: EVAL_OVERFLOW: NAME: ...`
# on stderr.
evals=0
while IFS= read -r line; do
  want=${line#* => }
  set -- ${line%% => *}
  file=$1
  name=$2
  shift 2
  status=0
  got=$(build/langc eval "examples/$file.lang" "$name" "$@" 2>"$tmp/err") || status=$?
  first=$(head -n 1 "$tmp/err")
  want_err=""
  [ "$want" = "trap" ] && want_err="langc: EVAL_OVERFLOW: $name: "
  evals=$((evals + 1))
  case "$status|$got|$first" in
    "0|$want|$want_err"*) ;;
    *) echo "FAIL eval $file $name: want $want, got exit $status: $got $first"; fail=$((fail + 1)) ;;
  esac
done <test/eval/expect.txt
echo "evals: $evals checked"

# eval_refused STATUS CODE NAME [ARGS]: `langc eval examples/entries.lang NAME
# ARGS` must exit STATUS and print `langc: CODE: NAME: ...` on stderr.
eval_refused() {
  want_status=$1
  code=$2
  shift 2
  status=0
  build/langc eval examples/entries.lang "$@" 2>"$tmp/err" >/dev/null || status=$?
  case "$status:$(head -n 1 "$tmp/err")" in
    "$want_status:langc: $code: $1: "*) ;;
    *) echo "FAIL eval $*: want exit $want_status and $code, got exit $status"; fail=$((fail + 1)) ;;
  esac
}
eval_refused 2 EVAL_ARGS addPrice 1
eval_refused 2 EVAL_ARGS addPrice x 1
eval_refused 2 EVAL_ARGS addPrice 18446744073709551616 1
eval_refused 2 EVAL_ARGS pick 2 4 9
eval_refused 1 EVAL_ENTRY nope

status=0
build/langc 2>/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL usage: want exit 2, got $status"; fail=$((fail + 1)); }
status=0
build/langc check "$tmp/missing.lang" 2>/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL missing file: want exit 2, got $status"; fail=$((fail + 1)); }

# The IR: each example lowers, with one func line for each entry.
irs=0
for f in examples/*.lang; do
  irs=$((irs + 1))
  status=0
  build/langc ir "$f" > "$tmp/ir.txt" 2> "$tmp/ir.err" || status=$?
  funcs=$(rg -c '^func ' "$tmp/ir.txt" || echo 0)
  entries=$(build/langc abi "$f" | rg -c '.' || echo 0)
  [ "$status" -eq 0 ] && [ "$funcs" -eq "$entries" ] || { echo "FAIL ir $f: exit $status, $funcs funcs for $entries entries: $(head -n 1 "$tmp/ir.err")"; fail=$((fail + 1)); }
done
echo "ir: $irs lowered"
status=0
build/langc check test/ir/closure.lang > /dev/null || status=$?
[ "$status" -eq 0 ] || { echo "FAIL closure.lang: check exit $status"; fail=$((fail + 1)); }
status=0
first=$(build/langc ir test/ir/closure.lang 2>&1 > /dev/null) || status=$?
case "$status:$first" in
  "1:langc: REFUSE_RUNTIME_CLOSURE: "*) ;;
  *) echo "FAIL closure.lang: want exit 1 and REFUSE_RUNTIME_CLOSURE, got exit $status: $first"; fail=$((fail + 1)) ;;
esac
got=$(build/langc eval test/ir/lazy.lang lazy 3 2>&1) || true
[ "$got" = "3" ] || { echo "FAIL lazy.lang: want 3, got $got"; fail=$((fail + 1)); }
build/langc ir test/ir/lazy.lang > /dev/null || { echo "FAIL lazy.lang: ir"; fail=$((fail + 1)); }
for want in "foldOnes 3:6" "mapUp 3:18" "bindUp 3:36" "filterUp 5:3" "trapHead 5:1" "trapSum 5:trap" "trapSum 2:3" \
  "filterTrap 2:3" "filterTrap 5:trap" "bindTrap 5:1" "stackSum 3:15" "stackTrap 0:7" "stackTrap 2:5"; do
  call=${want%%:*}
  got=$(build/langc eval test/ir/spine.lang ${call% *} ${call#* } 2>"$tmp/err") || true
  want_err=""
  [ "${want#*:}" = "trap" ] && want_err="langc: EVAL_OVERFLOW: ${call% *}: "
  case "$got|$(head -n 1 "$tmp/err")" in
    "${want#*:}|$want_err"*) ;;
    *) echo "FAIL spine.lang $call: want ${want#*:}, got $got"; fail=$((fail + 1)) ;;
  esac
done
build/langc ir test/ir/spine.lang > /dev/null || { echo "FAIL spine.lang: ir"; fail=$((fail + 1)); }
status=0
build/langc check test/ir/tree.lang > /dev/null || status=$?
[ "$status" -eq 0 ] || { echo "FAIL tree.lang: check exit $status"; fail=$((fail + 1)); }
status=0
first=$(build/langc ir test/ir/tree.lang 2>&1 > /dev/null) || status=$?
case "$status:$first" in
  "1:langc: REFUSE_RUNTIME_TREE: "*) ;;
  *) echo "FAIL tree.lang: want exit 1 and REFUSE_RUNTIME_TREE, got exit $status: $first"; fail=$((fail + 1)) ;;
esac

# The EVM target. First the assembler snippets: each `NAME HEX WANT` line of
# build/asm-selftest runs in evm and must give WANT.
snippets=0
build/asm-selftest >"$tmp/selftest.txt" 2>&1 || { echo "FAIL asm-selftest: $(tail -n 1 "$tmp/selftest.txt")"; fail=$((fail + 1)); }
awk 'NF == 3 && $2 ~ /^[0-9a-f]+$/' "$tmp/selftest.txt" >"$tmp/snippets.txt"
while read -r name hex want; do
  snippets=$((snippets + 1))
  got=$(python3 test/run-evm.py "0x$hex" --input '' </dev/null 2>&1) || true
  [ "$got" = "$want" ] || { echo "FAIL snippet $name: want $want, got $got"; fail=$((fail + 1)); }
done <"$tmp/snippets.txt"
echo "selftest: $snippets snippets"

# Each program builds, and its creation code deploys its runtime code. The
# selector table has one `NAME SELECTOR` line for each entry and no duplicate
# selector. Then the differential: on the grid of test/grid.awk, evm and
# `langc eval` give the same result for each entry (trap == trap). The stderr
# of eval is not merged into stdout.
evms=0
cases=0
mismatches=0
for f in examples/*.lang test/ir/spine.lang test/ir/lazy.lang test/ir/proof-trap.lang; do
  b="$tmp/$(basename "$f" .lang)"
  if ! build/langc build "$f" -o "$b.bin" 2>"$tmp/e.err" || ! build/langc build "$f" -o "$b.run" --runtime 2>>"$tmp/e.err" ||
    ! build/langc abi "$f" >"$b.abi" 2>>"$tmp/e.err"; then
    echo "FAIL evm build $f: $(head -n 1 "$tmp/e.err")"
    fail=$((fail + 1))
    continue
  fi
  deployed=$(evm --code "$(cat "$b.bin")" --create run 2>/dev/null | tail -n 1)
  if [ "$deployed" != "0x$(cat "$b.run")" ]; then
    echo "FAIL deploy $f: the creation code does not return the runtime code"
    fail=$((fail + 1))
    continue
  fi
  evms=$((evms + 1))
  entries=$(build/langc ir "$f" | rg -c '^func ' || echo 0)
  rows=$(rg -c '.' "$b.abi" || echo 0)
  shape=$(rg -c -v '^\S+ [0-9a-f]{8}$' "$b.abi" || echo 0)
  dups=$(awk '{ print $2 }' "$b.abi" | sort | uniq -d | rg -c '.' || echo 0)
  if [ "$rows" -ne "$entries" ] || [ "$shape" -ne 0 ] || [ "$dups" -ne 0 ]; then
    echo "FAIL abi $f: $rows rows for $entries entries, $shape bad rows, $dups duplicate selectors"
    fail=$((fail + 1))
  fi
  build/langc ir "$f" | awk -f test/grid.awk >"$tmp/cases.txt"
  : >"$tmp/want.txt"
  while read -r call; do
    status=0
    want=$(build/langc eval "$f" $call 2>/dev/null) || status=$?
    [ "$status" -eq 0 ] || want="eval-exit-$status"
    echo "$want" >>"$tmp/want.txt"
  done <"$tmp/cases.txt"
  if ! python3 test/run-evm.py "$b.run" --abi "$b.abi" - <"$tmp/cases.txt" >"$tmp/got.txt" 2>"$tmp/e.err"; then
    echo "FAIL run-evm $f: $(head -n 1 "$tmp/e.err")"
    fail=$((fail + 1))
    continue
  fi
  cases=$((cases + $(rg -c '.' "$tmp/cases.txt" || echo 0)))
  paste -d '|' "$tmp/cases.txt" "$tmp/want.txt" "$tmp/got.txt" | awk -F '|' '$2 != $3' >"$tmp/bad.txt"
  bad=$(rg -c '.' "$tmp/bad.txt" || echo 0)
  mismatches=$((mismatches + bad))
  [ "$bad" -eq 0 ] || { echo "FAIL differential $f: $bad mismatches, first $(head -n 1 "$tmp/bad.txt")"; fail=$((fail + 1)); }
done
echo "evm: $evms programs deployed"
echo "differential: $cases cases, $mismatches mismatches"

# Calls on examples/entries.lang that langc eval does not cover. A Flag
# argument larger than 1 (eval refuses it with EVAL_ARGS), an argument of 2^64,
# an unknown selector, calldata shorter than a selector and calldata without
# the arguments trap. 2^64 - 1 is the largest argument. A call value traps:
# test/prestate.json gives the default evm sender a balance, and the same call
# with value 0 gives the result.
e="$tmp/entries"
edges=0
edge() {
  label=$1
  want=$2
  shift 2
  edges=$((edges + 1))
  got=$(python3 test/run-evm.py "$e.run" "$@" </dev/null 2>&1) || true
  [ "$got" = "$want" ] || { echo "FAIL evm $label: want $want, got $got"; fail=$((fail + 1)); }
}
edge 'pick 2 4 9' trap --abi "$e.abi" pick 2 4 9
edge 'addPrice 2^64 0' trap --abi "$e.abi" addPrice 18446744073709551616 0
edge 'addPrice 2^64-1 0' 18446744073709551615 --abi "$e.abi" addPrice 18446744073709551615 0
edge 'unknown selector' trap --input deadbeef
edge 'short calldata' trap --input 00
edge 'no arguments' trap --input "$(awk '$1 == "addPrice" { print $2 }' "$e.abi")"
edge 'value 0' 3 --prestate test/prestate.json --abi "$e.abi" addPrice 1 2
edge 'value 1' trap --prestate test/prestate.json --value 1 --abi "$e.abi" addPrice 1 2
echo "evm: $edges edge calls"
rm -rf "$tmp"

if rg -n '\x{2013}|\x{2014}' . >/dev/null; then
  echo "FAIL: an em-dash or en-dash is in the kit"
  fail=$((fail + 1))
fi
echo "gate: $fail failures"
[ "$fail" -eq 0 ]
