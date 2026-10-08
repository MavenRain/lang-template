#!/bin/sh
# Checker tests of langc, run by make test after make (SPEC section 10,
# chunks 3, 4b and 7): the prelude, the fate report of each example program
# and the mutants in examples/mutants. Files go to build/test. Each run stays far under 4 GB:
# the arena of one run takes at most LANG_ARENA_MAX (src/syntax.h).
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

# accepts NAME PROG: check exits 0 and prints a fate report.
accepts() {
  "$langc" check "$2" > "$out/check.out" 2> "$out/check.err"
  status=$?
  case $(head -n 1 "$out/check.out") in
    "members "*) shape=1 ;;
    *) shape=0 ;;
  esac
  if [ "$status" -eq 0 ] && [ "$shape" -eq 1 ]; then pass "$1"; else fail "$1: exit $status, stderr: $(cat "$out/check.err")"; fi
}

# report_is NAME PROG: check exits 0 and stdout equals $out/want.txt.
report_is() {
  "$langc" check "$2" > "$out/check.out" 2> "$out/check.err"
  status=$?
  if [ "$status" -eq 0 ] && cmp -s "$out/check.out" "$out/want.txt"; then
    pass "$1"
  else
    fail "$1: exit $status, stderr: $(cat "$out/check.err")"
    diff "$out/want.txt" "$out/check.out"
  fi
}


# The prelude checks: with members only, the first error is the missing
# candidates, after the whole prelude.
printf 'def members : Nat := 1\n' > "$out/members-only.lang"
refuse "the prelude checks" TYPE_SCOPE candidates "is not declared; a program defines members, candidates and rule" \
  check "$out/members-only.lang"
printf 'def candidates : Nat := 1\n' > "$out/no-members.lang"
refuse "members is the first definition" REFUSE_MEMBERS - "" check "$out/no-members.lang"

# Each program checks and gives its fate report (SPEC sections 6 and 7).
cat > "$out/want.txt" <<'EOF'
members 2
candidates 2
fate none 3
tally 2 0
tally 1 1
tally 0 2
fate one 0
fate two 0
EOF
report_is "arrow-impossibility is none at every tally" "$programs/arrow-impossibility.lang"
cat > "$out/want.txt" <<'EOF'
members 3
candidates 2
fate none 0
fate one 4
tally 3 0
tally 2 1
tally 1 2
tally 0 3
fate two 0
EOF
report_is "arrow-debreu is one p at every tally" "$programs/arrow-debreu.lang"
cat > "$out/want.txt" <<'EOF'
members 2
candidates 2
fate none 0
fate one 0
fate two 3
tally 2 0
tally 1 1
tally 0 2
EOF
report_is "schelling-ising is two p q at every tally" "$programs/schelling-ising.lang"

# check tabulates, so a table that is too large is TABLE_LIMIT.
{ printf 'def members : Nat := 4096\n'; tail -n +5 "$programs/arrow-impossibility.lang"; } > "$out/check-limit.lang"
refuse "check of 4096 members is TABLE_LIMIT" TABLE_LIMIT candidates \
  "4096 members and 2 candidates give more than 4096 tallies" check "$out/check-limit.lang"

# SPEC section 2: a two p q side whose forkFreeze is flagNo.
refuse "fork-unfrozen is REFUSE_FORK" REFUSE_FORK rule "is flagNo" check "$mutants/fork-unfrozen.lang"

# SPEC section 2 and prelude note P1: each mutant has one defect, and the
# checker gives its code at the definition with the defect.
refuse "data-decl is REFUSE_DATA" REFUSE_DATA Color "mu lives in the prelude" check "$mutants/data-decl.lang"
refuse "rec-def is REFUSE_REC" REFUSE_REC spin "recursion lives in the prelude" check "$mutants/rec-def.lang"
refuse "prelude-name is REFUSE_NAME" REFUSE_NAME flagYes "flagYes is declared already" check "$mutants/prelude-name.lang"
refuse "core-name is REFUSE_NAME" REFUSE_NAME Hash "Hash is declared already" check "$mutants/core-name.lang"
refuse "hash-projection is TYPE_SHAPE" TYPE_SHAPE hashHead "a projection .0 of a term that is not a pair" \
  check "$mutants/hash-projection.lang"
