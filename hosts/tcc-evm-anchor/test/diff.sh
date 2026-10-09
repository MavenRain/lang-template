#!/bin/sh
# Differential tests of langc (SPEC section 10, chunks 6 and 12), run by
# make test after make. Six fixed traces of calls run on geth evm, each
# from a deploy: one evm run for each call, on the storage after the last
# call. A model in awk predicts each call from the langc table (the
# members, the candidates, the constitutions, the admit, the schema, the window and the
# amendTo mask of each policy, the outcome of each tally under each
# constitution) and the slot rules of SPEC section 7: the result (a revert
# or the output word), the log and the full storage after the call. Each
# case compares the prediction with the evm result. The model reads the
# table of langc, so these tests check the contract against the table;
# test/table.sh checks the table. With no evm on the PATH, the tests are
# skipped.
root=$(cd "$(dirname "$0")/.." && pwd)
if ! command -v evm > /dev/null 2>&1; then
  echo 'diff.sh: no evm on the PATH, skipped'
  exit 0
fi
out=$root/build/test/chain/diff
. "$root/test/evmchain.sh"

topic=$(keccak "$(text 'Anchored(bytes32,uint256)')")
amended=$(keccak "$(text 'Amended(uint256)')")
disputed=$(keccak "$(text 'Disputed(bytes32,uint256,bytes32)')")
h1=$(keccak "$(text 'anchor-lang diff h1')")
h2=$(keccak "$(text 'anchor-lang diff h2')")
h3=$(keccak "$(text 'anchor-lang diff h3')")
h4=$(keccak "$(text 'anchor-lang diff h4')")

addr() {
  case $1 in
    m0) member 0 ;;
    m1) member 1 ;;
    m2) member 2 ;;
    x) printf '%s' "$stranger" ;;
  esac
}

hash() {
  case $1 in
    h1) printf '%s' "$h1" ;;
    h2) printf '%s' "$h2" ;;
    h3) printf '%s' "$h3" ;;
    h4) printf '%s' "$h4" ;;
    0) printf '%s' "$zero" ;;
  esac
}

# pairkey H TW: keys gets the line pair H TW KEY, KEY the pair slot
# keccak256(H . TW), if keys does not have it.
pairkey() {
  if ! awk -v h="$1" -v t="$2" '$1 == "pair" && $2 == h && $3 == t { f = 1 } END { exit !f }' "$out/keys"; then
    _key=$(keccak "$1$2") || fatal "cannot hash pair $1 $2"
    printf 'pair %s %s %s\n' "$1" "$2" "$_key" >> "$out/keys"
  fi
}

