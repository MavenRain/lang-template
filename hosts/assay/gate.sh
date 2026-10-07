#!/usr/bin/env bash
# usage: bash gate.sh
# The gate of the assay host kit.  The static steps run first.  Then the
# assay steps run the binary through run.sh, one run at a time.  Each
# step prints PASS or FAIL.  A `check` takes 1 to 50 s, by the load of the
# machine.  The logs stay in the work directory that the gate prints.
set -euo pipefail

kit=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/assay-gate.XXXXXX")
export ASSAY_SCRATCH=$work
fails=0

pass() { printf 'PASS %s\n' "$1"; }
fail() {
  printf 'FAIL %s\n' "$1"
  fails=$((fails + 1))
}

# assay LABEL VERB FILE: run the binary.  The exit code is its exit code.
assay() {
  local label=$1 code=0
  shift
  bash "$kit/run.sh" "$label" "$@" < /dev/null > "$work/$label.run" 2>&1 || code=$?
  perl -ne 'print "     $_" if /shell_exit=/' "$work/$label.run"
  return "$code"
}

check_ok() {
  if assay "$1" check "$2"; then
    pass "$1: check"
  else
    fail "$1: check (log $work/$1.run)"
  fi
}

axioms_empty() {
  if assay "$1" axioms "$2" && [ -z "$(tr -d '[:space:]' < "$work/logs/$1.out")" ]; then
    pass "$1: axioms empty"
  else
    fail "$1: axioms empty (log $work/$1.run)"
  fi
}

# expect_refuse LABEL TEXT WANT: refuse.sh refuses TEXT with a line that holds WANT.
expect_refuse() {
  local f=$work/refuse-$1.asy out code=0
  printf '%s\n' "$2" > "$f"
  out=$(bash "$kit/refuse.sh" --domain "$refusal_domain" "$f" 2>&1) || code=$?
  if [ "$code" -eq 1 ] && [[ $out == *"$3"* ]]; then
    pass "refuse.sh refuses $1"
  else
    fail "refuse.sh refuses $1 (exit $code: $out)"
  fi
}

expect_accept() {
  local out code=0
  out=$(bash "$kit/refuse.sh" "$2" 2>&1) || code=$?
  if [ "$code" -eq 0 ]; then
    pass "refuse.sh accepts $1"
  else
    fail "refuse.sh accepts $1 (exit $code: $out)"
  fi
}

# expect_carrier_refuses LABEL ARGS..: gen/carrier.sh exits 2 and writes nothing.
expect_carrier_refuses() {
  local label=$1 code=0
  shift
  bash "$kit/gen/carrier.sh" "$@" > "$work/carrier-$label.out" 2> /dev/null || code=$?
  if [ "$code" -eq 2 ] && [ ! -s "$work/carrier-$label.out" ]; then
    pass "carrier.sh refuses $label"
  else
    fail "carrier.sh refuses $label (exit $code)"
  fi
}

# expect_assemble_refuses LABEL ARGS..: assemble.sh exits 2 and writes nothing.
expect_assemble_refuses() {
  local label=$1 code=0
  shift
  bash "$kit/assemble.sh" "$@" > "$work/assemble-$label.out" 2> /dev/null || code=$?
  if [ "$code" -eq 2 ] && [ ! -s "$work/assemble-$label.out" ]; then
    pass "assemble.sh refuses $label"
  else
    fail "assemble.sh refuses $label (exit $code)"
  fi
}

printf 'gate: work directory %s\n' "$work"

# 1. Static steps.
for s in assemble.sh refuse.sh run.sh gate.sh gen/carrier.sh; do
  if bash -n "$kit/$s"; then pass "bash -n $s"; else fail "bash -n $s"; fi
done

bash "$kit/gen/carrier.sh" --file "$kit/domain/carriers.txt" > "$work/carriers-1.asy"
bash "$kit/gen/carrier.sh" --file "$kit/domain/carriers.txt" > "$work/carriers-2.asy"
if [ -s "$work/carriers-1.asy" ] && cmp -s "$work/carriers-1.asy" "$work/carriers-2.asy"; then
  pass "carrier.sh output is deterministic"
else
  fail "carrier.sh output is deterministic"
fi
bash "$kit/assemble.sh" "$kit/examples/program.asy" > "$work/program-1.asy"
bash "$kit/assemble.sh" "$kit/examples/program.asy" > "$work/program-2.asy"
if cmp -s "$work/program-1.asy" "$work/program-2.asy"; then
  pass "assemble.sh output is deterministic"
else
  fail "assemble.sh output is deterministic"
fi

expect_carrier_refuses semicolon eq 'Bad;mu' Nat
expect_carrier_refuses paren list Items 'Item) (t : Nat'
expect_carrier_refuses lowercase eq color Color
expect_carrier_refuses kit-name eq Nat Nat
expect_carrier_refuses arity list Items
expect_carrier_refuses kind vec Items Item
printf 'eq Color Color\nlist Items $(touch x)\n' > "$work/bad-carriers.txt"
expect_carrier_refuses file-line --file "$work/bad-carriers.txt"

