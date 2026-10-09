#!/bin/sh
# Runtime tests of langc (SPEC section 10, chunk 5b), run by make test
# after make. The geth evm runs the runtime of langc build --runtime on a
# prestate: the counts of a tally of langc table, the member slot of
# member position 0 (build/evmtool slot) and its ballot at slot K. anchor
# gives TIMESTAMP at a row of the fate one with an allow policy, sets the
# pair slot and writes the Anchored log once for each pair. It reverts at
# each other row, for a caller that is not a member and for h = 0. cast
# reverts when the rows before and after the ballot moves have the fate one
# and the schema goes down (O6). amend (chunk 11) moves slot K + M to k and
# writes the Amended log, with the guards of O3 b3 to b6, and anchor and
# cast read the rows of the constitution in slot K + M. dispute (chunk 13)
# writes the Disputed log and no slot while the block time is less than
# t + window (O7). With no evm on the PATH, the tests are skipped. Files go to build/test.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
langc=$root/build/langc
tool=$root/build/evmtool
programs=$root/examples/programs
out=$root/build/test
mkdir -p "$out"
failures=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

# same NAME GOT WANT
same() {
  if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: got $2, want $3"; fi
}

if ! command -v evm > /dev/null 2>&1; then
  echo 'run.sh: no evm on the PATH, skipped'
  exit 0
fi

receiver=00000000000000000000000000000000000000aa
member=00000000000000000000000000000000000000bb
stranger=00000000000000000000000000000000000000cc
stamp=4660  # the TIMESTAMP of the block
t=$(printf '%064x' "$stamp")
h=$("$tool" keccak 'anchor-lang run.sh')
topic=$("$tool" keccak 'Anchored(bytes32,uint256)')
member_slot=$("$tool" slot "$member")
lang_input=eecdf927$h

# runtime PROGRAM: the runtime hex of PROGRAM to $out/code.hex and its
# table to $out/table.out.
runtime() {
  "$langc" build "$1" --runtime -o "$out/code.hex" 2> "$out/run.err" || fail "build --runtime of $1: $(cat "$out/run.err")"
  "$langc" table "$1" > "$out/table.out"
}

# tally FATE ADMIT: the counts and the row of the first tally of
# $out/table.out with FATE whose policy p is ADMIT (allow or deny, or - for
# each policy), without the word tally.
tally() {
  awk -v fate="$1" -v admit="$2" 'NR == FNR && $1 == "policy" { allow[$2] = $4 }
    NR != FNR && $1 == "tally" {
      i = 2
      while ($i != ":") i++
      any = admit == "-" || allow[$(i + 2)] == admit
      if ($(i + 1) == fate && any) { $1 = ""; print substr($0, 2); exit }
    }' "$out/table.out" "$out/table.out"
}

# counts COUNTS: the tally of $out/table.out with COUNTS, as tally gives it.
counts() {
  awk -v want="tally $1 :" 'index($0, want) == 1 { $1 = ""; print substr($0, 2); exit }' "$out/table.out"
}

# prestate TALLY BALLOT [PAIR [WORD]]: $out/prestate.json holds the runtime
# of $out/code.hex at the receiver, the counts of TALLY, the member slot of
# member position 0, its ballot BALLOT at slot K, the pair slot PAIR and
# the storage entry WORD ("slot":"value").
prestate() {
  storage=$(printf '%s\n' "$1" | awk -v m="$member_slot" -v b="$2" -v pair="${3:-}" -v word="${4:-}" '{
    for (c = 1; $c != ":"; c++) printf "\"0x%064x\":\"0x%064x\",", c - 1, $c
    printf "\"0x%064x\":\"0x%064x\",\"0x%s\":\"0x0000000000000000000000000000000000000000000000000000000000000001\"", c - 1, b, m
    if (pair != "") printf ",\"0x%s\":\"0x0000000000000000000000000000000000000000000000000000000000000001\"", pair
    if (word != "") printf ",%s", word
  }')
  printf '{"config":{"chainId":1,"homesteadBlock":0,"eip150Block":0,"eip155Block":0,"eip158Block":0,"byzantiumBlock":0,"constantinopleBlock":0,"petersburgBlock":0,"istanbulBlock":0,"berlinBlock":0,"londonBlock":0,"mergeNetsplitBlock":0,"shanghaiTime":0,"cancunTime":0,"terminalTotalDifficulty":0,"terminalTotalDifficultyPassed":true},"timestamp":"0x%x","gasLimit":"0x1c9c380","difficulty":"0x0","alloc":{"0x%s":{"balance":"0x0","code":"0x%s","storage":{%s}}}}\n' \
    "$stamp" "$receiver" "$(tr -d '\n' < "$out/code.hex")" "$storage" > "$out/prestate.json"
}