# The model of one call (SPEC section 7). anchor h by s at t reverts when
# s is not a member, h is 0, the outcome of the current tally is not one,
# or the admit of its policy is deny; else it returns t, and sets the pair
# slot and writes the log when the pair is new. verify returns 1 when the
# pair slot is set, else 0. cast c by s reverts when s is not a member or
# c is not below K; it moves the ballot of s to c, and reverts when the
# tallies before and after are both one and the schema goes down (O6).
# Each tally is the tally under the current constitution c, slot K + M (0
# for a program with one constitution). amend k by s reverts when s is not
# a member or the program has one constitution; it changes nothing when k
# is c; it reverts when k is not below C, the outcome of the current tally
# under c is not one, bit k of the amendTo mask of its policy is 0, or the
# tallies under c and under k are both one and the schema goes down (O6);
# else slot K + M becomes k and the log Amended gives k. dispute h t n by s
# reverts when no policy has a window > 0, s is not a member, the pair slot
# of (h, t) is 0, the outcome of the current tally is not one, or the time
# is not less than t + the window of its policy; else the log Disputed gives
# h and t . n, and the storage does not change (O7). Other call data
# reverts.
model_awk='
function num(x,   n, j) {
  n = 0
  for (j = 1; j <= length(x); j++) n = n * 16 + index("0123456789abcdef", substr(x, j, 1)) - 1
  return n
}
function key(n) { return sprintf("%064x", n) }
function get(n) { return (key(n) in st) ? num(st[key(n)]) : 0 }
function put(n, v) { if (v == 0) delete st[key(n)]; else st[key(n)] = sprintf("%064x", v) }
function row(   j, r) { r = ""; for (j = 0; j < K; j++) r = r " " get(j); return r }
BEGIN { C = 1; cur = 0; hasw = 0 }
FILENAME == tablef && $1 == "members" { M = $2 + 0 }
FILENAME == tablef && $1 == "candidates" { K = $2 + 0 }
FILENAME == tablef && $1 == "constitutions" { C = $2 + 0 }
FILENAME == tablef && $1 == "policy" { admit[$2] = $4; schema[$2] = $8 + 0; window[$2] = $7 + 0; if ($7 + 0 > 0) hasw = 1 }
FILENAME == tablef && $1 == "amendTo" { mask[$2] = $3 }
FILENAME == tablef && $1 == "constitution" { cur = $2 + 0 }
FILENAME == tablef && $1 == "tally" {
  r = cur
  for (j = 2; $j != ":"; j++) r = r " " $j
  fate[r] = $(j + 1)
  pol[r] = $(j + 2)
}
FILENAME == keysf && $1 == "member" { pos[$2] = $3 + 0 }
FILENAME == keysf && $1 == "pair" { pk[$2 " " $3] = $4 }
FILENAME == statef { st[$1] = $2 }
END {
  printf "" > nextf
  tw = sprintf("%064x", time)
  result = "revert"
  logline = ""
  con = get(K + M)
  if (kind == "anchor") {
    r = con row()
    if ((sender in pos) && a != zero && fate[r] == "one" && admit[pol[r]] == "allow") {
      result = "0x" tw
      k = pk[a " " tw]
      if (!(k in st)) { st[k] = sprintf("%064x", 1); logline = "log " topic " " a " " tw }
    }
  } else if (kind == "verify") {
    k = ((a " " b) in pk) ? pk[a " " b] : ""
    result = "0x" sprintf("%064x", (k != "" && (k in st)) ? 1 : 0)
  } else if (kind == "cast") {
    if ((sender in pos) && a + 0 < K) {
      i = pos[sender]
      old = get(K + i)
      c = a + 0
      before = con row()
      put(old, get(old) - 1)
      put(c, get(c) + 1)
      put(K + i, c)
      after = con row()
      if (fate[before] == "one" && fate[after] == "one" && schema[pol[after]] < schema[pol[before]]) {
        put(c, get(c) - 1)
        put(old, get(old) + 1)
        put(K + i, old)
      } else result = "0x"
    }
  } else if (kind == "amend") {
    k = a + 0
    r = row()
    if ((sender in pos) && C > 1) {
      if (k == con) result = "0x"
      else if (k < C && fate[con r] == "one" && substr(mask[pol[con r]], k + 1, 1) == "1") {
        if (!(fate[k r] == "one" && schema[pol[k r]] < schema[pol[con r]])) {
          put(K + M, k)
          result = "0x"
          logline = "log " amended " " sprintf("%064x", k)
        }
      }
    }
  } else if (kind == "dispute") {
    r = con row()
    k = ((a " " b) in pk) ? pk[a " " b] : ""
    if (hasw && (sender in pos) && k != "" && (k in st) && fate[r] == "one" && time + 0 < num(b) + window[pol[r]]) {
      result = "0x"
      logline = "log " disputed " " a " " b nt
    }
  }
  print "result " result
  if (logline != "") print logline
  for (k in st) print k " " st[k] > nextf
  close(nextf)
}'

# model KIND SENDER TIME A B [N]: want gets the prediction of the model for one
# call on the storage in model; model gets the storage after the call.
model() {
  awk -v kind="$1" -v sender="$2" -v time="$3" -v a="$4" -v b="$5" -v topic="$topic" -v amended="$amended" -v disputed="$disputed" -v nt="$6" -v zero="$zero" \
    -v tablef="$out/table.out" -v keysf="$out/keys" -v statef="$out/model" -v nextf="$out/model.next" \
    "$model_awk" "$out/table.out" "$out/keys" "$out/model" > "$out/want"
  sort "$out/model.next" > "$out/model"
  cat "$out/model" >> "$out/want"
}

