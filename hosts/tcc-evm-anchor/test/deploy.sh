#!/bin/sh
# Deploy tests of langc (SPEC section 10, chunks 6 and 12), run by make test
# after make. geth evm runs the creation code of langc build with the
# member words (evm run --create). For each example program, the deployed
# code and the output must equal langc build --runtime, and the storage
# must be the member count at slot 0 and the member slots. The constructor
# guards of SPEC section 7 must revert on evm, and anchor (and amend for
# arrow-debreu-amend, O3) must run on the deployed state. With no evm on
# the PATH, the tests are skipped.
root=$(cd "$(dirname "$0")/.." && pwd)
if ! command -v evm > /dev/null 2>&1; then
  echo 'deploy.sh: no evm on the PATH, skipped'
  exit 0
fi
out=$root/build/test/chain/deploy
. "$root/test/evmchain.sh"

# deploy NAME: deploy examples/programs/NAME.lang with M member words.
deploy() {
  program "$programs/$1.lang"
  members "$M"
  create "$(tr -d '\n' < "$out/creation.hex")$(words "$M")"
  _rt=$(tr -d '\n' < "$out/runtime.hex" | tr 'A-F' 'a-f')
  if [ "$created" = ok ] && [ "$(cat "$out/code.hex")" = "$_rt" ] && [ "$(cat "$out/out.hex")" = "$_rt" ]; then
    pass "$1: the deployed code and the output equal langc build --runtime"
  else
    fail "$1: the deploy gives $created, code $(cut -c1-24 "$out/code.hex"), output $(cut -c1-24 "$out/out.hex"), want $(printf '%s' "$_rt" | cut -c1-24)"
  fi
  fresh "$M" > "$out/want"
  if [ "$created" = ok ] && cmp -s "$out/want" "$out/state"; then
    pass "$1: the storage is $M at slot 0 and the member slots 1 to $M, nothing else"
  else
    fail "$1: the storage is $(tr '\n' ' ' < "$out/state")"
  fi
}

deploy arrow-debreu
h=$(keccak "$(text 'anchor-lang deploy.sh')")
step "$(member 0)" "$(lang_in "$h")" 4660
same 'arrow-debreu: anchor by b1 on the deployed state returns t = 4660' \
  "$(head -n 1 "$out/got")" "result 0x$(printf '%064x' 4660)"
deploy arrow-impossibility
deploy schelling-ising
deploy arrow-debreu-amend
step "$(member 0)" "$(amend_in 1)" 4660
same 'arrow-debreu-amend: amend(1) by b1 on the deployed state sets slot K + M to 1' \
  "$(awk -v k="$(printf '%064x' $((K + M)))" '$1 == k { print $2 }' "$out/state")" "$(printf '%064x' 1)"

# The constructor guards (SPEC section 7) on arrow-debreu (3 members).
program "$programs/arrow-debreu.lang"
creation=$(tr -d '\n' < "$out/creation.hex")
w0=$(word "$(member 0)")
w1=$(word "$(member 1)")
w2=$(word "$(member 2)")
create "$creation$w0$w1"
same 'arrow-debreu: a deploy with 2 member words reverts' "$created" revert
create "$creation$w0$w1$w2$(word "$(member 3)")"
same 'arrow-debreu: a deploy with 4 member words reverts' "$created" revert
create "$creation$w0$zero$w2"
same 'arrow-debreu: a deploy with the zero address reverts' "$created" revert
create "$creation$w0$w0$w2"
same 'arrow-debreu: a deploy with a duplicate member reverts' "$created" revert
create "$creation$w0$w1$(word "1$(member 2)")"
same 'arrow-debreu: a deploy with a word that is not an address (bit 160 set) reverts' "$created" revert
create "$creation$w0$w1$w2" 1
same 'arrow-debreu: a deploy with a value of 1 wei reverts' "$created" revert

# The chain reader must retain extra topics, so differential comparisons
# reject an event with the wrong topic count. This contract emits LOG3
# with topics 0x11, 0x22, 0x33 and the data word 1.
printf '%s\n' '600160005260336022601160206000a300' > "$out/code.hex"
: > "$out/state"
step "$stranger" '' 4660
same 'chain log reader: preserves all three topics and the data' \
  "$(cat "$out/got")" "$(printf 'result 0x\nlog %064x %064x %064x %064x' 17 34 51 1)"

finish deploy.sh