refuse "log-match is TYPE_MATCH" TYPE_MATCH logSize "the subject is not of the family AnchorLog with 0 indices" \
  check "$mutants/log-match.lang"
refuse "rule-type is TYPE_MISMATCH" TYPE_MISMATCH rule "expected Tally -> Outcome, found Nat -> Outcome" \
  check "$mutants/rule-type.lang"

# SPEC section 3, F13: transport and cong of EqOutcome are prelude defs. A
# program can use them, a wrong motive or result type is TYPE_MISMATCH, and
# a program cannot define them again.
one_genesis='one (mkPolicy allow nonZero blockTime 0 1 (inj 1 of 2 (tuple ())))'
refuse "cong-type is TYPE_MISMATCH" TYPE_MISMATCH congWrong "found EqOutcome ($one_genesis) ($one_genesis)" \
  check "$mutants/cong-type.lang"
refuse "transport-motive is TYPE_MISMATCH" TYPE_MISMATCH moved "expected Outcome, found Policy" \
  check "$mutants/transport-motive.lang"
{ cat "$programs/arrow-impossibility.lang"; cat <<'EOF'

def keep : Outcome := transportOutcome (fun (w : Outcome) => Outcome) none none (sameOutcome none) (one genesis)
def lift : EqOutcome (one genesis) (one genesis) := congOutcome (fun (w : Outcome) => one genesis) none none (sameOutcome none)
EOF
} > "$out/eq-use.lang"
accepts "a program uses transportOutcome and congOutcome" "$out/eq-use.lang"
{ head -n 12 "$programs/arrow-impossibility.lang"; printf '\ndef transportOutcome : Nat := 1\n'; tail -n +13 "$programs/arrow-impossibility.lang"; } > "$out/eq-name.lang"
refuse "a program that defines transportOutcome is REFUSE_NAME" REFUSE_NAME transportOutcome \
  "transportOutcome is declared already" check "$out/eq-name.lang"

# SPEC section 2: the surface has no axiom form, so the parser refuses an
# axiom before the checker can give REFUSE_AXIOM.
printf 'axiom x : Nat\n' > "$out/axiom.lang"
refuse "an axiom is PARSE_EXPECT" PARSE_EXPECT - "expected 'def' or 'mu', found 'axiom'" check "$out/axiom.lang"

# A policy or its freeze flag may branch on the tally. Every branch must
# freeze, on either side of two. The match variant also exercises a stuck
# match whose arm contains a case, followed by policyForkFreeze.
for form in policy-case policy-match flag-case; do
  for bad in neither zero one; do
    flag0=flagYes flag1=flagYes
    case $bad in zero) flag0=flagNo ;; one) flag1=flagNo ;; esac
    for side in p q; do
      fixture=$out/fork-$form-$bad-$side.lang
      cat > "$fixture" <<EOF
def members : Nat := 1
def safe : Policy := mkPolicy allow nonZero blockTime 0 1 flagYes
def a : Policy := mkPolicy allow nonZero blockTime 0 1 $flag0
def b : Policy := mkPolicy allow nonZero blockTime 0 2 $flag1
def candidates : Candidates := consPolicy a (lastPolicy b)
EOF
      case $form in
        policy-case)
          cat >> "$fixture" <<'EOF'
def choose : Tally -> Policy := fun (t : Tally) =>
  case natEq (tallyCount 0 t) 0 with
  | 0 (u : prod ()) => a
  | 1 (u : prod ()) => b
EOF
          ;;
        policy-match)
          cat >> "$fixture" <<'EOF'
def choose : Tally -> Policy := fun (t : Tally) =>
  match t as w in Tally return Policy with
  | mkTally count =>
    case natEq (count 0) 0 with
    | 0 (u : prod ()) => a
    | 1 (u : prod ()) => b
EOF
          ;;
        flag-case)
          cat >> "$fixture" <<EOF
def freeze : Tally -> Flag := fun (t : Tally) =>
  case natEq (tallyCount 0 t) 0 with
  | 0 (u : prod ()) => $flag0
  | 1 (u : prod ()) => $flag1
