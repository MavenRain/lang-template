# TinyCC EVM host kit

This kit compiles a small typed language to EVM bytecode. One C99
program, `build/langc`, holds the parser, the checker, the evaluator, the
lowering to a first-order IR and the EVM assembler. TinyCC builds it.

## Shared files

This kit and `hosts/tcc-wasm` share these files. `TCC_SHARED` in the root
`Makefile` lists them:

- the source: the front end (`src/front/`), `src/ir.h`, `src/target.h`
  and `src/main.c`
- the domain and its embed tool: `domain/` and `gen/`
- the examples: `examples/`
- the shared tests: `test/parse/`, `test/check/`, `test/eval/`, `test/ir/`,
  `test/grid.awk`, `test/review.sh` and `test/fronttool.c`
- `.gitignore`

`make check` at the template root compares each shared entry in the two
kits with `diff -r`. It fails when a shared file is different in the two
kits or is missing from one kit. When you change a shared file, make the
same change in `hosts/tcc-wasm`.

These files are specific to this kit:

- the target files: `src/evm.c`, `src/keccak.c` and `src/keccak.h`
- the target test files: `test/run-evm.py`, `test/prestate.json` and
  `test/asm-selftest.c`
- the build and gate files: `Makefile`, `test/gate.sh` and `PIN`
- the docs: `README.md`, `FORMERS.md` and `docs/CAPABILITY.md`

## Build

From the template root:

```sh
make -C hosts/tcc-evm build
make -C hosts/tcc-evm check
make -C hosts/tcc-evm check-clang
```

`build` runs `tcc -run gen/embed.c` to copy `domain/domain.lang` into
`build/domain.c`. Then it builds `build/langc` and `build/asm-selftest`
with `tcc -std=c99 -Wall -Werror`. The domain is part of the executable.
`check-clang` compiles the same sources and `test/asm-selftest.c` with
`cc -std=c99 -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only`.
`check` does both steps, then runs `test/gate.sh`.
The gate needs `evm` (go-ethereum) and `python3` on the `PATH`.
`PIN` records the tool versions of the last GREEN gate.
The build and the gate need no network access.

## Source language

A program is a sequence of `def NAME : TYPE := TERM`. A line comment
starts with `--`. Lambdas are `fun (x : A) => t`. There is no text type.

Core names: types `Nat Flag Prod Sum Option List Eq`, values
`flagYes flagNo pair first second inl inr either none some option nil cons
pure map bind fold unfold filter pack witness payload refl symm trans
transport cong`, Nat operations `natAdd natSub natMul natEq natLe`, and the
Flag eliminator `flagIf b yes no`. `Type 0 : Type 1` is the universe
chain. `Sigma (x : A) B` and `(x : A) * B` are dependent pairs.
`FORMERS.md` gives the status of each former.

The compiler enforces these rules:

| Rule | Code | The compiler refuses |
|---|---|---|
| R1 | `REFUSE_DATA` | a `family` declaration in a program |
| R2 | `REFUSE_AXIOM` | an `axiom` declaration |
| R3 | `REFUSE_REC` | `def rec`, or a definition that refers to itself or to a later definition |
| R4 | `REFUSE_NAME` | a core name, a domain name, a reserved name or a repeated name as a new definition |

## CLI

```sh
build/langc check PROG
build/langc eval PROG NAME [ARGS...]
build/langc ir PROG
build/langc abi PROG
build/langc build PROG -o OUT [--runtime]
```

- `check` prints `ok`, or refuses the program.
- `eval` prints the normal form of `NAME`. Each argument is a decimal
  number. When an entry traps, `eval` prints `trap` on stdout and an
  `EVAL_OVERFLOW` line on stderr, and the exit status is 0.
- `ir` prints the first-order IR. It has one `func` for each entry.
- `abi` prints one line for each entry: the name and the selector in
  hexadecimal, for example `addPrice 57279353`.
- `build` writes the creation code to `OUT` as hexadecimal text, with a
  newline at the end. With `--runtime`, it writes the runtime code.
  `evm --code CODE run` accepts this text.

The exit status is 0 for success, 1 for a refused program and 2 for a
usage or IO error. A diagnostic on stderr has the form
`langc: CODE: DEF: message`.

## Entries

An entry is a top-level program definition whose declared type is `Nat`,
`Flag`, or a first-order function from `Nat` and `Flag` arguments to a
`Nat` or `Flag` result. Other definitions are not entries.

The selector of an entry is the first 4 bytes of
`keccak256("NAME(uint256,...)")`, with one `uint256` for each parameter
(`src/evm.c:353`). The calldata is the selector, then one 32-byte
big-endian word for each argument. The result is one 32-byte word.
`test/run-evm.py` makes the calldata from a `NAME ARGS` line and the
`abi` output, and runs the code in `evm run`.

`Nat` is an unsigned 64-bit integer. `natAdd` and `natMul` trap on an
overflow past 2^64 - 1. `natSub` stops at 0. `Flag` is 0 or 1.

## Compilation

Compilation is partial evaluation. The compiler normalizes the body of
each entry with the parameters as free variables. Types and universes erase.
Proof contents erase, while their trap flags remain. A lambda stays only as the direct function argument of
`fold`, `unfold`, `map`, `bind`, `filter`, `either` or `option`. The
lowering puts its body into a loop or a branch. The lowering refuses two
programs with a HOST-LIMIT code (see `docs/CAPABILITY.md`):

- `REFUSE_RUNTIME_CLOSURE`: a function value must exist at run time.
- `REFUSE_RUNTIME_TREE`: a fold walks a tree that exists only at run time.

## Lazy traps

