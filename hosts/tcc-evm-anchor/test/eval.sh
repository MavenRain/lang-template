#!/bin/sh
# Eval tests of langc, run by make test after make (SPEC section 10,
# chunks 4b and 7): eval prints the normal form of a def of each example program,
# a name that is not a def gives EVAL_NAME, and each mutant keeps its code
# under eval. Files go to build/test. Each run stays far under 4 GB: the
# arena of one run takes at most LANG_ARENA_MAX (src/syntax.h).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
langc=$root/build/langc
programs=$root/examples/programs
mutants=$root/examples/mutants
out=$root/build/test
mkdir -p "$out"
failures=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

open='mkPolicy allow nonZero blockTime 0 1 (inj 1 of 2 (tuple ()))'
closed='mkPolicy deny nonZero blockTime 0 1 (inj 1 of 2 (tuple ()))'
wide='mkPolicy allow nonZero blockTime 0 2 (inj 1 of 2 (tuple ()))'
t='anchorq0x0'
count() { printf 'match %s as anchorq1x0 in Tally return Nat with | mkTally anchorq1x0 => anchorq1x0 %s' "$t" "$1"; }
leg() { printf '%s (anchorq1x0 : prod ()) => %s' "$1" "$2"; }

# eval_is PROG NAME WANT: exit 0 and stdout is the line WANT.
eval_is() {
  "$langc" eval "$programs/$1.lang" "$2" > "$out/eval.out" 2> "$out/eval.err"
  status=$?
  printf '%s\n' "$3" > "$out/eval.want"
  if [ "$status" -eq 0 ] && cmp -s "$out/eval.out" "$out/eval.want"; then
    pass "eval $1 $2"
  else
    fail "eval $1 $2: exit $status, stderr: $(cat "$out/eval.err")"
    diff "$out/eval.want" "$out/eval.out"
  fi
}

# refuse NAME CODE DEF SUFFIX ARGS...: exit 1, stderr "langc: CODE: DEF: ..."
# that ends in SUFFIX.
refuse() {
  name=$1 code=$2 def=$3 suffix=$4
  shift 4
  "$langc" "$@" > /dev/null 2> "$out/eval.err"
  status=$?
  err=$(cat "$out/eval.err")
  case $err in
    "langc: $code: $def: "*"$suffix") shape=1 ;;
    *) shape=0 ;;
  esac
  if [ "$status" -eq 1 ] && [ "$shape" -eq 1 ]; then pass "$name"; else fail "$name: exit $status, stderr: $err"; fi
}

# The defs of each example program, and one def of the prelude.
eval_is arrow-impossibility members 2
eval_is arrow-impossibility genesis "$open"
eval_is arrow-impossibility candidates "consPolicy ($open) (lastPolicy ($closed))"
eval_is arrow-impossibility rule "fun ($t : Tally) => none"
eval_is arrow-impossibility flagYes 'inj 1 of 2 (tuple ())'
eval_is arrow-debreu closedLog "$closed"
eval_is arrow-debreu rule "fun ($t : Tally) => case natLt ($(count 1)) ($(count 0)) with | $(leg 0 "one ($closed)") | $(leg 1 "one ($open)")"
eval_is schelling-ising schemaTwo "$wide"
eval_is schelling-ising rule "fun ($t : Tally) => case natLt ($(count 0)) ($(count 1)) with | $(leg 0 "two ($open) ($wide)") | $(leg 1 "two ($wide) ($open)")"

# Recursive prelude defs evaluate to functions that can be checked and used.
for fold in candidatesFold profileFold; do
  case $fold in
    candidatesFold)
      type='(0 B : Type 0) -> (Policy -> B) -> (Policy -> B -> B) -> Candidates -> B'
      args='Nat (fun (p : Policy) => 7) (fun (p : Policy) (n : Nat) => natAdd n 1) candidates'
      want=8 ;;
    profileFold)
      type='(0 B : Type 0) -> (Ballot -> B -> B) -> B -> Profile -> B'
      args='Nat (fun (b : Ballot) (n : Nat) => natAdd b n) 0 (withBallot 2 (withBallot 3 noBallots))'
      want=5 ;;
  esac
  "$langc" eval "$programs/arrow-impossibility.lang" "$fold" > "$out/fold.out" 2> "$out/eval.err"
  status=$?
  if [ "$status" -ne 0 ]; then
    fail "eval $fold: exit $status, stderr: $(cat "$out/eval.err")"
    continue
  fi
  {
    cat "$programs/arrow-impossibility.lang"
    printf '\ndef copiedFold : %s := ' "$type"
    cat "$out/fold.out"
    printf 'def folded : Nat := copiedFold %s\n' "$args"
  } > "$out/eval-fold.lang"
  "$langc" eval "$out/eval-fold.lang" folded > "$out/eval.out" 2> "$out/eval.err"
  status=$?
  if [ "$status" -eq 0 ] && [ "$(cat "$out/eval.out")" = "$want" ]; then
    pass "eval $fold prints a usable function"
  else
    fail "eval $fold round trip: exit $status, stderr: $(cat "$out/eval.err")"
  fi