# trace NAME FILE: deploy FILE with M members; the model starts at the
# storage of a deploy.
trace() {
  logs=0 reverts=0
  program "$2"
  members "$M"
  create "$(tr -d '\n' < "$out/creation.hex")$(words "$M")"
  fresh "$M" > "$out/model"
  if [ "$created" = ok ] && cmp -s "$out/model" "$out/state"; then
    pass "trace $1: the deploy gives the start storage of the model"
  else
    fail "trace $1: the deploy gives $created, storage $(tr '\n' ' ' < "$out/state")"
  fi
}

# call LABEL TIME SENDER KIND [ARG [ARG]]: one call of a trace. KIND is
# anchor h, verify h t, cast c, amend k, dispute h t n or raw HEX. The case passes when the model
# and evm give the same result, log and storage.
call() {
  _s=$(addr "$3")
  _tw=$(printf '%064x' "$2")
  _a='' _b='' _c=''
  case $4 in
    anchor)
      _a=$(hash "$5")
      _in=$(lang_in "$_a")
      if [ "$_a" != "$zero" ]; then pairkey "$_a" "$_tw"; fi ;;
    verify)
      _a=$(hash "$5")
      _b=$(printf '%064x' "$6")
      _in=$(verify_in "$_a" "$6") ;;
    cast)
      _a=$5
      _in=$(cast_in "$5") ;;
    amend)
      _a=$5
      _in=$(amend_in "$5") ;;
    dispute)
      _a=$(hash "$5")
      _b=$(printf '%064x' "$6")
      _c=$(keccak "$(text "$7")")
      _in=$(dispute_in "$_a" "$6" "$_c")
      pairkey "$_a" "$_b" ;;
    raw)
      _in=$5 ;;
  esac
  model "$4" "$_s" "$2" "$_a" "$_b" "$_c"
  logs=$((logs + $(awk '$1 == "log" { n++ } END { print n + 0 }' "$out/want")))
  reverts=$((reverts + $(awk '$0 == "result revert" { n++ } END { print n + 0 }' "$out/want")))
  step "$_s" "$_in" "$2"
  _name="trace $1: $4 $5${6:+ $6}${7:+ $7} by $3 at $2"
  if cmp -s "$out/want" "$out/got"; then
    pass "$_name"
  else
    fail "$_name: $(diff "$out/want" "$out/got" | head -n 4 | tr '\n' ' ')"
  fi
}

# summary NAME LOGS REVERTS PAIRS: the model predicts this many logs and
# reverts over the trace, and this many pairs at the end.
summary() {
  _p=$(awk 'NR == FNR { if ($1 == "pair") p[$4] = 1; next } ($1 in p) { n++ } END { print n + 0 }' "$out/keys" "$out/model")
  same "trace $1: the model predicts $2 logs, $3 reverts and $4 pairs" "$logs $reverts $_p" "$2 $3 $4"
}

# Trace A: arrow-debreu (M 3, K 2). Tallies 3 0 and 2 1 are one 0 (allow),
# 1 2 and 0 3 are one 1 (deny), both of schema 1.
trace A "$programs/arrow-debreu.lang"
call 'A 1' 4660 m0 anchor h1
call 'A 2' 4660 m1 anchor h1
call 'A 3' 4660 m2 anchor h2
call 'A 4' 4660 x anchor h3
call 'A 5' 4660 m0 anchor 0
call 'A 6' 4661 x verify h1 4660
call 'A 7' 4661 m0 verify h1 4661
call 'A 8' 4661 m1 anchor h1
call 'A 9' 4662 m0 cast 1
call 'A 10' 4662 m2 anchor h3
call 'A 11' 4663 m1 cast 1
call 'A 12' 4663 m2 anchor h4
call 'A 13' 4664 x verify h3 4662
call 'A 14' 4664 m1 cast 2
call 'A 15' 4664 x cast 0
call 'A 16' 4665 m1 cast 0
call 'A 17' 4665 m0 cast 1
call 'A 18' 4665 m2 anchor h4
call 'A 19' 4666 m0 raw 00000000
call 'A 20' 4666 m0 verify h4 4665
summary A 5 6 5