# call SENDER INPUT: run the receiver. stdout (the output word, then the
# state after the run) goes to $out/run.out, the trace and the logs go to
# $out/run.err.
call() {
  evm run --prestate "$out/prestate.json" --receiver "0x$receiver" --sender "0x$1" \
    --input "0x$2" --debug --dump --nostack > "$out/run.out" 2> "$out/run.err"
}

# reverts NAME SENDER INPUT: the run reverts.
reverts() {
  call "$2" "$3"
  case $(cat "$out/run.out") in
    *'execution reverted'*) pass "$1" ;;
    *) fail "$1: $(head -n 1 "$out/run.out")" ;;
  esac
}

# runs NAME SENDER INPUT: the run stops with no error.
runs() {
  call "$2" "$3"
  case $(cat "$out/run.out") in
    *'error'*) fail "$1: $(head -n 1 "$out/run.out")" ;;
    *) pass "$1" ;;
  esac
}

log_count() { awk '/^LOG[0-9]:/ { n++ } END { print n + 0 }' "$out/run.err"; }

# anchor at the first tally of Arrow-Debreu: the fate one, policy allow.
runtime "$programs/arrow-debreu.lang"
open=$(tally one allow)
prestate "$open" 0
call "$member" "$lang_input"
same 'anchor at a row one of an allow policy returns TIMESTAMP' "$(head -n 1 "$out/run.out")" "0x$t"
same 'anchor writes one log' "$(log_count)" 1
same 'topic 0 of the log is keccak256 of Anchored(bytes32,uint256)' \
  "$(awk '$1 == "00000000" && NF == 2 { print $2; exit }' "$out/run.err")" "$topic"
same 'topic 1 of the log is h' "$(awk '$1 == "00000001" && NF == 2 { print $2; exit }' "$out/run.err")" "$h"
same 'the data of the log is t' \
  "$(awk 'p && /\|/ { for (i = 2; i <= 17; i++) printf "%s", $i } /^LOG2:/ { p = 1 }' "$out/run.err")" "$t"
pair=$(awk -v m="\"0x$member_slot\":" '/"0x[0-9a-f]+": "01"/ && $1 != m { k = $1; gsub(/[":]/, "", k); print substr(k, 3) }' "$out/run.out")
if [ -n "$pair" ]; then pass 'anchor sets one more storage slot to 1'; else fail 'anchor sets no slot to 1'; fi
prestate "$open" 0 "$pair"
call "$member" "382262fc$h$t"
same 'verify(h, t) gives 1 with that slot' "$(head -n 1 "$out/run.out")" "0x$(printf '%064x' 1)"
call "$member" "$lang_input"
same 'a second anchor of h in the same block returns t' "$(head -n 1 "$out/run.out")" "0x$t"
same 'a second anchor of h in the same block writes no log' "$(log_count)" 0

prestate "$open" 0
reverts 'anchor reverts for a caller that is not a member' "$stranger" "$lang_input"
reverts 'anchor reverts for h = 0' "$member" "eecdf927$(printf '%064x' 0)"
prestate "$(tally one deny)" 0
reverts 'anchor reverts at a row one of a deny policy' "$member" "$lang_input"
runtime "$programs/arrow-impossibility.lang"
prestate "$(tally none -)" 0
reverts 'anchor reverts at a row none' "$member" "$lang_input"
runtime "$programs/schelling-ising.lang"
prestate "$(tally two -)" 0
reverts 'anchor reverts at a row two' "$member" "$lang_input"

# cast (O6). schema-2.lang is Arrow-Debreu with the schema 2 in closedLog:
# the tallies 3 0 and 2 1 are one of policy 0 (schema 1), and 1 2 and 0 3
# are one of policy 1 (schema 2). Member position 0 casts.
cast_input() { printf '738198b4%064x' "$1"; }
awk '/^def closedLog/ { sub(/ 0 1 flagYes/, " 0 2 flagYes") } { print }' \
  "$programs/arrow-debreu.lang" > "$out/schema-2.lang"
