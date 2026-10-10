#!/bin/sh
# The build gate (kit slice K4a). `make check` runs it from the kit directory
# after test/asm.sh. It needs evm (go-ethereum), bc and build/slottool.
set -eu
fail=0
checks=0
builds=0
refused=0
tmp=${TMPDIR:-/tmp}/langc-build.$$
mkdir -p "$tmp"

# hexnorm H: the hex number H in lowercase with no leading zeros ("0" for zero).
hexnorm() {
  printf 'obase=16; ibase=16; %s\n' "$(printf '%s' "$1" | tr a-f A-F)" | bc | tr A-F a-f
}

# word TOKEN: the storage word of a value that `langc eval` prints, as hexnorm.
word() {
  case $1 in
    0x*) hexnorm "${1#0x}" ;;
    flagYes) echo 1 ;;
    flagNo) echo 0 ;;
    *u) printf 'obase=16; %s\n' "${1%u}" | bc | tr A-F a-f ;;
    *) printf 'obase=16; %s\n' "$1" | bc | tr A-F a-f ;;
  esac
}

# pad H: H as 64 hex digits.
pad() {
  printf '%64s' "$1" | tr ' ' 0
}

# put SLOT VALUE: one expected storage row. A zero word is not stored.
put() {
  [ "$2" = 0 ] || echo "$1 $2" >>"$tmp/want"
}

# list SLOT ARG: the length n at SLOT, element j at keccak256(SLOT) + (n-1-j) (C-c9-1).
list() {
  base=$(build/slottool "$1")
  len=$(printf '%s' "$2" | tr -d '()' | awk '{ for (i = 1; i <= NF; i++) if ($i != "cons" && $i != "nil") c++ } END { print c + 0 }')
  j=0
  for t in $(printf '%s' "$2" | tr -d '()'); do
    case $t in
      cons|nil) ;;
      *)
        at=$(printf 'obase=16; ibase=16; %s + %X\n' "$(printf '%s' "$base" | tr a-f A-F)" "$((len - 1 - j))" | bc | tr A-F a-f)
        put "$(pad "$at")" "$(word "$t")"
        j=$((j + 1)) ;;
    esac
  done
  put "$1" "$(word "$j")"
}

# map SLOT ARG: the value at key k at keccak256(k . SLOT).
map() {
  slot=$1
  set -- $(printf '%s' "$2" | tr -d '()[],')
  shift
  while [ "$#" -ge 2 ]; do
    put "$(build/slottool "$(pad "$(word "$1")")$slot")" "$(word "$2")"
    shift 2
  done
}

# For each example with a state, `langc build` and `evm run --create --dump`
# must give the storage of `langc eval PROG init` (the reference evaluator).
for prog in examples/contract.lang examples/map.lang examples/storage.lang; do
  builds=$((builds + 1))
  checks=$((checks + 1))
  : >"$tmp/want"
  build/langc eval "$prog" init | awk '{
    s = $0; sub(/^makeState /, "", s); d = 0; a = ""
    for (i = 1; i <= length(s); i++) {
      c = substr(s, i, 1)
      if (c == "(" || c == "[") d++
      if (c == ")" || c == "]") d--
      if (c == " " && d == 0) { print a; a = "" } else a = a c
    }
    if (a != "") print a
  }' >"$tmp/args"
  n=0
  while IFS= read -r arg; do
    slot=$(pad "$(printf '%x' "$n")")
    case $arg in
      "(cons "*|nil) list "$slot" "$arg" ;;
      "(mapOf "*) map "$slot" "$arg" ;;
      *) put "$slot" "$(word "$arg")" ;;
    esac
    n=$((n + 1))
  done <"$tmp/args"
  status=0
  hex=$(build/langc build "$prog" 2>"$tmp/err") || status=$?
  evm run --create --dump --code "$hex" 2>/dev/null >"$tmp/dump" || status=$?
  awk -F'"' 'NF >= 5 && length($2) == 66 && substr($2, 1, 2) == "0x" { print substr($2, 3), $4 }' "$tmp/dump" >"$tmp/rows"
  : >"$tmp/got"
  while read -r key value; do
    echo "$key $(hexnorm "$value")" >>"$tmp/got"
  done <"$tmp/rows"
  sort "$tmp/want" >"$tmp/want.sorted"
  sort "$tmp/got" >"$tmp/got.sorted"
  if [ "$status" -ne 0 ] || ! cmp -s "$tmp/want.sorted" "$tmp/got.sorted"; then
    echo "FAIL build $prog: exit $status: $(head -n 1 "$tmp/err")"
    diff "$tmp/want.sorted" "$tmp/got.sorted" | head -n 6
    fail=$((fail + 1))
  fi
