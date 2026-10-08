#!/bin/sh
# Law tests of langc (SPEC section 10, chunk 6), run by make test after
# make. Each law runs on geth evm: a deploy, then calls on the deployed
# storage. The laws: the log only grows (monotone), a second anchor of the
# same pair changes nothing (idempotent), two distinct anchors commute, no
# call deletes a pair, amend is the identity on the log (O3: M1 has no amend
# entry, so its selectors revert and the storage does not change), and
# there is no admit at a tally two. With no evm on the PATH, the tests are
# skipped.
root=$(cd "$(dirname "$0")/.." && pwd)
if ! command -v evm > /dev/null 2>&1; then
  echo 'laws.sh: no evm on the PATH, skipped'
  exit 0
fi
out=$root/build/test/chain/laws
. "$root/test/evmchain.sh"

m0=$(member 0)
m1=$(member 1)
m2=$(member 2)
h1=$(keccak "$(text 'anchor-lang laws h1')")
h2=$(keccak "$(text 'anchor-lang laws h2')")
h3=$(keccak "$(text 'anchor-lang laws h3')")
t0=$(printf '%064x' 4660)
one=$(printf '%064x' 1)
pk=$(keccak "$h1$t0")

save() { cp "$out/state" "$out/state.$1"; }
load() { cp "$out/state.$1" "$out/state"; }
result() { head -n 1 "$out/got"; }
logcount() { awk '$1 == "log" || $1 == "logs" { n++ } END { print n + 0 }' "$out/got"; }
unchanged() { if cmp -s "$out/state.$1" "$out/state"; then echo same; else echo changed; fi; }
slot() { awk -v k="$1" '$1 == k { print $2 }' "$out/state"; }
count() { _v=$(slot "$(printf '%064x' "$1")"); printf '%d' "0x${_v:-0}"; }

# pairs FILE: the pair slots of the storage FILE: the keys that are not a
# count or ballot slot (below K + M) and not a member slot.
pairs() {
  awk -v n=$((K + M)) '
    BEGIN { for (j = 0; j < n; j++) skip[sprintf("%064x", j)] = 1 }
    NR == FNR { if ($1 == "member") skip[$4] = 1; next }
    !($1 in skip) { print $1 }' "$out/keys" "$1" | sort
}

program "$programs/arrow-debreu.lang"
members "$M"
create "$(tr -d '\n' < "$out/creation.hex")$(words "$M")"
save deployed

# Monotone: after each call, the pairs before are a subset of the pairs
# after.
lost=0
grow() {
  pairs "$out/state" > "$out/before"
  step "$1" "$2" "$3"
  pairs "$out/state" > "$out/after"
  if [ -n "$(comm -23 "$out/before" "$out/after")" ]; then lost=$((lost + 1)); fi
}
grow "$m0" "$(lang_in "$h1")" 4660
grow "$m0" "$(cast_in 1)" 4660
grow "$m1" "$(lang_in "$h2")" 4661
grow "$m1" "$(cast_in 1)" 4661
grow "$m2" "$(lang_in "$h3")" 4661
grow "$m1" "$(cast_in 0)" 4662
grow "$m2" "$(lang_in "$h1")" 4662
grow "$m0" 00000000 4662
grow "$stranger" "$(verify_in "$h1" 4660)" 4662
grow "$m0" "$(cast_in 0)" 4662
same 'monotone: no call of the 10 removes a pair' "$lost" 0
same 'monotone: the 10 calls end with 3 pairs' "$(pairs "$out/state" | wc -l | tr -d ' ')" 3

# Idempotent: a second anchor of (h1, 4660) changes nothing.
load deployed
step "$m0" "$(lang_in "$h1")" 4660
save first
r1=$(result)
l1=$(logcount)
step "$m0" "$(lang_in "$h1")" 4660
r2=$(result)
l2=$(logcount)
same 'idempotent: the storage after a second anchor of (h1, 4660) by m0 is the same' "$(unchanged first)" same
same 'idempotent: both anchors return t' "$r1 $r2" "result 0x$t0 result 0x$t0"
same 'idempotent: the first anchor writes one log and the second none' "$l1 $l2" '1 0'
step "$m1" "$(lang_in "$h1")" 4660
same 'idempotent: an anchor of (h1, 4660) by m1 returns t and the storage is the same' \
  "$(result) $(unchanged first)" "result 0x$t0 same"

# Distinct anchors commute: two orders from the same deployed storage.
load deployed
step "$m0" "$(lang_in "$h1")" 4660
awk '$1 == "log" { print $3, $4 }' "$out/got" > "$out/logs.ab"
step "$m1" "$(lang_in "$h2")" 4660
awk '$1 == "log" { print $3, $4 }' "$out/got" >> "$out/logs.ab"
save ab
load deployed
step "$m1" "$(lang_in "$h2")" 4660
awk '$1 == "log" { print $3, $4 }' "$out/got" > "$out/logs.ba"
step "$m0" "$(lang_in "$h1")" 4660
awk '$1 == "log" { print $3, $4 }' "$out/got" >> "$out/logs.ba"
same 'commute: h1 by m0 then h2 by m1, and the reverse order, give the same storage' "$(unchanged ab)" same
same 'commute: the two orders write the same 2 logs (topic 1 and data)' \
  "$(wc -l < "$out/logs.ab" | tr -d ' ') $(sort "$out/logs.ab" | tr '\n' ' ')" "2 $(sort "$out/logs.ba" | tr '\n' ' ')"