runtime "$out/schema-2.lang"
same 'policy 1 of schema-2.lang has the schema 2' "$(awk '$1 == "policy" && $2 == 1 { print $8 }' "$out/table.out")" 2
prestate "$(counts '1 2')" 1
reverts 'cast from 1 2 (schema 2) to 2 1 (schema 1) reverts (O6)' "$member" "$(cast_input 0)"
prestate "$(counts '2 1')" 0
runs 'cast from 2 1 (schema 1) to 1 2 (schema 2) does not revert' "$member" "$(cast_input 1)"
runtime "$programs/arrow-debreu.lang"
prestate "$(counts '1 2')" 1
runs 'cast between two rows one of schema 1 does not revert' "$member" "$(cast_input 0)"
runtime "$programs/arrow-impossibility.lang"
prestate "$(counts '1 1')" 1
runs 'cast between two rows none does not revert' "$member" "$(cast_input 0)"

# amend (O3, chunk 11). arrow-debreu-amend.lang has K = 2 and M = 3, so slot
# 5 holds the constitution. Policy 0 (allow) has the mask 11 and policy 1
# (deny) the mask 10 (bit k from the left). Constitution 0 gives 2 1 the
# policy 0 and constitution 1 gives it the policy 1; 3 0 has the policy 0
# and 1 2 the policy 1 under both. A prestate with slot 5 = 1 is the
# storage after amend(1).
amend_input() { printf '13723792%064x' "$1"; }
constitution() { printf '"0x%064x":"0x%064x"' 5 "$1"; }
slot5() { awk -v s="\"0x$(printf '%064x' 5)\":" '$1 == s { v = $2; gsub(/[",]/, "", v); print v }' "$out/run.out"; }
amended=$("$tool" keccak 'Amended(uint256)')
runtime "$programs/arrow-debreu-amend.lang"
prestate "$(counts '3 0')" 0
runs 'amend(1) at 3 0 under constitution 0 does not revert' "$member" "$(amend_input 1)"
same 'amend(1) sets slot K + M to 1' "$(slot5)" 01
same 'amend(1) writes one log' "$(log_count)" 1
same 'topic 0 of the log is keccak256 of Amended(uint256)' \
  "$(awk '$1 == "00000000" && NF == 2 { print $2; exit }' "$out/run.err")" "$amended"
same 'the data of the log is k' \
  "$(awk 'p && /\|/ { for (i = 2; i <= 17; i++) printf "%s", $i } /^LOG1:/ { p = 1 }' "$out/run.err")" "$(printf '%064x' 1)"
reverts 'amend(1) reverts for a caller that is not a member' "$stranger" "$(amend_input 1)"
reverts 'amend(2) reverts (k > C - 1)' "$member" "$(amend_input 2)"
reverts 'amend(2^256 - 1) reverts (k > C - 1)' "$member" "13723792$(awk 'BEGIN { while (n++ < 64) printf "f" }')"
runs 'amend(0) under constitution 0 does not revert' "$member" "$(amend_input 0)"
same 'amend(0) under constitution 0 writes no slot K + M and no log (b3)' "[$(slot5)] $(log_count)" '[] 0'
prestate "$(counts '1 2')" 1
reverts 'amend(1) at 1 2 reverts: amendTo 1 1 is no' "$member" "$(amend_input 1)"
prestate "$(counts '2 1')" 1 '' "$(constitution 1)"
runs 'amend(1) at 2 1 under constitution 1 (policy 1, amendTo 1 1 is no) does not revert (b3)' "$member" "$(amend_input 1)"
same 'amend(1) under constitution 1 keeps slot K + M at 1 and writes no log' "[$(slot5)] $(log_count)" '[01] 0'
reverts 'anchor at 2 1 under constitution 1 (policy 1, deny) reverts' "$member" "$lang_input"
prestate "$(counts '2 1')" 1
call "$member" "$lang_input"
same 'anchor at 2 1 under constitution 0 (policy 0, allow) returns TIMESTAMP' "$(head -n 1 "$out/run.out")" "0x$t"

# O6 for amend and cast under constitution 1. amend-schema-2.lang is
# arrow-debreu-amend.lang with the schema 2 in openLog (policy 0).
awk '/^def openLog/ { sub(/ 0 1 flagYes/, " 0 2 flagYes") } { print }' \
  "$programs/arrow-debreu-amend.lang" > "$out/amend-schema-2.lang"
