#!/usr/bin/env bash
# usage: bash assemble.sh [--domain DIR] [--header FILE] [--stop-after PART] [PROGRAM]
# Writes one assay file to stdout, in the order that the kernel needs (a
# name is defined before its first use):
#   1. the header: one `def NAME : Nat := N` for each line of the header
#      file (default DIR/header, if it exists)
#   2. prelude/Kit.asy
#   3. the domain types: DIR/Domain.asy above its `-- @carriers` line
#   4. the carrier kits of DIR/carriers.txt (gen/carrier.sh)
#   5. the domain operations: DIR/Domain.asy below its `-- @carriers` line
#   6. PROGRAM
# DIR is domain/ in this kit by default.  PART is kit, types, carriers or
# domain: the output stops after that part.  A header line must match
# `NAME : Nat := N` (NAME ^[a-z][A-Za-z0-9]*$, N a literal), so a header
# cannot inject a definition.  On a refusal the script writes nothing to
# stdout and exits 2.
set -euo pipefail

kit=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
domain=$kit/domain
header=
header_set=0
stop=all
program=

refuse() {
  printf 'assemble: %s\n' "$1" >&2
  exit 2
}

while [ $# -gt 0 ]; do
  case $1 in
    --domain)
      [ $# -ge 2 ] || refuse "--domain needs a directory"
      domain=$2
      shift 2
      ;;
    --header)
      [ $# -ge 2 ] || refuse "--header needs a file"
      header=$2
      header_set=1
      shift 2
      ;;
    --stop-after)
      [ $# -ge 2 ] || refuse "--stop-after needs a part"
      stop=$2
      shift 2
      ;;
    -*)
      refuse "unknown option '$1'"
      ;;
    *)
      [ -z "$program" ] || refuse "give one PROGRAM only"
      program=$1
      shift
      ;;
  esac
done

case $stop in
  kit | types | carriers | domain | all) ;;
  *) refuse "unknown part '$stop': use kit, types, carriers or domain" ;;
esac

if [ "$header_set" -eq 0 ] && [ -f "$domain/header" ]; then
  header=$domain/header
fi
dom=$domain/Domain.asy
[ -f "$dom" ] || refuse "no domain file $dom"
marks=$(awk '$0 == "-- @carriers" { n++ } END { print n + 0 }' "$dom")
[ "$marks" -eq 1 ] || refuse "$dom must hold one '-- @carriers' line, not $marks"
if [ -n "$program" ]; then
  [ -f "$program" ] || refuse "no program file $program"
fi

out=$(mktemp "${TMPDIR:-/tmp}/assemble.XXXXXX")
trap 'rm -f "$out"' EXIT
finish() {
  cat "$out"
  exit 0
}

re='^([a-z][A-Za-z0-9]*) : Nat := ([0-9]+)$'
if [ -n "$header" ]; then
  [ -f "$header" ] || refuse "no header file $header"
  n=0
  printf -- '-- @section header\n' >> "$out"
  while IFS= read -r line || [ -n "$line" ]; do
    n=$((n + 1))
    case $line in
      '' | '#'*) continue ;;
      *) ;;
    esac
    [[ $line =~ $re ]] || refuse "$header:$n: a header line is 'NAME : Nat := N', not: $line"
    printf 'def %s : Nat := %s\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" >> "$out"
  done < "$header"
  printf '\n' >> "$out"
fi

cat "$kit/prelude/Kit.asy" >> "$out"
if [ "$stop" = kit ]; then finish; fi

printf '\n' >> "$out"
awk '$0 == "-- @carriers" { exit } { print }' "$dom" >> "$out"
if [ "$stop" = types ]; then finish; fi

if [ -f "$domain/carriers.txt" ]; then
  printf '\n-- @section carriers (gen/carrier.sh)\n' >> "$out"
  bash "$kit/gen/carrier.sh" --file "$domain/carriers.txt" >> "$out"
fi
if [ "$stop" = carriers ]; then finish; fi

awk 'f { print } $0 == "-- @carriers" { f = 1 }' "$dom" >> "$out"
if [ "$stop" = domain ]; then finish; fi

if [ -n "$program" ]; then
  printf '\n-- @section program\n' >> "$out"
  cat "$program" >> "$out"
fi
finish
