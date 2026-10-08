#!/bin/sh
# Front end tests of langc, run by make test after make. Files go to
# build/test. Each run stays far under 4 GB: the arena of one run takes at
# most LANG_ARENA_MAX (256 MiB, src/syntax.h).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
tool=$root/build/parsetool
langc=$root/build/langc
out=$root/build/test
mkdir -p "$out"
failures=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

# check NAME STATUS WANT_STATUS ERRFILE WANT_PREFIX
check() {
  err=$(cat "$4")
  case $err in
    "$5"*) prefix_ok=1 ;;
    *) prefix_ok=0 ;;
  esac
  if [ "$2" -eq "$3" ] && [ "$prefix_ok" -eq 1 ]; then pass "$1"; else fail "$1: exit $2, stderr: $err"; fi
}

# Parse, print, parse the print and print again: the two prints are the same.
for file in domain/domain.lang test/parser-arms.lang examples/programs/arrow-impossibility.lang \
    examples/programs/arrow-debreu.lang examples/programs/schelling-ising.lang examples/mutants/fork-unfrozen.lang \
    examples/mutants/data-decl.lang examples/mutants/rec-def.lang examples/mutants/prelude-name.lang \
    examples/mutants/core-name.lang examples/mutants/hash-projection.lang examples/mutants/log-match.lang \
    examples/mutants/rule-type.lang examples/mutants/cong-type.lang examples/mutants/transport-motive.lang; do
  name=$(basename "$file" .lang)
  "$tool" "$root/$file" > "$out/$name.1" 2> "$out/$name.err"
  first=$?
  "$tool" "$out/$name.1" > "$out/$name.2" 2>> "$out/$name.err"
  second=$?
  if [ "$first" -eq 0 ] && [ "$second" -eq 0 ] && cmp -s "$out/$name.1" "$out/$name.2"; then
    pass "parse and round trip $file ($(wc -l < "$out/$name.1" | tr -d ' ') lines)"
  else
    fail "parse and round trip $file: exit $first $second, $(cat "$out/$name.err")"
  fi
done

if "$tool" --prelude | cmp -s - "$root/domain/domain.lang"; then
  pass "the embedded prelude is domain/domain.lang"
else
  fail "the embedded prelude is domain/domain.lang"
fi

# refuse NAME CODE DEF TEXT: parsetool exits 1 with "langc: CODE: DEF: ".
refuse() {
  printf '%s\n' "$4" > "$out/$1.lang"
  "$tool" "$out/$1.lang" > /dev/null 2> "$out/$1.err"
  check "$1 is $2" $? 1 "$out/$1.err" "langc: $2: $3: $out/$1.lang:"
}

refuse bad-token LEX_TOKEN x 'def x : Nat := natAdd 1 $ 2'
refuse big-number LEX_NUMBER x 'def x : Nat := 123456789012345678901234567890'
refuse unclosed-paren PARSE_PAREN x 'def x : Nat := natAdd (natAdd 1 2'
refuse unmatched-paren PARSE_PAREN x 'def x : Nat := natAdd 1 2)'
refuse missing-define PARSE_EXPECT x "$(printf 'def x : Nat\ndef y : Nat := 3')"
refuse missing-term PARSE_EXPECT x 'def x : Nat :='
refuse tuple-arity PARSE_ARITY x 'def x : prod (Nat, Nat) := tuple (1)'
refuse inj-arity PARSE_ARITY x 'def x : Nat := inj 2 of 2 x'
refuse case-arity PARSE_ARITY x 'def x : Nat := case s with | 1 (a : Nat) => a | 0 (b : Nat) => b'
refuse missing-match-arm PARSE_EXPECT x 'def x : Nat := case s with | 0 (a : Nat) => match a as z in F return Nat with | 1 (b : Nat) => b'
refuse deep-parens PARSE_DEPTH x "def x : Nat := $(awk 'BEGIN { for (i = 0; i < 100000; i++) printf "("; printf "0"; for (i = 0; i < 100000; i++) printf ")" }')"
refuse deep-arrows PARSE_DEPTH x "def x : $(awk 'BEGIN { for (i = 0; i < 5000; i++) printf "Nat -> " }')Nat := 0"
refuse long-spine PARSE_DEPTH x "def x : Nat := f$(awk 'BEGIN { for (i = 0; i < 5000; i++) printf " 0" }')"

# langc: usage and IO exit 2, each verb exits 0 on a program that checks.
"$langc" > /dev/null 2> "$out/usage.err"
check "langc with no verb is usage" $? 2 "$out/usage.err" "langc: USAGE: -: "
"$langc" frob x > /dev/null 2> "$out/usage.err"
check "langc frob is usage" $? 2 "$out/usage.err" "langc: USAGE: -: "
"$langc" eval x > /dev/null 2> "$out/usage.err"
check "langc eval without NAME is usage" $? 2 "$out/usage.err" "langc: USAGE: -: "
"$langc" abi x y > /dev/null 2> "$out/usage.err"
check "langc abi with NAME is usage" $? 2 "$out/usage.err" "langc: USAGE: -: "
"$langc" build x --runtime > /dev/null 2> "$out/usage.err"
check "langc build without -o is usage" $? 2 "$out/usage.err" "langc: USAGE: -: "
"$langc" check "$out/no-such-file.lang" > /dev/null 2> "$out/io.err"
check "langc check of a missing file is IO" $? 2 "$out/io.err" "langc: IO_READ: -: "
"$langc" check "$out/bad-token.lang" > /dev/null 2> "$out/refused.err"
check "langc check of a bad file is refused" $? 1 "$out/refused.err" "langc: LEX_TOKEN: x: "
program=$root/examples/programs/arrow-debreu.lang
"$langc" check "$program" > /dev/null 2> "$out/verb.err"
check "langc check exits 0" $? 0 "$out/verb.err" ""
"$langc" eval "$program" members > /dev/null 2> "$out/verb.err"
check "langc eval exits 0" $? 0 "$out/verb.err" ""
"$langc" abi "$program" > /dev/null 2> "$out/verb.err"
check "langc abi exits 0" $? 0 "$out/verb.err" ""
"$langc" build "$program" --runtime -o "$out/x.hex" > /dev/null 2> "$out/verb.err"
check "langc build exits 0" $? 0 "$out/verb.err" ""

if [ "$failures" -eq 0 ]; then echo "parse.sh: all passed"; exit 0; fi
echo "parse.sh: $failures failed"
exit 1