# Trace B: arrow-debreu with the schema 2 in closedLog (the awk of
# test/run.sh). Tallies 1 2 and 0 3 are one 1 (deny) of schema 2.
awk '/^def closedLog/ { sub(/ 0 1 flagYes/, " 0 2 flagYes") } { print }' \
  "$programs/arrow-debreu.lang" > "$out/schema-2.lang"
trace B "$out/schema-2.lang"
call 'B 1' 4660 m0 anchor h1
call 'B 2' 4661 m0 cast 1
call 'B 3' 4661 m1 cast 1
call 'B 4' 4662 m1 cast 0
call 'B 5' 4662 m0 anchor h2
call 'B 6' 4663 m2 cast 1
call 'B 7' 4663 m2 cast 0
call 'B 8' 4664 x verify h1 4660
summary B 1 2 1

# Trace C: schelling-ising (M 2). Each tally is two; both policies allow.
trace C "$programs/schelling-ising.lang"
call 'C 1' 4660 m0 anchor h1
call 'C 2' 4660 m0 cast 1
call 'C 3' 4661 m1 anchor h1
call 'C 4' 4661 m1 cast 1
call 'C 5' 4662 m0 anchor h2
call 'C 6' 4662 m1 verify h1 4660
summary C 0 3 0

# Trace D: arrow-impossibility (M 2). Each tally is none.
trace D "$programs/arrow-impossibility.lang"
call 'D 1' 4660 m0 anchor h1
call 'D 2' 4660 m1 cast 1
call 'D 3' 4661 m1 anchor h1
summary D 0 2 0

# Trace E: arrow-debreu-amend (M 3, K 2, C 2) with the schema 2 in
# closedLog. Under constitution 0, tallies 3 0 and 2 1 are one 0 (allow,
# schema 1), 1 2 and 0 3 are one 1 (deny, schema 2). Under constitution 1,
# only 3 0 is one 0. Policy 0 may amend to 0 and 1, policy 1 only to 0.
awk '/^def closedLog/ { sub(/ 0 1 flagYes/, " 0 2 flagYes") } { print }' \
  "$programs/arrow-debreu-amend.lang" > "$out/amend-schema-2.lang"
trace E "$out/amend-schema-2.lang"
call 'E 1' 4660 m0 anchor h1
call 'E 2' 4660 m0 amend 0
call 'E 3' 4661 x amend 1
call 'E 4' 4661 m1 amend 2
call 'E 5' 4661 m1 amend 1
call 'E 6' 4661 m2 amend 1
call 'E 7' 4662 m2 anchor h2
call 'E 8' 4662 m0 cast 1
call 'E 9' 4663 m1 anchor h3
call 'E 10' 4663 m1 amend 0
call 'E 11' 4663 m0 cast 0
call 'E 12' 4664 m1 cast 1
call 'E 13' 4664 m2 amend 0
call 'E 14' 4665 m0 amend 1
call 'E 15' 4665 m0 anchor h4
call 'E 16' 4666 m2 cast 1
call 'E 17' 4666 x verify h1 4660
call 'E 18' 4666 m0 raw 00000000
summary E 4 8 2

# Trace F: arrow-debreu-dispute (M 3, K 2). Tallies 3 0 and 2 1 are one 0
# (allow, window 100), 1 2 and 0 3 are one 1 (deny, window 0). A dispute
# of (h, t) returns while the time is less than t + 100 under policy 0.
trace F "$programs/arrow-debreu-dispute.lang"
call 'F 1' 4660 m0 anchor h1
call 'F 2' 4660 m1 dispute h1 4660 n1
call 'F 3' 4661 x dispute h1 4660 n1
call 'F 4' 4661 m2 dispute h2 4660 n1
call 'F 5' 4759 m0 dispute h1 4660 n2
call 'F 6' 4760 m0 dispute h1 4660 n2
call 'F 7' 4760 m1 anchor h2
call 'F 8' 4761 m0 cast 1
call 'F 9' 4761 m1 cast 1
call 'F 10' 4762 m2 dispute h2 4760 n1
call 'F 11' 4762 m0 cast 0
call 'F 12' 4763 m2 dispute h2 4760 n3
call 'F 13' 4763 x verify h2 4760
call 'F 14' 4763 m0 raw 00000000
summary F 5 5 2

finish diff.sh
