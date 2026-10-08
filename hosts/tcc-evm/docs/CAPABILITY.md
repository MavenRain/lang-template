# TinyCC EVM host capability

This file holds host facts only. Put the facts of your domain in the
`probe/CAPABILITY.md` of your language. Paths are relative to the host
kit, or to the root of a generated language. The numbers were measured
on 2026-10-08 on AArch64 Darwin with evm 1.14.12.

## Toolchain

- TinyCC 0.9.28rc (AArch64 Darwin) builds `build/langc` and
  `build/asm-selftest` with `-std=c99 -Wall -Werror` (`Makefile:4`).
  `PIN` has the exact version.
- `cc -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only` checks the same
  sources (`make check-clang`, `Makefile:5`). Each switch on an enum names
  every case and has no default arm.
- `tcc -run gen/embed.c` turns `domain/domain.lang` into `build/domain.c`
  at build time (`Makefile:17`). The domain is part of the executable.
- The kit uses the C99 `uint64_t` type for `Nat`. Unsigned arithmetic in
  C wraps modulo 2^64, so the evaluator and the IR use explicit carry
  tests to find an overflow.
- `src/evm.c` is a two-pass assembler. Pass 1 gives each label an
  address. It repeats until no label push becomes wider, because a jump
  target past byte 255 needs a wider `PUSH` (`src/evm.c:154`). Pass 2
  writes the bytes (`src/evm.c:176`). It needs no library.
- `src/keccak.c` computes keccak256 for the selectors.
  `build/asm-selftest` checks `keccak256("")` and the selector `a9059cbb`
  of `transfer(address,uint256)`.
- The gate runs the code in `evm run` (go-ethereum) through
  `test/run-evm.py`, with one `evm` process for each call.

## Values

- `Nat` is an unsigned 64-bit integer. The lexer refuses a literal larger
  than 18446744073709551615 (`test/parse/nat-range.lang`).
- `natAdd` and `natMul` trap on overflow. `natSub` stops at 0. A trap is
  lazy: it stops a computation only when the computation needs it.
  `README.md` gives the rule and the reason.
- There is no text type. The lexer refuses `"` (`test/parse/string.lang`).
- On the EVM, each value is a 32-byte word and a trap flag. Each word is
  less than 2^64. Only the entry result is strict.

## Overflow tests on the EVM

- `ADD` and `MUL` keep the low 64 bits with `AND 2^64 - 1`
  (`src/evm.c:223`, `src/evm.c:227`).
- `ADD_CARRY` is `2^64 - 1 < a + b` (`src/evm.c:232`). `MUL_CARRY` is
  `2^64 - 1 < a * b` (`src/evm.c:234`). The tests are exact: each operand
  is less than 2^64, so the 256-bit sum and the 256-bit product do not
  wrap.
- `SUB` is `(a >= b) * (a - b)` (`src/evm.c:218`).
- The differential grid holds the edge values (`test/grid.awk:8`). evm
  and `langc eval` agree: `scale 4294967296 4294967296` is `trap`,
  `scale 4294967295 4294967297` is 18446744073709551615, and
  `addPrice 18446744073709551615 1` is `trap`.

## Limits

- The checker and the evaluator stop at a depth of 2000
  (`src/front/check.c:10`, `src/front/core.h:16`). The parser stops at a
  nesting of 1000 (`src/front/parser.c:16`). These limits keep the C
  stack bounded. `test/gate.sh` checks a nesting of 1100 parentheses.
- The evaluator has a fuel of 20000000 steps (`src/front/core.h:17`).
  For this reason the differential grid uses values of 2^32 and more
  only for entries with no loop.
- All memory of `langc` comes from one arena with a limit of 1 GiB
  (`src/main.c:11`). A run that reaches it stops with `OOM`. The
  assembler has its own arena with the same limit (`src/evm.c:55`).
- The assembler stops at a nesting of 4096 with `IR_DEPTH`
  (`src/evm.c:54`). The IR printer stops at 4000
  (`src/front/lower.c:17`).
- A loop over a list spine has a fuel of 2^63 steps
  (`src/front/lower.c:688`). The gas limit stops the loop first.
- Source files are at most 1 MiB (`src/front/front.h:7`).

## Measured numbers

- `make check` runs 683 differential cases on 13 programs, with
  0 mismatches, and 8 edge calls.
- `langc build examples/entries.lang --runtime` writes 757 bytes of
  runtime code for six entries. The creation code is 769 bytes.

## HOST-LIMIT reasons

The IR is first-order (`src/ir.h`). It has words, heap blocks, branches,
counted loops and bounded loops. It has no calls, no function values and
no stack of its own. Two programs need more than that. The lowering
refuses them with a HOST-LIMIT code. These are not defects.

- `REFUSE_RUNTIME_CLOSURE` (`src/front/lower.c:503`): a function value
  must exist at run time, for example as the result of a `fold`. The IR
  has no closures. `test/ir/closure.lang` is the gate case.
- `REFUSE_RUNTIME_TREE` (`src/front/lower.c:990`): a `fold` walks a value
  of a family with two or more recursive fields, and the value exists
  only at run time. A loop walks one spine. A tree needs recursion or an
  explicit stack. `test/ir/tree.lang` is the gate case.

The EVM adds three limits. These are not defects either.

- `EVM_SELECTOR` (`src/evm.c:381`): two entries have the same selector.
  The dispatcher cannot tell them apart, so `build` refuses the program.
  No gate case exists: it needs two names whose keccak256 values start
  with the same 4 bytes.
- Memory gas: the gas cost of `w` words of EVM memory is
  `3 w + w * w / 512`. `evm run` has a default gas of 10^10, so the
  memory stops near 2.2 million words (about 72 MB). A heap that needs
  more reverts with out of gas. On Wasm the same heap can grow to 1 GiB.
  This number is computed, not measured.
- Code size: EIP-170 limits deployed runtime code to 24576 bytes. `build`
  does not check this limit. `examples/entries.lang` gives 757 bytes.
