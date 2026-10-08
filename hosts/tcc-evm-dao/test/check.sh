#!/bin/sh
# Checker and verb tests of langc, run by test/gate.sh (make check): the gate
# checker fixtures, the mutants in test/mutants and the guards.
# Files go to build/test. Each run stays far under 4 GB: the arena of one run
# takes at most LANG_ARENA_MAX (256 MiB, src/syntax.h).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
langc=$root/build/langc
programs=$root/examples
out=$root/build/test
mkdir -p "$out"
failures=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

# expect NAME WANT_STATUS WANT_STDOUT ARGS...: exit status and stdout, with an
# empty stderr on exit 0.
expect() {
  name=$1 want_status=$2 want=$3
  shift 3
  "$langc" "$@" > "$out/check.out" 2> "$out/check.err"
  status=$?
  got=$(cat "$out/check.out")
  err=$(cat "$out/check.err")
  if [ "$status" -eq "$want_status" ] && [ "$got" = "$want" ] && { [ "$status" -ne 0 ] || [ -z "$err" ]; }; then
    pass "$name"
  else
    fail "$name: exit $status, stdout: $got, stderr: $err"
  fi
}

# refuse NAME CODE DEF SUFFIX ARGS...: exit 1, stderr "langc: CODE: DEF: ..."
# that ends in SUFFIX.
refuse() {
  name=$1 code=$2 def=$3 suffix=$4
  shift 4
  "$langc" "$@" > /dev/null 2> "$out/check.err"
  status=$?
  err=$(cat "$out/check.err")
  case $err in
    "langc: $code: $def: "*"$suffix") shape=1 ;;
    *) shape=0 ;;
  esac
  if [ "$status" -eq 1 ] && [ "$shape" -eq 1 ]; then pass "$name"; else fail "$name: exit $status, stderr: $err"; fi
}

expect "check arrow-debreu" 0 "ok debreu" check "$programs/arrow-debreu.lang"
expect "check arrow-impossibility" 0 "ok impossibility" check "$programs/arrow-impossibility.lang"
expect "table arrow-debreu" 0 "debreu 3 3 3 2 2 3 3 2 1 1 1" table "$programs/arrow-debreu.lang"
expect "table arrow-impossibility" 0 "impossibility 3" table "$programs/arrow-impossibility.lang"
expect "verdicts arrow-debreu F" 0 "111123133123222323133323333" verdicts "$programs/arrow-debreu.lang" F
expect "verdicts arrow-impossibility first" 0 "111111111222222222333333333" verdicts "$programs/arrow-impossibility.lang" first
expect "eval payeeAfterSettle" 0 "reflNat 5" eval "$programs/arrow-debreu.lang" payeeAfterSettle
expect "eval firstX" 0 "reflDec release" eval "$programs/arrow-impossibility.lang" firstX
expect "eval members" 0 "3" eval "$programs/arrow-impossibility.lang" members

# Erased Sigma fields may be constructed from erased variables and used
# in types, while the second field remains available at run time.
cat > "$out/erased-sigma.lang" <<'EOF'
def members : Nat := 3
def pack : (0 n : Nat) -> (0 x : Nat) * EqNat x n :=
  fun (0 n : Nat) => (n, reflNat n)
def unpack : (p : (0 x : Nat) * Nat) -> Nat := fun (p : (0 x : Nat) * Nat) => p.1
def proof : (p : (0 x : Nat) * EqNat x x) -> EqNat p.0 p.0 :=
  fun (p : (0 x : Nat) * EqNat x x) => p.1
def witness : EqNat (unpack (1, 2)) 2 := reflNat 2
EOF
expect "erased Sigma construction and type projection" 0 "ok impossibility" check "$out/erased-sigma.lang"

# A definitionally equal annotation must choose the same regime and
# contract, including through an alias of the Aggregation type function.
for annotation in 'Aggregation F' AggF 'AggType F'; do
  cat > "$out/aggregation-alias.lang" <<EOF
def members : Nat := 1
def F : ChoiceRule := fun (x : Config) => release
def AggF : Type 0 := Aggregation F
def AggType : (0 F : ChoiceRule) -> Type 0 := Aggregation
def agg : $annotation := mkAgg F (fun (t : Tally) => release) (fun (x : Config) => reflDec release)
EOF
  expect "check aggregation annotation $annotation" 0 "ok debreu" check "$out/aggregation-alias.lang"
  expect "table aggregation annotation $annotation" 0 "debreu 1 1 1 1" table "$out/aggregation-alias.lang"
  expect "build aggregation annotation $annotation" 0 "" build "$out/aggregation-alias.lang" -o "$out/aggregation.hex"
  if [ "$annotation" = 'Aggregation F' ]; then
    cp "$out/aggregation.hex" "$out/aggregation-direct.hex"
  elif cmp -s "$out/aggregation.hex" "$out/aggregation-direct.hex"; then
    pass "aggregation annotation $annotation preserves bytecode"
  else
    fail "aggregation annotation $annotation changes bytecode"
  fi
done

refuse "mutant debreu payeeAfterSettle reflNat 4" TYPE_MISMATCH payeeAfterSettle \
  "the types differ: expected EqNat 5 5, found EqNat 4 4" check "$root/test/mutants/debreu-payee-4.lang"
refuse "verdicts of a name that is not a ChoiceRule" VERDICT_TYPE agg "agg is not a ChoiceRule" \
  verdicts "$programs/arrow-debreu.lang" agg
refuse "eval of an unknown name" TYPE_SCOPE nothing "nothing is not declared" \
  eval "$programs/arrow-debreu.lang" nothing

# Each append of a list to itself doubles it; the evaluation of the long
# appends nests past the depth cap, and the run stops with TYPE_FUEL.
awk 'BEGIN {
  print "def members : Nat := 1"
  print "def l0 : Ballots := bcons release bnil"
  for (i = 1; i <= 16; i++) printf "def l%d : Ballots := appendBallots l%d l%d\n", i, i - 1, i - 1
}' > "$out/deep.lang"
refuse "deep evaluation is TYPE_FUEL" TYPE_FUEL l11 "evaluation nests too deep" check "$out/deep.lang"

if [ "$failures" -eq 0 ]; then echo "check.sh: all passed"; exit 0; fi
echo "check.sh: $failures failed"
exit 1
