#!/bin/sh
# Dispatcher and decode rows (slice K4b, C-K4b-1, C-K4-6, C-K4-11): one
# `evm run` call on the runtime of examples/residuals.lang for each row. The
# selectors and the map slots come from SHA3 in evm, not from langc. It needs
# evm (go-ethereum), od and build/langc. Run it from the kit root.
set -u
fail=0
checks=0

rt=$(build/langc build examples/residuals.lang --runtime) || { echo 'FAIL dispatch: langc build'; exit 1; }
revert='error: execution reverted'
trapped="0xae96083a $revert"

# keccak256 of the bytes HEX: CALLDATACOPY, SHA3, then RETURN of 32 bytes.
keccak() { evm run --code 365f5f37365f205f5260205ff3 --input "$1" 2>&1 | cut -c3-; }
# The selector of the signature TEXT.
sel() { keccak "$(printf '%s' "$1" | od -An -tx1 | tr -d ' \n')" | cut -c1-8; }
# A 32-byte word from hex digits.
w() { printf '%064s' "$1" | tr ' ' 0; }

refuse=$(sel 'refuse()')
crash=$(sel 'crash()')
earn=$(sel 'earn(uint256)')
file=$(sel 'file(uint64,uint256,uint64)')
own=$(sel 'own()')

# call NAME INPUT WANT [FLAGS...]: the output and the error of `evm run` on one line.
# With --prestate, evm also writes an INFO log line: the awk filter removes it.
call() {
  name=$1 input=$2 want=$3
  shift 3
  checks=$((checks + 1))
  got=$(evm run "$@" --code "$rt" --input "$input" 2>&1 | awk '!/^INFO /' | tr '\n' ' ' | awk '{ $1 = $1; print }')
  if [ "$got" != "$want" ]; then
    echo "FAIL dispatch $name: want '$want', got '$got'"
    fail=$((fail + 1))
  fi
}

# store NAME INPUT WANT: the storage after the call, as sorted `SLOT VALUE` rows on one line.
store() {
  checks=$((checks + 1))
  got=$(evm run --dump --code "$rt" --input "$2" 2>/dev/null \
    | awk -F'"' 'NF >= 5 && length($2) == 66 && substr($2, 1, 2) == "0x" { print substr($2, 3), $4 }' \
    | sort | tr '\n' ' ' | awk '{ $1 = $1; print }')
  if [ "$got" != "$3" ]; then
    echo "FAIL dispatch $1: want '$3', got '$got'"
    fail=$((fail + 1))
  fi
}

# The head: no selector, a short selector, an unknown selector, a call value.
call empty '' "$revert"
call short-selector "$(printf '%s' "$refuse" | cut -c1-6)" "$revert"
call unknown 12345678 "$revert"
# The evm sender ("sender") gets a balance, so the call value reaches the code.
# evm needs gasLimit, difficulty and a config with each fork at block 0 and
# shanghaiTime 0 (PUSH0). The same call with no value is the control row.
forks='"homesteadBlock":0,"eip150Block":0,"eip155Block":0,"eip158Block":0,"byzantiumBlock":0,"constantinopleBlock":0,"petersburgBlock":0,"istanbulBlock":0,"berlinBlock":0,"londonBlock":0,"mergeNetsplitBlock":0'
printf '{"gasLimit":"0x1c9c380","difficulty":"0x0","config":{"chainId":1,%s,"shanghaiTime":0,"terminalTotalDifficulty":0,"terminalTotalDifficultyPassed":true},"alloc":{"0x000000000000000000000000000073656e646572":{"balance":"0x10"}}}\n' \
  "$forks" >build/dispatch-genesis.json
call call-value "$earn$(w 5)" "$revert" --prestate build/dispatch-genesis.json --value 1
call call-value-zero "$earn$(w 5)" '' --prestate build/dispatch-genesis.json --value 0

# The entries: none, a trap, short arguments, dirty Nat words, a clean Nat word at 2^64 - 1.
call refuse "$refuse" "$revert"
call crash "$crash" "$trapped"
call own-not-owner "$own" "$revert"
call earn-short "$earn" "$revert"
call earn "$earn$(w 5)" ''
store earn-storage "$earn$(w 5)" "$(w 2) 05 $(w 3) 01"
call file-short "$file$(w 3)$(w 9)" "$revert"
call file-dirty-k "$file$(w 10000000000000000)$(w 9)$(w 4)" "$revert"
call file-dirty-j "$file$(w 3)$(w 9)$(w 10000000000000000)" "$revert"
call file "$file$(w ffffffffffffffff)$(w 9)$(w 4)" ''
store file-storage "$file$(w ffffffffffffffff)$(w 9)$(w 4)" "$(keccak "$(w ffffffffffffffff)$(w 4)") 09"

if [ "$fail" -ne 0 ]; then
  echo "dispatch: $fail of $checks checks failed"
  exit 1
fi
echo "dispatch: $checks checks passed"