A trap is lazy. It stops a computation only when the computation needs
the value. `langc eval` and the EVM code agree on this rule. The
lowering gives each value a word and a trap flag. A heap field keeps its
word and its flag. The entry result is the only strict point: when its
flag is set, the entry reverts with no data.

Strict traps are not correct here. Normalization at compile time drops
terms that are not read, for example `first (pair a b)` gives `a`. The
evaluator also computes both arms of `flagIf`. A strict trap would fire
in a term that the program does not use.

Decision record: the kit builder (Claude) made this decision. The
repository owner has not ruled on it.

## EVM layout

- The runtime code starts with the dispatcher (`src/evm.c:390`). It
  reverts when the call has a value, when the calldata is shorter than
  4 bytes and when no entry has the selector.
- An entry reverts when the calldata is shorter than 4 + 32 N bytes for
  N parameters, when a `Nat` argument is larger than 2^64 - 1 and when a
  `Flag` argument is larger than 1 (`src/evm.c:340`).
- A trap is `PUSH1 0 DUP1 REVERT` (`src/evm.c:214`). The kit does not
  use `PUSH0`, so the code does not need the Shanghai fork.
- A word is 32 bytes. Each value is less than 2^64. Memory word 0 holds
  the heap pointer. Local K is at byte 32 + 32 K. The heap starts after
  the last local. Each entry first sets the heap pointer to the start of
  the heap, so each call starts with an empty heap.
- A heap block holds the tag in word 0, the word of field K in word
  1 + 2K and the trap flag of field K in word 2 + 2K.
- A tag switch is a chain of `DUP1 PUSH k EQ JUMPI`. A tag out of range
  reverts (`src/evm.c:274`).
- The creation code is `PUSH size DUP1 PUSH start PUSH1 0 CODECOPY
  PUSH1 0 RETURN`, then the runtime code (`src/evm.c:415`). It copies the
  runtime code into memory and returns it.
- When two entries have the same selector, `build` refuses the program
  with the code `EVM_SELECTOR` (`src/evm.c:381`). This is a HOST-LIMIT.

## Domain slot

`domain/domain.lang` holds the families and the operations of the domain.
Only this file can declare a `family` (rule R1). `domain/README.md` tells
how to replace the sample domain.

## Gate

`make -C hosts/tcc-evm check` prints these lines when it is GREEN:

| Line | What the gate does |
|---|---|
| `examples: 10 checked, 0 failed` | `langc check` accepts each `examples/*.lang` file |
| `parse refusals: 9 checked` | each `test/parse` file fails with the code in `test/parse/expect.txt`; one file has 1100 nested parentheses |
| `check refusals: 19 checked` | each `test/check` mutant fails with the code in `test/check/expect.txt`; the `TYPE_REFL` message shows both normal forms |
| `evals: 25 checked` | `langc eval` gives each result in `test/eval/expect.txt` |
| `ir: 10 lowered` | `langc ir` lowers each example, with one `func` for each `abi` line |
| `selftest: 9 snippets` | `build/asm-selftest` checks two keccak256 vectors and prints 9 assembler snippets (jumps, a label past byte 255, the 64-bit wrap and carry, a revert); each snippet runs in `evm` and gives the expected word |
| `evm: 13 programs deployed` | `langc build` writes the creation code and the runtime code for each example and for `test/ir/spine.lang`, `test/ir/lazy.lang` and `test/ir/proof-trap.lang`; `evm --create run` of the creation code returns the runtime code; `abi` has one `NAME SELECTOR` line for each `func` and no repeated selector |
| `differential: 683 cases, 0 mismatches` | `evm` runs each entry on the grid of `test/grid.awk`; each result is equal to the `langc eval` result, and a trap is equal to a trap |
| `evm: 8 edge calls` | calls on `examples/entries.lang` that `langc eval` does not cover: `pick 2 4 9`, an argument of 2^64, an argument of 2^64 - 1, an unknown selector, 1 byte of calldata, a selector with no arguments, and one call with value 0 and with value 1 |
| `gate: 0 failures` | the sum of all failures |

For the call value, `test/prestate.json` gives the default sender of
`evm run` a balance. With value 1 the call reverts. With value 0 the same
call gives the result, so the revert comes from the dispatcher.

The gate also checks the usage and IO exit codes, five `eval` refusals,
`test/ir/closure.lang` (`REFUSE_RUNTIME_CLOSURE`), `test/ir/tree.lang`
(`REFUSE_RUNTIME_TREE`), the 13 cases of `test/ir/spine.lang`, and the
absence of em-dashes and en-dashes in the kit.
`perl bin/doc-check.pl hosts/tcc-evm` must report 0 problems.

## Known limits

- A `fold` over a bare `nil` cannot infer its type (`TYPE_INFER`). Use a
  definition with a declared `List` type.
- With no expected type, `either`, `map` and `cong` take the result type
  of the function at a fresh variable. A dependent result needs a
  declared type.
- A definition with a `List`, `Prod` or other non-scalar result is not an
  entry. `ir` and `abi` skip it.
- `langc eval` stops after 20000000 steps (`EVAL_FUEL`). The EVM code
  has no step limit, but each step costs gas. A large loop count can run
  on the EVM and stop in `eval`. The grid uses large values only for
  entries with no loop.
- The arena of `langc` stops at 1 GiB. The EVM memory has no fixed
  limit, but its gas cost grows with the square of its size
  (`docs/CAPABILITY.md`).
- `build` does not check the EIP-170 limit of 24576 bytes of runtime
  code.
- Source files are at most 1 MiB. `Nat` literals are at most
  18446744073709551615.
