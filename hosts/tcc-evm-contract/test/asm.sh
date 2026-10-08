#!/bin/sh
# The assembler gate. `make check` runs it from the kit directory after
# test/gate.sh. It needs geth `evm` on PATH.
set -eu
fail=0
build/asmtool || fail=$((fail + 1))

want="addPrice 0x57279353"
got=$(build/asmtool abi)
[ "$got" = "$want" ] || { echo "FAIL abi: want $want, got $got"; fail=$((fail + 1)); }

# The runtime returns the word 42. The creation code returns the runtime.
runtime=$(build/asmtool runtime)
creation=$(build/asmtool creation)
word=0x000000000000000000000000000000000000000000000000000000000000002a
got=$(evm run --code "$runtime" 2>&1) || true
[ "$got" = "$word" ] || { echo "FAIL evm run runtime: want $word, got $got"; fail=$((fail + 1)); }
got=$(evm run --code "$creation" 2>&1) || true
[ "$got" = "0x$runtime" ] || { echo "FAIL evm run creation: want 0x$runtime, got $got"; fail=$((fail + 1)); }

echo "asm gate: $fail failures"
[ "$fail" -eq 0 ]
