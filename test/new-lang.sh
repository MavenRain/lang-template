#!/usr/bin/env bash
# JSON hosts must refuse a language name that collides with the instances key.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/new-lang-test.XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
for host in tcc-json tcc-js tcc-media; do
  status=0
  bash "$root/bin/new-lang.sh" instances "$host" "$tmp/$host" >"$tmp/out" 2>"$tmp/err" || status=$?
  if [[ $status != 1 || -e $tmp/$host ]] || ! rg -q -F "NAME 'instances' is reserved" "$tmp/err"; then
    printf 'FAIL new-lang %s: expected the reserved-name refusal and no destination\n' "$host" >&2
    cat "$tmp/err" >&2
    exit 1
  fi
done
printf 'new-lang: 3 reserved-name refusals checked\n'
