#!/usr/bin/env bash
# usage: bash run.sh LABEL VERB ARGS..
# Runs the assay binary with VERB ARGS (for example `check FILE`,
# `axioms FILE`, `emit FILE -o NEWDIR`).
#   ASSAY_BIN      the binary (default /Users/oobi/Documents/assay/_build/bin/assay)
#   ASSAY_DIR      the assay tree for the PIN check (default: two levels
#                  above the directory of ASSAY_BIN)
#   ASSAY_SCRATCH  the scratch directory (default $TMPDIR/lang-template-assay)
#   LINES_OUT      the number of stdout lines to show (default 30)
# The launcher puts NODE_COMPILE_CACHE in the assay tree, so this script
# sets it to ASSAY_SCRATCH/node-cache.  The V8 heap cap is 3 GB.  rss.cjs
# stops the run above 4 GB RSS and prints the wall time and the peak RSS.
# The logs are ASSAY_SCRATCH/logs/LABEL.out and LABEL.err.  When the assay
# HEAD is not PIN, the script writes a warning and continues.  The exit
# code is the exit code of the binary.
set -euo pipefail

kit=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
[ $# -ge 2 ] || {
  printf 'usage: bash run.sh LABEL VERB ARGS..\n' >&2
  exit 2
}
label=$1
shift
[[ $label =~ ^[A-Za-z0-9._-]+$ ]] || {
  printf 'run: LABEL must match ^[A-Za-z0-9._-]+$, not: %s\n' "$label" >&2
  exit 2
}

bin=${ASSAY_BIN:-/Users/oobi/Documents/assay/_build/bin/assay}
[ -x "$bin" ] || {
  printf 'run: no assay binary at %s (set ASSAY_BIN)\n' "$bin" >&2
  exit 2
}
scratch=${ASSAY_SCRATCH:-${TMPDIR:-/tmp}/lang-template-assay}
mkdir -p "$scratch/logs" "$scratch/node-cache"
tree=${ASSAY_DIR:-$(cd "$(dirname "$bin")/../.." && pwd)}
pin=$(tr -d '[:space:]' < "$kit/PIN")
head=$(git -C "$tree" rev-parse HEAD 2>/dev/null || printf 'unknown')
if [ "$head" != "$pin" ]; then
  printf '[%s] warning: assay HEAD %s is not PIN %s\n' "$label" "$head" "$pin" >&2
fi

out=$scratch/logs/$label.out
err=$scratch/logs/$label.err
code=0
# NODE_OPTIONS parses its own quoting after the shell has expanded it.
preload=$kit/rss.cjs
preload=${preload//\\/\\\\}
preload=${preload//\"/\\\"}
env NODE_COMPILE_CACHE="$scratch/node-cache" \
  NODE_OPTIONS="--max-old-space-size=3072 --require \"$preload\"" \
  "$bin" "$@" > "$out" 2> "$err" || code=$?

probe=$(perl -ne 'print if /^\[probe\]/' "$err" | tail -1)
printf '[%s] shell_exit=%s %s\n' "$label" "$code" "$probe"
printf -- '--- stdout (%s lines, first %s)\n' "$(wc -l < "$out" | tr -d ' ')" "${LINES_OUT:-30}"
perl -ne 'BEGIN { $m = shift } chomp; print substr($_, 0, 300), "\n"; last if $. >= $m' "${LINES_OUT:-30}" "$out"
printf -- '--- stderr\n'
perl -ne 'next if /^\[probe\]/; chomp; print substr($_, 0, 400), "\n"; last if ++$k >= 15' "$err"
exit "$code"