load deployed
step "$m0" "$(lang_in "$h1")" 4660
step "$m0" "$(lang_in "$h1")" 4661
save tt
load deployed
step "$m0" "$(lang_in "$h1")" 4661
step "$m0" "$(lang_in "$h1")" 4660
same 'commute: h1 at 4660 then at 4661, and the reverse order, give the same 2 pairs' \
  "$(unchanged tt) $(pairs "$out/state" | wc -l | tr -d ' ')" 'same 2'

# No deletion: the pair (h1, 4660) stays after each call.
kept() {
  _v=$(slot "$pk")
  step "$stranger" "$(verify_in "$h1" 4660)" 4667
  same "no deletion: $1, the pair slot of (h1, 4660) is 1 and verify gives 1" "$_v $(result)" "$one result 0x$one"
}
load deployed
step "$m0" "$(lang_in "$h1")" 4660
step "$m0" "$(lang_in "$h1")" 4661
kept 'after an anchor of (h1, 4661)'
step "$m1" "$(lang_in "$h2")" 4661
kept 'after an anchor of h2 by m1'
step "$m0" "$(cast_in 1)" 4662
step "$m1" "$(cast_in 1)" 4662
kept 'after cast 1 by m0 and by m1 (a deny tally)'
step "$stranger" "$(lang_in "$h3")" 4663
kept 'after an anchor by x (it reverts)'
step "$m0" 00000000 4663
kept 'after an unknown selector (it reverts)'
step "$m1" "$(cast_in 0)" 4664
kept 'after cast 0 by m1 (back to allow)'

# Amend: M1 has no amend entry (O3), so its selectors revert.
abi=$(for p in arrow-debreu arrow-impossibility schelling-ising; do "$langc" abi "$programs/$p.lang"; done)
same 'amend: langc abi of the 3 example programs has 9 entries and no amend line' \
  "$(printf '%s\n' "$abi" | awk '$1 == "entry" { e++ } /amend/ { a++ } END { print e + 0, a + 0 }')" '9 0'
_amend=$(keccak "$(text 'amend(uint256)')") || fatal 'cannot hash amend(uint256)'
amend1=$(printf '%s' "$_amend" | cut -c1-8)
_amend=$(keccak "$(text 'amend()')") || fatal 'cannot hash amend()'
amend0=$(printf '%s' "$_amend" | cut -c1-8)
load deployed
step "$m0" "$(lang_in "$h1")" 4660
save pre
step "$m0" "$amend1$zero" 4661
same 'amend: amend(uint256) by m0 reverts' "$(result)" 'result revert'
same 'amend: the storage after amend(uint256) is the same, with the pair slot of (h1, 4660)' \
  "$(unchanged pre) $(slot "$pk")" "same $one"
step "$m0" "$amend0" 4661
same 'amend: amend() by m0 reverts and the storage is the same' "$(result) $(unchanged pre)" 'result revert same'

# No admit at a tally two: schelling-ising.
program "$programs/schelling-ising.lang"
members "$M"
create "$(tr -d '\n' < "$out/creation.hex")$(words "$M")"
same 'two: the 3 tallies of schelling-ising are two and its 2 policies allow' \
  "$(awk '$1 == "tally" { t++ } $1 == "tally" && index($0, " : two ") { w++ } $1 == "policy" { p++ } $1 == "policy" && $4 == "allow" { a++ } END { print t + 0, w + 0, p + 0, a + 0 }' "$out/table.out")" '3 3 2 2'
noadmit() {
  save pre
  step "$m0" "$(lang_in "$h1")" "$1"
  _r0=$(result)
  step "$m1" "$(lang_in "$h1")" "$1"
  printf '%s %s %s' "$_r0" "$(result)" "$(unchanged pre)"
}
same 'two: at 2 0 (the deploy), anchor by m0 and by m1 reverts and the storage stays' \
  "$(count 0) $(count 1) $(noadmit 4660)" '2 0 result revert result revert same'
step "$m0" "$(cast_in 1)" 4661
c1=$(result)
same 'two: after cast 1 by m0 (now 1 1), anchor by m0 and by m1 reverts and the storage stays' \
  "$c1 $(count 0) $(count 1) $(noadmit 4661)" 'result 0x 1 1 result revert result revert same'
step "$m1" "$(cast_in 1)" 4662
c2=$(result)
same 'two: after cast 1 by m1 (now 0 2), anchor by m0 and by m1 reverts and the storage stays' \
  "$c2 $(count 0) $(count 1) $(noadmit 4662)" 'result 0x 0 2 result revert result revert same'

finish laws.sh
