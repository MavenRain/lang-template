#!/bin/sh
# Back-end tests of langc, run by make test after make (SPEC section 10,
# chunks 5a, 5b and 7): abi of each example program gives its golden text, each
# selector is the first 4 bytes of keccak256 of its signature, build writes
# lowercase hex, the runtime holds each selector, the creation code ends
# with the runtime code, the runtime ends with the rows and the policy
# records of langc table, each mutant keeps its code under abi and build,
# and EIP-3860 bounds the creation code and the member words. A bytecode write failure exits 2 and removes the
# incomplete output. Files go to build/test.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
langc=$root/build/langc
tool=$root/build/evmtool
programs=$root/examples/programs
mutants=$root/examples/mutants
out=$root/build/test
mkdir -p "$out"
failures=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

entries='entry anchor(bytes32) selector eecdf927 inputs bytes32 outputs uint256
entry verify(bytes32,uint256) selector 382262fc inputs bytes32,uint256 outputs uint256
entry cast(uint256) selector 738198b4 inputs uint256 outputs -
event Anchored(bytes32,uint256) topic fde54488b5523b3abf19b99976dd0e2c531fbcd233d0eb682c96c3a18cf6b3c1 indexed bytes32 data uint256'

# abi_is PROG MEMBERS: exit 0 and stdout is the golden text.
abi_is() {
  "$langc" abi "$programs/$1" > "$out/abi.out" 2> "$out/abi.err"
  status=$?
  printf 'constructor inputs address[%s]\n%s\n' "$2" "$entries" > "$out/want.txt"
  if [ "$status" -eq 0 ] && cmp -s "$out/abi.out" "$out/want.txt"; then
    pass "abi of $1"
  else
    fail "abi of $1: exit $status, stderr: $(cat "$out/abi.err")"
    diff "$out/want.txt" "$out/abi.out"
  fi
}
abi_is arrow-debreu.lang 3
abi_is arrow-impossibility.lang 2
abi_is schelling-ising.lang 2
got=$(awk '$1 == "event" { print $4 }' "$out/abi.out")
want=$("$tool" keccak 'Anchored(bytes32,uint256)')
if [ "$got" = "$want" ]; then pass "topic of Anchored(bytes32,uint256) is keccak256 of the signature"; else fail "topic: got $got, want $want"; fi

# Each selector is the first 4 bytes of keccak256 of its signature.
for signature in 'anchor(bytes32)' 'verify(bytes32,uint256)' 'cast(uint256)'; do
  got=$(awk -v s="$signature" '$2 == s { print $4 }' "$out/abi.out")
  want=$("$tool" keccak "$signature" | cut -c1-8)
  if [ -n "$got" ] && [ "$got" = "$want" ]; then pass "selector of $signature is $got"; else fail "selector of $signature: got $got, want $want"; fi
done