def choose : Tally -> Policy := fun (t : Tally) =>
  mkPolicy allow nonZero blockTime 0 1 (freeze t)
EOF
          ;;
      esac
      left='choose t' right=safe
      if [ "$side" = q ]; then left=safe right='choose t'; fi
      printf 'def rule : Tally -> Outcome := fun (t : Tally) => two (%s) (%s)\n' \
        "$left" "$right" >> "$fixture"
      if [ "$bad" = neither ]; then
        accepts "fork $form $bad $side checks" "$fixture"
      else
        refuse "fork $form $bad $side refuses" REFUSE_FORK rule \
          "policyForkFreeze $side of a two p q outcome is flagNo" check "$fixture"
      fi
    done
  done
done

# Erased inputs can construct types, including through eliminators.
# They still cannot determine a run-time result or escape a type check.
cat > "$out/erased-types.lang" <<'EOF'
def members : Nat := 1
def Id : (0 A : Type 0) -> Type 0 := fun (0 A : Type 0) => A
def idNat : Id Nat := 0
def typeArg : (0 A : Type 0) -> (F : Type 0 -> Nat) -> Nat :=
  fun (0 A : Type 0) (F : Type 0 -> Nat) => F A
def CaseType : (0 f : Flag) -> Type 0 := fun (0 f : Flag) =>
  case f with
  | 0 (u : prod ()) => Nat
  | 1 (u : prod ()) => Flag
def caseNat : CaseType flagNo := 0
def MatchType : (0 v : Verdict) -> Type 0 := fun (0 v : Verdict) =>
  match v as w in Verdict return Type 0 with
  | allow => Nat
  | deny => Flag
def matchFlag : MatchType deny := flagYes
def candidates : Candidates := lastPolicy (mkPolicy allow nonZero blockTime 0 1 flagYes)
def rule : Tally -> Outcome := fun (t : Tally) => none
EOF
accepts "erased inputs construct types" "$out/erased-types.lang"

for use in direct application case match proof projection mismatch; do
  fixture=$out/erased-$use.lang
  printf 'def members : Nat := 1\n' > "$fixture"
  case $use in
    direct)
      printf 'def leak : (0 x : Nat) -> Nat := fun (0 x : Nat) => x\n' >> "$fixture"
      ;;
    application)
      cat >> "$fixture" <<'EOF'
def leak : (0 x : Nat) -> (F : Nat -> Nat) -> Nat :=
  fun (0 x : Nat) (F : Nat -> Nat) => F x
EOF
      ;;
    case)
      cat >> "$fixture" <<'EOF'
def leak : (0 f : Flag) -> Nat := fun (0 f : Flag) =>
  case f with
  | 0 (u : prod ()) => 0
  | 1 (u : prod ()) => 1
EOF
      ;;
    match)
      cat >> "$fixture" <<'EOF'
def leak : (0 v : Verdict) -> Nat := fun (0 v : Verdict) =>
  match v as w in Verdict return Nat with
  | allow => 0
  | deny => 1
EOF
      ;;
    proof)
      cat >> "$fixture" <<'EOF'
def leak : (0 e : EqOutcome none none) -> Nat := fun (0 e : EqOutcome none none) =>
  match e as q in EqOutcome i j return Nat with
  | sameOutcome 0 z => 0
EOF
      ;;
    projection)
      cat >> "$fixture" <<'EOF'
def leak : ((0 n : Nat) * Nat) -> Nat :=
  fun (p : (0 n : Nat) * Nat) => p.0
EOF
      ;;
    mismatch)
      printf 'def leak : (0 A : Type 0) -> Type 0 := fun (0 A : Type 0) => 0\n' >> "$fixture"
      ;;
  esac
  expected=TYPE_ERASED
  if [ "$use" = mismatch ]; then expected=TYPE_MISMATCH; fi
  refuse "erased $use is refused" "$expected" leak "" check "$fixture"
done

if [ "$failures" -ne 0 ]; then
  printf '%s checker test(s) failed\n' "$failures"
  exit 1
fi
printf 'check.sh: all passed\n'
