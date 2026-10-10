#!/bin/sh
# The kit gate. `make check` runs it from the kit directory after the build.
set -eu
fail=0
count=0
for prog in examples/*.lang; do
  out=$(build/langc check "$prog" 2>&1) || true
  count=$((count + 1))
  if [ "$out" != "ok" ]; then
    echo "FAIL $prog: $out"
    fail=$((fail + 1))
  fi
done
echo "examples: $count checked, $fail failed"

tmp=${TMPDIR:-/tmp}/langc-gate.$$
mkdir -p "$tmp"
awk 'BEGIN { printf "def deep : Nat := " } { } END {
  for (i = 0; i < 1100; i++) printf "("; printf "1"; for (i = 0; i < 1100; i++) printf ")"; print "" }' </dev/null >"$tmp/deep.lang"

# refusals DIR LABEL: each line of DIR/expect.txt is `FILE CODE`. `langc check`
# must exit 1 and print `langc: CODE: ...` on stderr.
refusals() {
  checked=0
  while read -r file code; do
    path="$1/$file"
    [ "$file" = "deep.lang" ] && path="$tmp/deep.lang"
    status=0
    build/langc check "$path" 2>"$tmp/err" >/dev/null || status=$?
    first=$(head -n 1 "$tmp/err")
    checked=$((checked + 1))
    case "$status:$first" in
      "1:langc: $code: "*) ;;
      *) echo "FAIL $file: want exit 1 and $code, got exit $status: $first"; fail=$((fail + 1)) ;;
    esac
  done <"$1/expect.txt"
  echo "$2 refusals: $checked checked"
}
refusals test/parse parse
refusals test/check check

# A refl refusal shows both normal forms.
build/langc check test/check/bad-refl.lang 2>"$tmp/err" >/dev/null || true
if ! rg -q -F 'the left side normalizes to 1201, the right side normalizes to 1200' "$tmp/err"; then
  echo "FAIL bad-refl.lang: the message does not show both normal forms"
  fail=$((fail + 1))
fi

# Each line of test/eval/expect.txt is `FILE NAME [ARGS] => OUTPUT`. `langc eval`
# must exit 0 and print OUTPUT. A trap also prints `langc: EVAL_OVERFLOW: NAME: ...`
# on stderr.
evals=0
while IFS= read -r line; do
  want=${line#* => }
  set -- ${line%% => *}
  file=$1
  name=$2
  shift 2
  status=0
  got=$(build/langc eval "examples/$file.lang" "$name" "$@" 2>"$tmp/err") || status=$?
  first=$(head -n 1 "$tmp/err")
  want_err=""
  [ "$want" = "trap" ] && want_err="langc: EVAL_OVERFLOW: $name: "
  evals=$((evals + 1))
  case "$status|$got|$first" in
    "0|$want|$want_err"*) ;;
    *) echo "FAIL eval $file $name: want $want, got exit $status: $got $first"; fail=$((fail + 1)) ;;
  esac
done <test/eval/expect.txt
echo "evals: $evals checked"

# eval_refused STATUS CODE NAME [ARGS]: `langc eval examples/entries.lang NAME
# ARGS` must exit STATUS and print `langc: CODE: NAME: ...` on stderr.
eval_refused() {
  want_status=$1
  code=$2
  shift 2
  status=0
  build/langc eval examples/entries.lang "$@" 2>"$tmp/err" >/dev/null || status=$?
  case "$status:$(head -n 1 "$tmp/err")" in
    "$want_status:langc: $code: $1: "*) ;;
    *) echo "FAIL eval $*: want exit $want_status and $code, got exit $status"; fail=$((fail + 1)) ;;
  esac
}
eval_refused 2 EVAL_ARGS addPrice 1
eval_refused 2 EVAL_ARGS addPrice x 1
eval_refused 2 EVAL_ARGS addPrice 18446744073709551616 1
eval_refused 2 EVAL_ARGS pick 2 4 9
eval_refused 1 EVAL_ENTRY nope

status=0
build/langc 2>/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL usage: want exit 2, got $status"; fail=$((fail + 1)); }
status=0
build/langc check "$tmp/missing.lang" 2>/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL missing file: want exit 2, got $status"; fail=$((fail + 1)); }
# JSON: `langc build` of each example must equal test/json/NAME.json byte for
# byte, and each golden must parse as JSON.
jsons=0
for prog in examples/*.lang; do
  name=$(basename "$prog" .lang)
  jsons=$((jsons + 1))
  if ! build/langc build "$prog" >"$tmp/out.json" 2>"$tmp/err" || ! cmp -s "$tmp/out.json" "test/json/$name.json"; then
    echo "FAIL build $prog: the output differs from test/json/$name.json"
    fail=$((fail + 1))
  fi
done
if ! node -e 'for (const f of process.argv.slice(1)) JSON.parse(require("fs").readFileSync(f, "utf8"))' test/json/*.json; then
  echo "FAIL: a JSON golden does not parse"
  fail=$((fail + 1))
fi
echo "json: $jsons builds checked"

# Each line of test/emit/expect.txt is `FILE CODE`. `langc build` must exit 1,
# print `langc: CODE: ...` on stderr and print nothing on stdout.
while read -r file code; do
  status=0
  build/langc build "test/emit/$file" >"$tmp/out.json" 2>"$tmp/err" || status=$?
  case "$status:$(head -n 1 "$tmp/err"):$(wc -c <"$tmp/out.json" | tr -d ' ')" in
    "1:langc: $code: "*":0") ;;
    *) echo "FAIL build $file: want exit 1, $code and no stdout, got exit $status"; fail=$((fail + 1)) ;;
  esac
done <test/emit/expect.txt
echo "build refusals: $(wc -l <test/emit/expect.txt | tr -d ' ') checked"

# -o writes the same document; a refused build leaves no file.
build/langc build examples/formers.lang -o "$tmp/o.json" && cmp -s "$tmp/o.json" test/json/formers.json \
  || { echo "FAIL build -o: the file differs"; fail=$((fail + 1)); }
build/langc build test/emit/trap.lang -o "$tmp/no.json" 2>/dev/null || true
[ ! -e "$tmp/no.json" ] || { echo "FAIL build -o: a refused build wrote a file"; fail=$((fail + 1)); }
status=0
build/langc build examples/formers.lang -x 2>/dev/null >/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL build usage: want exit 2, got $status"; fail=$((fail + 1)); }

# JS: `langc build --js --selftest` of each example writes a module. node runs
# it (the selftest compares both sides of each Eq-typed definition), and
# encode(document) must equal test/json/NAME.json byte for byte.
modules=0
for prog in examples/*.lang; do
  name=$(basename "$prog" .lang)
  modules=$((modules + 1))
  if ! build/langc build "$prog" --js "$tmp/$name.js" --selftest 2>"$tmp/err" || ! node "$tmp/$name.js" 2>"$tmp/err"; then
    echo "FAIL js $prog: $(head -n 1 "$tmp/err")"
    fail=$((fail + 1))
    continue
  fi
  if ! node --input-type=module -e 'const m = await import(process.argv[1]); process.stdout.write(m.encode(m.document));' \
    "$tmp/$name.js" >"$tmp/js.json" 2>"$tmp/err" || ! cmp -s "$tmp/js.json" "test/json/$name.json"; then
    echo "FAIL js $prog: encode(document) differs from test/json/$name.json"
    fail=$((fail + 1))
  fi
done
echo "js: $modules modules checked"

# A module is ASCII, has no trailing blank, ends in one newline, and the same
# build gives the same bytes.
if rg -n '(?-u:[\x80-\xff])|[ \t]$' "$tmp"/*.js >/dev/null; then
  echo "FAIL js: a module has a byte above 0x7f or a trailing blank"
  fail=$((fail + 1))
fi
for f in "$tmp"/*.js; do
  end=$(tail -c 2 "$f" | od -An -tx1 | tr -d ' \n')
  [ "${end#??}" = "0a" ] && [ "$end" != "0a0a" ] || { echo "FAIL js $f: not one final newline"; fail=$((fail + 1)); }
done
build/langc build examples/laws.lang --js "$tmp/again.js" --selftest && cmp -s "$tmp/again.js" "$tmp/laws.js" \
  || { echo "FAIL js: two builds of laws.lang differ"; fail=$((fail + 1)); }

# The entries agree with `langc eval` on each line of test/eval/expect.txt: the
# same Nat or Flag, or EVAL_OVERFLOW where eval traps. A name that is not an
# entry (eval prints its normal form) is skipped.
js_evals=0
js_skips=0
while IFS= read -r line; do
  want=${line#* => }
  set -- ${line%% => *}
  file=$1
  shift
  got=$(node --input-type=module -e '
const [path, name, ...args] = process.argv.slice(1);
const m = await import(path);
let out = "skip";
if (Object.hasOwn(m.entries, name)) {
  const f = m.entries[name];
  out = "trap";
  try {
    const r = f(...args.map((a, i) => (f.params[i] === "F" ? a === "1" : BigInt(a))));
    out = typeof r === "boolean" ? (r ? "1" : "0") : String(r);
  } catch (e) {
    if (!String(e.message).startsWith("EVAL_OVERFLOW: " + name + ": ")) throw e;
  }
}
process.stdout.write(out);' "$tmp/$file.js" "$@" 2>"$tmp/err") || got="error: $(head -n 1 "$tmp/err")"
  case "$got" in
    skip) js_skips=$((js_skips + 1)) ;;
    "$want") js_evals=$((js_evals + 1)) ;;
    *) echo "FAIL js eval $file $1: want $want, got $got"; fail=$((fail + 1)) ;;
  esac
done <test/eval/expect.txt
echo "js evals: $js_evals checked, $js_skips not entries"

# js_refused STATUS CODE PROG FLAGS...: `langc build PROG FLAGS` must exit
# STATUS, print `langc: CODE: ...` on stderr, and write no file and no stdout.
js_refusals=0
js_refused() {
  want_status=$1
  code=$2
  shift 2
  rm -f "$tmp/no.js" "$tmp/no.json"
  status=0
  build/langc build "$@" >"$tmp/out" 2>"$tmp/err" || status=$?
  js_refusals=$((js_refusals + 1))
  case "$status:$(head -n 1 "$tmp/err")" in
    "$want_status:langc: $code: "*) ;;
    *) echo "FAIL js build $*: want exit $want_status and $code, got exit $status"; fail=$((fail + 1)); return 0 ;;
  esac
  if [ -e "$tmp/no.js" ] || [ -e "$tmp/no.json" ] || [ -s "$tmp/out" ]; then
    echo "FAIL js build $*: a refused build wrote output"
    fail=$((fail + 1))
  fi
}
while read -r file code; do
  js_refused 1 "$code" "test/emit/$file" --js "$tmp/no.js" --selftest --json "$tmp/no.json"
done <test/emit/expect.txt
printf 'def bad : Str := "a\377b"\n' >"$tmp/utf8.lang"
js_refused 1 JS_UTF8 "$tmp/utf8.lang" --json "$tmp/no.json" --js "$tmp/no.js"
js_refused 2 USAGE examples/formers.lang --selftest
js_refused 2 USAGE examples/formers.lang --json "$tmp/no.json" --selftest
js_refused 2 USAGE examples/formers.lang --js
js_refused 2 USAGE examples/formers.lang --js "$tmp/no.js" --js "$tmp/no.js"
js_refused 2 USAGE examples/formers.lang --js "$tmp/no.js" --selftest --selftest
js_refused 2 USAGE examples/formers.lang -o "$tmp/no.json" --js "$tmp/no.js"
js_refused 2 USAGE examples/formers.lang --js "$tmp/no.js" --wasm "$tmp/no.json"
echo "js build refusals: $js_refusals checked"

# --json writes the document; --js with --json writes both; stdout stays empty.
build/langc build examples/formers.lang --json "$tmp/a.json" >"$tmp/out" && cmp -s "$tmp/a.json" test/json/formers.json \
  && [ ! -s "$tmp/out" ] || { echo "FAIL build --json: the file differs"; fail=$((fail + 1)); }
build/langc build examples/formers.lang --json "$tmp/b.json" --js "$tmp/b.js" >"$tmp/out" && cmp -s "$tmp/b.json" test/json/formers.json \
  && [ -s "$tmp/b.js" ] && [ ! -s "$tmp/out" ] || { echo "FAIL build --js --json: a file is missing or differs"; fail=$((fail + 1)); }

rm -rf "$tmp"

if rg -n '\x{2013}|\x{2014}' . >/dev/null; then
  echo "FAIL: an em-dash or en-dash is in the kit"
  fail=$((fail + 1))
fi
echo "gate: $fail failures"
[ "$fail" -eq 0 ]
