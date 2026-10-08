#!/bin/sh
# Runtime tests of langc (SPEC section 10, chunk 5b), run by make test
# after make. The geth evm runs the runtime of langc build --runtime on a
# prestate: the counts of a tally of langc table, the member slot of
# member position 0 (build/evmtool slot) and its ballot at slot K. anchor
# gives TIMESTAMP at a row of the fate one with an allow policy, sets the
# pair slot and writes the Anchored log once for each pair. It reverts at
# each other row, for a caller that is not a member and for h = 0. cast
# reverts when the rows before and after the ballot moves have the fate one
# and the schema goes down (O6). With no evm on the PATH, the tests are
# skipped. Files go to build/test.
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

# prestate TALLY BALLOT [PAIR]: $out/prestate.json holds the runtime of
# $out/code.hex at the receiver, the counts of TALLY, the member slot of
# member position 0, its ballot BALLOT at slot K and the pair slot PAIR.
prestate() {
  storage=$(printf '%s\n' "$1" | awk -v m="$member_slot" -v b="$2" -v pair="${3:-}" '{
    for (c = 1; $c != ":"; c++) printf "\"0x%064x\":\"0x%064x\",", c - 1, $c
    printf "\"0x%064x\":\"0x%064x\",\"0x%s\":\"0x0000000000000000000000000000000000000000000000000000000000000001\"", c - 1, b, m
    if (pair != "") printf ",\"0x%s\":\"0x0000000000000000000000000000000000000000000000000000000000000001\"", pair
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

if [ "$failures" -eq 0 ]; then echo "run.sh: all passed"; exit 0; fi
echo "run.sh: $failures failed"
exit 1