done

# A program with no entry (slice K4b, C-K4b-1): the dispatcher head
# (CALLVALUE, CALLDATASIZE < 4, selector), the shared empty REVERT block at
# 0x12 and the shared Trap() REVERT block at 0x16.
head=346100125760043610610012575f3560e01c5b5f5ffd5b63ae96083a60e01b5f5260045ffd
checks=$((checks + 1))
if [ "$(build/langc build examples/storage.lang --runtime)" != "$head" ]; then
  echo "FAIL build --runtime: want $head"
  fail=$((fail + 1))
fi

# For each row `FILE CODE` of test/lower/expect.txt, `langc build test/lower/FILE`
# must exit 1, print nothing on stdout and print `langc: CODE: ...` on stderr.
while read -r file code; do
  refused=$((refused + 1))
  checks=$((checks + 1))
  status=0
  build/langc build "test/lower/$file" >"$tmp/out" 2>"$tmp/err" || status=$?
  case "$status:$(head -n 1 "$tmp/err")" in
    "1:langc: $code: "*) [ ! -s "$tmp/out" ] || { echo "FAIL lower $file: output on stdout"; fail=$((fail + 1)); } ;;
    *) echo "FAIL lower $file: want exit 1 and $code, got exit $status: $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
  esac
done <test/lower/expect.txt

# expect NAME WANT GOT: one build-output check.
expect() {
  checks=$((checks + 1))
  [ "$2" = "$3" ] || { echo "FAIL build output $1: want $2, got $3"; fail=$((fail + 1)); }
}

# `-o OUT` writes the bytes of stdout. A refused build keeps an existing OUT
# and makes no new OUT.
build/langc build examples/storage.lang >"$tmp/ref.hex"
status=0
build/langc build examples/storage.lang -o "$tmp/new.hex" >"$tmp/out" || status=$?
expect '-o exit' 0 "$status"
expect '-o bytes' same "$(cmp -s "$tmp/ref.hex" "$tmp/new.hex" && echo same || echo different)"
expect '-o stdout' 0 "$(wc -c <"$tmp/out" | tr -d ' ')"
cp "$tmp/ref.hex" "$tmp/existing.hex"
status=0
build/langc build test/lower/option.lang -o "$tmp/existing.hex" 2>/dev/null || status=$?
expect 'refused -o exit' 1 "$status"
expect 'refused -o keeps file' same "$(cmp -s "$tmp/ref.hex" "$tmp/existing.hex" && echo same || echo different)"
rm -f "$tmp/absent.hex"
status=0
build/langc build test/lower/option.lang -o "$tmp/absent.hex" 2>/dev/null || status=$?
expect 'refused new -o exit' 1 "$status"
expect 'refused new -o file' absent "$([ -e "$tmp/absent.hex" ] && echo present || echo absent)"

# A failed write to stdout (a read-only descriptor) is IO_WRITE, exit 2.
status=0
build/langc build examples/storage.lang 1<"$tmp/ref.hex" 2>"$tmp/err" || status=$?
expect 'stdout write exit' 2 "$status"
case $(head -n 1 "$tmp/err") in
  "langc: IO_WRITE: -: stdout: "*) expect 'stdout write code' IO_WRITE IO_WRITE ;;
  *) expect 'stdout write code' IO_WRITE "$(head -n 1 "$tmp/err")" ;;
esac

rm -rf "$tmp"
echo "builds: $builds checked, lower refusals: $refused checked, checks: $checks"
echo "build gate: $fail failures"
[ "$fail" -eq 0 ]