# hex_ok TEXT: 1 when TEXT is a nonempty even run of lowercase hex digits.
hex_ok() {
  case $1 in
    '' | *[!0-9a-f]*) echo 0 ;;
    *) echo $((1 - ${#1} % 2)) ;;
  esac
}

for f in "$programs"/*.lang; do
  name=$(basename "$f")
  "$langc" table "$f" > "$out/table.out"
  members=$(awk '/^members/ { print $2 }' "$out/table.out")
  candidates=$(awk '/^candidates/ { print $2 }' "$out/table.out")
  rm -f "$out/creation.hex" "$out/runtime.hex"
  "$langc" build "$f" -o "$out/creation.hex" 2> "$out/build.err"
  first=$?
  "$langc" build "$f" --runtime -o "$out/runtime.hex" 2>> "$out/build.err"
  second=$?
  creation=$(cat "$out/creation.hex" 2> /dev/null)
  runtime=$(cat "$out/runtime.hex" 2> /dev/null)
  if [ "$first" -eq 0 ] && [ "$second" -eq 0 ]; then pass "build of $name exits 0"; else fail "build of $name: exit $first and $second, stderr: $(cat "$out/build.err")"; fi
  if [ "$(hex_ok "$creation")" -eq 1 ] && [ "$(hex_ok "$runtime")" -eq 1 ]; then pass "build of $name writes lowercase hex"; else fail "build of $name: not lowercase hex"; fi
  held=1
  for selector in eecdf927 382262fc 738198b4; do
    case $runtime in *"63$selector"*) ;; *) held=0 ;; esac
  done
  if [ "$held" -eq 1 ]; then pass "runtime of $name holds each selector"; else fail "runtime of $name: a selector is missing"; fi
  case $creation in
    ?*"$runtime") pass "creation of $name ends with the runtime" ;;
    *) fail "creation of $name does not end with the runtime" ;;
  esac
  # The runtime is the code and the binomial part of evmtool (which has the
  # same number of rows), then the rows and the policy records of the table.
  records=$(awk '/^policy/ { p = p sprintf("%02x%016x", $4 == "allow", $8) }
    /^tally/ { i = 1; while ($i != ":") i++; f = $(i + 1)
      r = r sprintf("%02x%04x%04x", (f == "one") + 2 * (f == "two"), $(i + 2) + 0, $(i + 3) + 0) }
    END { print r p }' "$out/table.out")
  nrows=$(awk '/^tally/ { n++ } END { print n }' "$out/table.out")
  code=$("$tool" runtime "$members" "$candidates")
  keep=$(( ${#code} - 10 * nrows - 18 * candidates ))
  want=$(printf '%s' "$code" | cut -c1-"$keep")$records
  if [ "$runtime" = "$want" ]; then pass "runtime of $name ends with the $nrows rows and the policy records of its table"; else fail "runtime of $name: got $runtime, want $want"; fi
done

# Each mutant gives the same code under abi and build as under check, and
# build writes no file. abi and build tabulate, so a table refusal
# (REFUSE_FORK of fork-unfrozen.lang) stops them too.
for f in "$mutants"/*.lang; do
  rm -f "$out/mutant.hex"
  "$langc" check "$f" > /dev/null 2> "$out/check.err"
  "$langc" abi "$f" > /dev/null 2> "$out/abi.err"
  abi_status=$?
  "$langc" build "$f" -o "$out/mutant.hex" 2> "$out/build.err"
  build_status=$?
  want=$(cut -d: -f2 "$out/check.err")
  abi=$(cut -d: -f2 "$out/abi.err")
  build=$(cut -d: -f2 "$out/build.err")
  same=0
  if [ -n "$want" ] && [ "$want" = "$abi" ]; then same=1; fi
  if [ "$same" -eq 1 ] && [ "$want" = "$build" ]; then same=2; fi
  if [ "$same" -eq 2 ] && [ ! -e "$out/mutant.hex" ]; then same=3; fi
  refused=0
  if [ "$abi_status" -eq 1 ] && [ "$build_status" -eq 1 ]; then refused=1; fi
  if [ "$refused" -eq 1 ] && [ "$same" -eq 3 ]; then
    pass "mutant $(basename "$f") keeps$want"
  else
    fail "mutant $(basename "$f"): exit $abi_status and $build_status, check$want, abi$abi, build$build"
  fi
done

# SPEC section 3, F13: a rule that calls transportOutcome builds. The proof
# computes away when build tabulates the rule, so the bytecode is the
# bytecode of arrow-impossibility.lang, whose rule gives none with no proof.
{ head -n 12 "$programs/arrow-impossibility.lang"; cat <<'EOF'

def rule : Tally -> Outcome :=
  fun (t : Tally) => transportOutcome (fun (w : Outcome) => Outcome) none none (sameOutcome none) none
EOF
} > "$out/transport-rule.lang"
rm -f "$out/transport.hex" "$out/plain.hex"
"$langc" build "$out/transport-rule.lang" -o "$out/transport.hex" 2> "$out/build.err"
first=$?
"$langc" build "$programs/arrow-impossibility.lang" -o "$out/plain.hex" 2>> "$out/build.err"
second=$?
if [ "$first" -eq 0 ] && [ "$second" -eq 0 ] && cmp -s "$out/transport.hex" "$out/plain.hex"; then
  pass "a rule through transportOutcome builds the bytecode of arrow-impossibility.lang"
else
  fail "a rule through transportOutcome: exit $first and $second, stderr: $(cat "$out/build.err")"
fi

# EIP-3860: the creation code and 32 bytes for each member word are at most
# 49152 bytes. The table grows with the members, so a bisection over 256 to
# 4095 members (2 candidates) finds the largest contract that builds. build
# refuses one member more with the EIP-3860 message and writes no file.
members_is() {
  awk -v n="$1" '/^def members/ { print "def members : Nat := " n; next } { print }' \
    "$programs/arrow-debreu.lang" > "$out/members-$1.lang"
  rm -f "$out/max.hex"
  "$langc" build "$out/members-$1.lang" -o "$out/max.hex" 2> "$out/build.err"
}
low=256
high=4095
while [ $((high - low)) -gt 1 ]; do
  mid=$(( (low + high) / 2 ))
  if members_is "$mid"; then low=$mid; else high=$mid; fi
done
max=$low
members_is "$max"
status=$?
bytes=$(( $(tr -d '\n' < "$out/max.hex" | wc -c) / 2 + 32 * max ))
if [ "$status" -eq 0 ] && [ "$bytes" -le 49152 ]; then pass "$max members build, $bytes bytes with the member words"; else fail "$max members: exit $status, $bytes bytes, stderr: $(cat "$out/build.err")"; fi
members_is $((max + 1))
status=$?
case $(cat "$out/build.err") in
  'langc: EVM_SIZE: -: the creation code and '*'(EIP-3860)') prefix_ok=1 ;;
  *) prefix_ok=0 ;;
esac
if [ -e "$out/max.hex" ]; then prefix_ok=0; fi
if [ "$status" -eq 1 ] && [ "$prefix_ok" -eq 1 ]; then pass "$((max + 1)) members is EVM_SIZE (EIP-3860)"; else fail "$((max + 1)) members: exit $status, stderr: $(cat "$out/build.err")"; fi

# Let fopen succeed, then make the bytecode write fail. Ignore SIGXFSZ so
# stdio reports the error. Capture stderr through a pipe because regular
# files in this subshell have a zero size limit.
rm -f "$out/write-error.hex"
message=$(
  trap '' XFSZ
  ulimit -f 0
  "$langc" build "$programs/arrow-debreu.lang" -o "$out/write-error.hex" 2>&1
)
status=$?
case $message in
  'langc: EVM_IO: -: '*) prefix_ok=1 ;;
  *) prefix_ok=0 ;;
esac
if [ -e "$out/write-error.hex" ]; then prefix_ok=0; fi
if [ "$status" -eq 2 ] && [ "$prefix_ok" -eq 1 ]; then pass 'bytecode write failure exits 2 and removes output'; else fail "bytecode write failure: exit $status, stderr: $message"; fi

if [ "$failures" -eq 0 ]; then echo "build.sh: all passed"; exit 0; fi
echo "build.sh: $failures failed"
exit 1