runtime "$out/amend-schema-2.lang"
same 'policy 0 of amend-schema-2.lang has the schema 2' "$(awk '$1 == "policy" && $2 == 0 { print $8 }' "$out/table.out")" 2
prestate "$(counts '2 1')" 1
reverts 'amend(1) at 2 1 from policy 0 (schema 2) to policy 1 (schema 1) reverts (O6)' "$member" "$(amend_input 1)"
prestate "$(counts '2 1')" 1 '' "$(constitution 1)"
runs 'amend(0) at 2 1 from policy 1 (schema 1) to policy 0 (schema 2) does not revert' "$member" "$(amend_input 0)"
same 'amend(0) clears slot K + M and writes one log' "[$(slot5)] $(log_count)" '[] 1'
prestate "$(counts '3 0')" 0 '' "$(constitution 1)"
reverts 'cast(1) from 3 0 to 2 1 under constitution 1 (schema 2 to 1) reverts (O6)' "$member" "$(cast_input 1)"
prestate "$(counts '3 0')" 0
runs 'cast(1) from 3 0 to 2 1 under constitution 0 (policy 0 at both) does not revert' "$member" "$(cast_input 1)"

# dispute (O7, chunk 13). arrow-debreu-dispute.lang is arrow-debreu.lang with
# the window 100 in openLog (policy 0) and the window 0 in closedLog
# (policy 1). The pair slot of (h, 4660) is the one of the anchor above.
# stamp is the block time; the tests set it back to 4660.
disputed=$("$tool" keccak 'Disputed(bytes32,uint256,bytes32)')
note=$("$tool" keccak 'anchor-lang run.sh note')
dispute_input() { printf '4db31205%s%064x%s' "$h" "$1" "$note"; }
dump() { awk '/"0x[0-9a-f]+": "/' "$out/run.out" | sort; }
runtime "$programs/arrow-debreu-dispute.lang"
same 'policy 0 of arrow-debreu-dispute.lang has the window 100' "$(awk '$1 == "policy" && $2 == 0 { print $7 }' "$out/table.out")" 100
prestate "$(counts '3 0')" 0 "$pair"
call "$member" "382262fc$h$t"
same 'verify of (h, 4660) gives 1 before the dispute' "$(head -n 1 "$out/run.out")" "0x$(printf '%064x' 1)"
dump > "$out/dump.before"
runs 'dispute of (h, 4660) by a member at 4660 does not revert' "$member" "$(dispute_input 4660)"
same 'dispute writes one log' "$(log_count)" 1
same 'topic 0 of the log is keccak256 of Disputed(bytes32,uint256,bytes32)' \
  "$(awk '$1 == "00000000" && NF == 2 { print $2; exit }' "$out/run.err")" "$disputed"
same 'topic 1 of the log is h' "$(awk '$1 == "00000001" && NF == 2 { print $2; exit }' "$out/run.err")" "$h"
same 'the data of the log is t and the note' \
  "$(awk 'p && /\|/ { for (i = 2; i <= 17; i++) printf "%s", $i } /^LOG2:/ { p = 1 }' "$out/run.err")" "$t$note"
dump > "$out/dump.after"
same 'dispute changes no storage (metadata)' "$(if cmp -s "$out/dump.before" "$out/dump.after"; then echo same; else echo changed; fi)" same
stamp=4759
prestate "$(counts '3 0')" 0 "$pair"
runs 'dispute of (h, 4660) at 4759 (t + window - 1) does not revert' "$member" "$(dispute_input 4660)"
stamp=4760
prestate "$(counts '3 0')" 0 "$pair"
reverts 'dispute of (h, 4660) at 4760 (t + window) reverts' "$member" "$(dispute_input 4660)"
stamp=4660
prestate "$(counts '3 0')" 0 "$pair"
reverts 'dispute reverts for a caller that is not a member' "$stranger" "$(dispute_input 4660)"
reverts 'dispute of (h, 4661) reverts: the pair is not set' "$member" "$(dispute_input 4661)"
prestate "$(counts '3 0')" 0
reverts 'dispute of (h, 4660) reverts with no pair slot' "$member" "$(dispute_input 4660)"
prestate "$(counts '1 2')" 1 "$pair"
reverts 'dispute at 1 2 (policy 1, window 0) reverts' "$member" "$(dispute_input 4660)"
runtime "$programs/arrow-debreu.lang"
prestate "$(counts '3 0')" 0 "$pair"
reverts 'dispute reverts on arrow-debreu.lang (no window, no dispute entry)' "$member" "$(dispute_input 4660)"

if [ "$failures" -eq 0 ]; then echo "run.sh: all passed"; exit 0; fi
echo "run.sh: $failures failed"
exit 1