printf 'limit : Nat := 3\naxiom forged : Prop\n' > "$work/header-axiom"
expect_assemble_refuses header-injection --header "$work/header-axiom"
printf 'limit : Nat := 3 def x : Nat := 4\n' > "$work/header-tail"
expect_assemble_refuses header-tail --header "$work/header-tail"
mkdir -p "$work/no-marker"
perl -ne 'print unless /^-- \@carriers$/' "$kit/domain/Domain.asy" > "$work/no-marker/Domain.asy"
expect_assemble_refuses no-carriers-line --domain "$work/no-marker"
expect_assemble_refuses unknown-part --stop-after nothing

# Refusal probes use a fixed domain so replacing the sample domain or
# removing its optional header does not invalidate these checks.
refusal_domain=$work/refusal-domain
mkdir -p "$refusal_domain"
cat > "$refusal_domain/Domain.asy" <<'EOF'
mu GateColor : Type 0 := | gateRed : GateColor
-- @carriers
def
  gateTotal : Nat := 0
EOF
printf 'list GateItems Nat\n' > "$refusal_domain/carriers.txt"
printf 'gateLimit : Nat := 1\n' > "$refusal_domain/header"

expect_accept examples/program.asy "$kit/examples/program.asy"
expect_accept examples/mutant.asy "$kit/examples/mutant.asy"
printf 'def ok : Nat := 1 -- mu axiom contract sstore\ndef s : Nat := 2\n' > "$work/comment.asy"
expect_accept 'a refused word in a comment' "$work/comment.asy"
expect_refuse mu 'mu Foo : Type 0 :=
  | foo : Foo' 'refused form mu'
expect_refuse nu 'def x : Nat := nu' 'refused form nu'
expect_refuse axiom 'axiom forged : EqNat 1 2' 'refused form axiom'
expect_refuse def-rec 'def rec loop : Nat -> Nat := fun (n : Nat) => loop n' 'refused form def rec'
expect_refuse contract 'contract Bank' 'refused form contract'
expect_refuse storage-sstore 'def x : Nat := storage sstore' 'refused form sstore'
expect_refuse domain-name 'def gateTotal : Nat := 0' 'the def gateTotal redefines'
expect_refuse kit-name 'def reflNat : Nat := 0' 'the def reflNat redefines'
expect_refuse carrier-name 'def foldGateItems : Nat := 0' 'the def foldGateItems redefines'
expect_refuse header-name 'def gateLimit : Nat := 1' 'the def gateLimit redefines'
expect_refuse split-name 'def
  gateTotal : Nat := 0' 'the def gateTotal redefines'
expect_refuse packed-name 'def fresh : Nat := 0 def gateTotal : Nat := 0' 'the def gateTotal redefines'
expect_refuse inline-constructor 'def gateRed : Nat := 0' 'the def gateRed redefines'
expect_refuse split-rec 'def -- recursion
  rec loop : Nat -> Nat := fun (n : Nat) => loop n' 'refused form def rec'

# 2. The kit alone, with no axiom.
cp "$kit/prelude/Kit.asy" "$work/kit.asy"
check_ok kit "$work/kit.asy"
axioms_empty kit-axioms "$work/kit.asy"

# 3. Each carrier kit, in the order of domain/carriers.txt.
bash "$kit/assemble.sh" --stop-after types > "$work/prefix.asy"
i=0
while IFS= read -r line || [ -n "$line" ]; do
  case $line in
    '' | '#'*) continue ;;
    *) ;;
  esac
  i=$((i + 1))
  set -f
  # Word splitting is intended: one carrier line is KIND ARGS.
  set -- $line
  set +f
  bash "$kit/gen/carrier.sh" "$@" < /dev/null >> "$work/prefix.asy"
  cp "$work/prefix.asy" "$work/carrier-$i.asy"
  check_ok "carrier-$i-$1-$2" "$work/carrier-$i.asy"
done < "$kit/domain/carriers.txt"

# 4. The sample domain and program, with no axiom.
check_ok program "$work/program-1.asy"
axioms_empty program-axioms "$work/program-1.asy"

# 5. The mutant: the kernel refuses it with the normal form (P3).
want=$(perl -ne 'print $1 if /^-- expect: (.*)$/' "$kit/examples/mutant.asy")
bash "$kit/assemble.sh" "$kit/examples/mutant.asy" > "$work/mutant.asy"
if assay mutant check "$work/mutant.asy"; then
  fail "mutant: the kernel accepted it"
else
  said=$(cat "$work/logs/mutant.out" "$work/logs/mutant.err")
  if [ -n "$want" ] && [[ $said == *"$want"* ]]; then
    pass "mutant: refused with '$want'"
  else
    fail "mutant: refused without '$want' (log $work/mutant.run)"
  fi
fi

printf 'gate: %s s\n' "$SECONDS"
if [ "$fails" -eq 0 ]; then
  printf 'gate: GREEN\n'
else
  printf 'gate: %s FAIL\n' "$fails"
  exit 1
fi
