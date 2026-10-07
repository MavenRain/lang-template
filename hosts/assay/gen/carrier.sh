#!/usr/bin/env bash
# usage: bash gen/carrier.sh eq NAME TYPE
#        bash gen/carrier.sh list NAME ELEM
#        bash gen/carrier.sh bridge FROM FROMTYPE TO TOTYPE
#        bash gen/carrier.sh --file CARRIERS
# Writes the assay kit of one carrier, or of each line of CARRIERS, to
# stdout.  The host has no parametric `mu` (host CAPABILITY.md, P2), so
# each Eq family and each list family is one generated copy of a template.
#   eq NAME TYPE    EqNAME on TYPE, with reflNAME, transportNAME, symmNAME,
#                   transNAME and congNAME (escrow-lang EqDec template).
#   list NAME ELEM  the list family NAME of ELEM, with nilNAME, consNAME,
#                   foldNAME, appendNAME, pureNAME, mapNAME, bindNAME and
#                   filterNAME (escrow-lang Ballots template).
#   bridge FROM FROMTYPE TO TOTYPE
#                   congFROMTO: a function FROMTYPE -> TOTYPE takes
#                   EqFROM x y to EqTO (f x) (f y) (escrow-lang congND).
# Each name and type must match ^[A-Z][A-Za-z0-9]+$.  In CARRIERS, an
# empty line or a line that starts with `#` is skipped.  The output is the
# same for the same input.  On a refusal the script writes nothing to
# stdout and exits 2.
set -euo pipefail

where=
refuse() {
  printf 'carrier: %s%s\n' "$where" "$1" >&2
  exit 2
}

ident='^[A-Z][A-Za-z0-9]+$'
guard() {
  [[ $1 =~ $ident ]] || refuse "'$1' is not a name: a name matches ^[A-Z][A-Za-z0-9]+\$"
}

eq_kit() {
  local n=$1 t=$2
  cat <<EOF
-- @carrier eq $n $t
mu Eq$n : (0 a : $t) -> (0 b : $t) -> Type 0 :=
  | refl$n : (0 x : $t) -> Eq$n x x

def transport$n : (0 P : $t -> Type 0) -> (0 x : $t) -> (0 y : $t) -> Eq$n x y -> P x -> P y :=
  fun (0 P : $t -> Type 0) (0 x : $t) (0 y : $t) (e : Eq$n x y) =>
    match e as q in Eq$n i j return P i -> P j with
    | refl$n 0 z => fun (p : P z) => p

def symm$n : (0 x : $t) -> (0 y : $t) -> Eq$n x y -> Eq$n y x :=
  fun (0 x : $t) (0 y : $t) (e : Eq$n x y) =>
    match e as q in Eq$n i j return Eq$n j i with
    | refl$n 0 z => refl$n z

def trans$n : (0 x : $t) -> (0 y : $t) -> (0 z : $t) -> Eq$n x y -> Eq$n y z -> Eq$n x z :=
  fun (0 x : $t) (0 y : $t) (0 z : $t) (e1 : Eq$n x y) (e2 : Eq$n y z) =>
    transport$n (fun (w : $t) => Eq$n x w) y z e2 e1

def cong$n : (f : $t -> $t) -> (0 x : $t) -> (0 y : $t) -> Eq$n x y -> Eq$n (f x) (f y) :=
  fun (f : $t -> $t) (0 x : $t) (0 y : $t) (e : Eq$n x y) =>
    match e as q in Eq$n i j return Eq$n (f i) (f j) with
    | refl$n 0 z => refl$n (f z)

EOF
}

list_kit() {
  local n=$1 e=$2
  cat <<EOF
-- @carrier list $n $e
mu $n : Type 0 :=
  | nil$n : $n
  | cons$n (h : $e) (t : $n) : $n

def rec fold$n : (0 B : Type 0) -> ($e -> B -> B) -> B -> $n -> B :=
  fun (0 B : Type 0) (f : $e -> B -> B) (z : B) (xs : $n) =>
    match xs as w in $n return B with
    | nil$n => z
    | cons$n h t => f h (fold$n B f z t)

def append$n : $n -> $n -> $n :=
  fun (xs : $n) (ys : $n) =>
    fold$n $n (fun (h : $e) (t : $n) => cons$n h t) ys xs

def pure$n : $e -> $n := fun (a : $e) => cons$n a nil$n

def map$n : ($e -> $e) -> $n -> $n :=
  fun (f : $e -> $e) (xs : $n) =>
    fold$n $n (fun (h : $e) (t : $n) => cons$n (f h) t) nil$n xs

def bind$n : $n -> ($e -> $n) -> $n :=
  fun (xs : $n) (f : $e -> $n) =>
    fold$n $n (fun (h : $e) (t : $n) => append$n (f h) t) nil$n xs

def filter$n : ($e -> Option (prod ())) -> $n -> $n :=
  fun (p : $e -> Option (prod ())) (xs : $n) =>
    fold$n $n
      (fun (h : $e) (t : $n) =>
        case p h with
        | 0 (u : prod ()) => t
        | 1 (u : prod ()) => cons$n h t)
      nil$n xs

EOF
}

bridge_kit() {
  local from=$1 ft=$2 to=$3 tt=$4
  cat <<EOF
-- @carrier bridge $from $ft $to $tt
def cong$from$to : (f : $ft -> $tt) -> (0 x : $ft) -> (0 y : $ft) -> Eq$from x y -> Eq$to (f x) (f y) :=
  fun (f : $ft -> $tt) (0 x : $ft) (0 y : $ft) (e : Eq$from x y) =>
    match e as q in Eq$from i j return Eq$to (f i) (f j) with
    | refl$from 0 z => refl$to (f z)

EOF
}

emit() {
  [ $# -ge 1 ] || refuse "a carrier line needs a kind: eq, list or bridge"
  case $1 in
    eq)
      [ $# -eq 3 ] || refuse "usage: eq NAME TYPE"
      guard "$2"
      guard "$3"
      [ "$2" != Nat ] || refuse "eq Nat: EqNat is in prelude/Kit.asy"
      eq_kit "$2" "$3"
      ;;
    list)
      [ $# -eq 3 ] || refuse "usage: list NAME ELEM"
      guard "$2"
      guard "$3"
      case $2 in
        Type | Prop | Univ | Nat | Unit | Option | Sum | Le) refuse "list $2: '$2' is a kernel or prelude name" ;;
        *) ;;
      esac
      list_kit "$2" "$3"
      ;;
    bridge)
      [ $# -eq 5 ] || refuse "usage: bridge FROM FROMTYPE TO TOTYPE"
      guard "$2"
      guard "$3"
      guard "$4"
      guard "$5"
      bridge_kit "$2" "$3" "$4" "$5"
      ;;
    *)
      refuse "unknown kind '$1': use eq, list or bridge"
      ;;
  esac
}

from_file() {
  local file=$1 line n=0
  [ -f "$file" ] || refuse "no file $file"
  while IFS= read -r line || [ -n "$line" ]; do
    n=$((n + 1))
    case $line in
      '' | '#'*) continue ;;
      *) ;;
    esac
    where="$file:$n: "
    set -f
    # Word splitting is intended: one carrier line is KIND ARGS.
    emit $line
    set +f
  done < "$file"
  where=
}

[ $# -ge 1 ] || refuse "usage: carrier.sh eq|list|bridge ARGS, or carrier.sh --file CARRIERS"
if [ "$1" = --file ]; then
  [ $# -eq 2 ] || refuse "usage: carrier.sh --file CARRIERS"
  text=$(from_file "$2")
else
  text=$(emit "$@")
fi
printf '%s\n' "$text"