done

# SPEC section 3, F13: transport and cong of EqOutcome compute by
# definition. At sameOutcome, transport gives its input and cong gives
# sameOutcome (f o).
{ cat "$programs/arrow-impossibility.lang"; cat <<'EOF'

def lawTransport : Outcome -> Outcome :=
  fun (u : Outcome) => transportOutcome (fun (w : Outcome) => Outcome) none none (sameOutcome none) u
def lawCong : (o : Outcome) -> EqOutcome (one genesis) (one genesis) :=
  fun (o : Outcome) => congOutcome (fun (w : Outcome) => one genesis) o o (sameOutcome o)
EOF
} > "$out/eval-eq.lang"

# law_is NAME WANT: eval of NAME in eval-eq.lang exits 0 and stdout is the line WANT.
law_is() {
  "$langc" eval "$out/eval-eq.lang" "$1" > "$out/eval.out" 2> "$out/eval.err"
  status=$?
  printf '%s\n' "$2" > "$out/eval.want"
  if [ "$status" -eq 0 ] && cmp -s "$out/eval.out" "$out/eval.want"; then
    pass "eval law $1"
  else
    fail "eval law $1: exit $status, stderr: $(cat "$out/eval.err")"
    diff "$out/eval.want" "$out/eval.out"
  fi
}
law_is lawTransport "fun ($t : Outcome) => $t"
law_is lawCong "fun ($t : Outcome) => sameOutcome (one ($open))"

# A name that is not a def of the prelude or the program is EVAL_NAME.
program=$programs/arrow-debreu.lang
refuse "eval of an unknown name" EVAL_NAME nosuch "nosuch is not declared" eval "$program" nosuch
refuse "eval of a constructor" EVAL_NAME none "none is not a def" eval "$program" none
refuse "eval of a core name" EVAL_NAME Hash "Hash is not a def" eval "$program" Hash
refuse "eval of a builtin" EVAL_NAME Nat "Nat is not a def" eval "$program" Nat

# eval does not tabulate: a program whose table is TABLE_LIMIT evaluates.
{ printf 'def members : Nat := 4096\n'; tail -n +5 "$programs/arrow-impossibility.lang"; } > "$out/eval-limit.lang"
"$langc" eval "$out/eval-limit.lang" members > "$out/eval.out" 2> "$out/eval.err"
status=$?
if [ "$status" -eq 0 ] && [ "$(cat "$out/eval.out")" = 4096 ]; then
  pass "eval does not tabulate"
else
  fail "eval does not tabulate: exit $status, stderr: $(cat "$out/eval.err")"
fi

# Each mutant gives the same code under eval as under check.
for f in "$mutants"/*.lang; do
  "$langc" check "$f" > /dev/null 2> "$out/check.err"
  "$langc" eval "$f" rule > /dev/null 2> "$out/eval.err"
  status=$?
  want=$(cut -d: -f2 "$out/check.err")
  got=$(cut -d: -f2 "$out/eval.err")
  same=0
  if [ -n "$want" ] && [ "$want" = "$got" ]; then same=1; fi
  if [ "$status" -eq 1 ] && [ "$same" -eq 1 ]; then
    pass "mutant $(basename "$f") keeps$got"
  else
    fail "mutant $(basename "$f"): exit $status, check$want, eval$got"
  fi
done

if [ "$failures" -eq 0 ]; then echo "eval.sh: all passed"; exit 0; fi
echo "eval.sh: $failures failed"
exit 1
