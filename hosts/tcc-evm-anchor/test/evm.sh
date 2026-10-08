#!/bin/sh
# Back end tests of langc, run by make test after make: the keccak
# vectors and the bytes of the contract with no entry (src/evm.c), which
# fix the PUSH widths and the PUSH2 label offsets of both passes.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
tool=$root/build/evmtool
out=$root/build/test
mkdir -p "$out"
failures=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

# same NAME GOT WANT
same() {
  if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: got $2, want $3"; fi
}

same 'keccak256 of the empty string' "$("$tool" keccak '')" \
  c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
same 'selector of transfer(address,uint256)' "$("$tool" keccak 'transfer(address,uint256)' | cut -c1-8)" \
  a9059cbb

# Runtime of 1 member and 1 candidate: PUSH1 4, CALLDATASIZE, LT, PUSH2 46,
# JUMPI, PUSH0, CALLDATALOAD, PUSH1 0xe0, SHR; then DUP1, PUSH4 selector, EQ,
# PUSH2 entry, JUMPI for anchor (byte 50), verify (byte 232) and cast (byte
# 271); then the revert block at byte 46. anchor calls the rank subroutine
# (byte 486) with the return label at byte 94, and its pair slot test jumps
# to byte 224. cast calls the rank subroutine with the return labels at
# bytes 293 and 347, and its O6 guard jumps to byte 484. The rank loop is at
# byte 493. After the code (byte 546), the outcome table: no binomial bytes
# (K = 1), the row of the tally (1) is none (5 zero bytes), the record of
# policy 0 is deny of schema 0 (9 zero bytes).
head=6004361061002e575f3560e01c8063eecdf92714610032578063382262fc146100e8578063738198b41461010f575b5f5ffd
runtime=$("$tool" runtime 1 1)
case $runtime in
  "$head"*) pass 'runtime dispatch of 1 member and 1 candidate' ;;
  *) fail "runtime dispatch: got $runtime" ;;
esac
same 'runtime of 1 member and 1 candidate' "$runtime" \
  6004361061002e575f3560e01c8063eecdf92714610032578063382262fc146100e8578063738198b41461010f575b5f5ffd5b503461002e576024361061002e57335f5260205f20541561002e57600435801561002e5761005e6101e6565b600502610222018060209060403960405160f81c6001141561002e5760010160209060403960405160f01c6009026102270160209060403960405160f81c1561002e57805f52428060205260405f2080546100e05760019055817ffde54488b5523b3abf19b99976dd0e2c531fbcd233d0eb682c96c3a18cf6b3c160206020a25f5b505f5260205ff35b503461002e576044361061002e576004355f5260243560205260405f205415155f5260205ff35b503461002e576024361061002e576101256101e6565b6004355f811161002e57335f5260205f2054801561002e575f01805480546001900390558190558054600101905561015b6101e6565b600502610222018060209060403960405160f81c600114156101e45760010160209060403960405160f01c6009026102270160010160209060403960405160c01c90600502610222018060209060403960405160f81c600114156101e45760010160209060403960405160f01c6009026102270160010160209060403960405160c01c1161002e575b005b5f5f6102225f5b801561021c57805483019250828001820160209060403960405160f01c840193509060040190600190036101ed565b50505090560000000000000000000000000000
# Creation: CALLVALUE, PUSH2 95, JUMPI; the check of the argument words
# against PUSH2 END, the size of the creation code; CODECOPY of the words to
# 0x20; the member loop at byte 27; the count of candidate 0; the copy of the
# runtime (560 bytes); the revert block at byte 95; the runtime at byte 99.
same 'creation of 1 member and 1 candidate' "$("$tool" creation 1 1)" \
  3461005f5760206102930138141561005f5760206102936020395f5b80602002602001518060a01c61005f57801561005f575f5260205f20805461005f578160010190556001018060011161001b575060015f55610230806100635f395ff35b5f5ffd6004361061002e575f3560e01c8063eecdf92714610032578063382262fc146100e8578063738198b41461010f575b5f5ffd5b503461002e576024361061002e57335f5260205f20541561002e57600435801561002e5761005e6101e6565b600502610222018060209060403960405160f81c6001141561002e5760010160209060403960405160f01c6009026102270160209060403960405160f81c1561002e57805f52428060205260405f2080546100e05760019055817ffde54488b5523b3abf19b99976dd0e2c531fbcd233d0eb682c96c3a18cf6b3c160206020a25f5b505f5260205ff35b503461002e576044361061002e576004355f5260243560205260405f205415155f5260205ff35b503461002e576024361061002e576101256101e6565b6004355f811161002e57335f5260205f2054801561002e575f01805480546001900390558190558054600101905561015b6101e6565b600502610222018060209060403960405160f81c600114156101e45760010160209060403960405160f01c6009026102270160010160209060403960405160c01c90600502610222018060209060403960405160f81c600114156101e45760010160209060403960405160f01c6009026102270160010160209060403960405160c01c1161002e575b005b5f5f6102225f5b801561021c57805483019250828001820160209060403960405160f01c840193509060040190600190036101ed565b50505090560000000000000000000000000000

# limit_is NAME MEMBERS CANDIDATES: exit 1 with EVM_LIMIT.
limit_is() {
  "$tool" runtime "$2" "$3" > /dev/null 2> "$out/evm.err"
  status=$?
  case $(cat "$out/evm.err") in
    'langc: EVM_LIMIT: -: '*) prefix_ok=1 ;;
    *) prefix_ok=0 ;;
  esac
  if [ "$status" -eq 1 ] && [ "$prefix_ok" -eq 1 ]; then pass "$1 is EVM_LIMIT"; else fail "$1: exit $status, stderr: $(cat "$out/evm.err")"; fi
}
limit_is 'zero members' 0 1
limit_is 'zero candidates' 1 0

# EIP-170: the runtime is at most 24576 bytes. With 2 candidates and 256 to
# 32766 members, each member adds 7 bytes to the runtime (2 binomial bytes
# and a row of 5 bytes), so the largest runtime has N = 1000 + (24576 - R) / 7
# members, with R the runtime of 1000 members. The creation code of N
# members is over EIP-3860, so N gives the EIP-3860 message and N + 1 gives
# the EIP-170 message.
size_is() {
  "$tool" runtime "$2" 2 > /dev/null 2> "$out/evm.err"
  status=$?
  case $(cat "$out/evm.err") in
    $3) message_ok=1 ;;
    *) message_ok=0 ;;
  esac
  if [ "$status" -eq 1 ] && [ "$message_ok" -eq 1 ]; then pass "$1"; else fail "$1: exit $status, stderr: $(cat "$out/evm.err")"; fi
}
r=$(( $("$tool" runtime 1000 2 | tr -d '\n' | wc -c) / 2 ))
n=$(( 1000 + (24576 - r) / 7 ))
size_is "$n members and 2 candidates: the runtime fits, EIP-3860 refuses" "$n" \
  'langc: EVM_SIZE: -: the creation code and *(EIP-3860)'
size_is "$((n + 1)) members and 2 candidates: EIP-170 refuses" $((n + 1)) \
  'langc: EVM_SIZE: -: the runtime has 245[78]? bytes, the limit is 24576'

if [ "$failures" -eq 0 ]; then echo "evm.sh: all passed"; exit 0; fi
echo "evm.sh: $failures failed"
exit 1
