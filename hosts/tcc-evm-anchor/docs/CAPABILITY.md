# anchor-lang host capability: TinyCC to EVM (M7 done, chunk 19, 2026-10-09)

This file records what the `tcc-evm` host can do at the end of chunk 19
(SPEC section 10). The host is the C99 compiler `langc`, built by
TinyCC. The target is EVM bytecode for one contract. Each fact cites a file
or a test. PLANNED work has no evidence yet.

Each "SPEC section N" in this kit is a section of the anchor-lang
`SPEC.md` at commit `211dc59`. That file is not in the kit (README
`## Origin`).

## Compiler

- TinyCC 0.9.28rc (mob 0fb54300, AArch64 Darwin), at
  `/Users/oobi/.local/bin/tcc`. `make` builds `build/langc`,
  `build/parsetool` and `build/evmtool` with `tcc -std=c99 -Wall -Werror`
  (`Makefile`).
- `make check-clang` checks every C file with
  `cc -std=c99 -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only` (Apple
  clang 21.0.0).
- `gen/embed.c` runs under `tcc -run` and writes the prelude as C
  (`build/domain.c`). `test/parse.sh` checks that the embedded prelude
  is equal to `domain/domain.lang`.
- Verbs (`src/main.c`): `langc check|table|abi PROG`,
  `langc eval PROG NAME`, `langc build PROG [--runtime] -o OUT`. Each
  verb parses and checks the embedded prelude and PROG. `check` prints the
  fate report and `table` prints the table (SPEC section 7). Each tabulates
  one time. `eval` prints the normal form of a def, else `EVAL_NAME`.
  `abi` prints the entries and the `Anchored` log, and `build` writes
  the hex of the contract with the outcome table (chunk 5, SPEC section
  7). Both tabulate one time; a table refusal stops them with its code.
  For a program with more than one constitution (O3, chunks 10 and 11),
  `abi` also prints the `amend` entry and the `Amended` log, and `build`
  writes the slot K + M, the C R rows and the policy records with the
  `amendTo` mask (`test/build.sh`, `test/run.sh`). When some policy has
  a `window` > 0 (O7, chunk 13), `abi` also prints the `dispute` entry
  and the `Disputed` log. `build` writes the `window` in each policy
  record: 17 bytes when C = 1, or 18 bytes with the `amendTo` mask when
  C > 1 (`src/evm.c`, `test/build.sh`, `test/run.sh`).

## Limits

- One run uses one arena of at most `LANG_ARENA_MAX` = 256 MiB
  (`src/syntax.h`), far under the 4 GB limit for a run.
- The parser and the AST nest at most `LANG_DEPTH_MAX` = 512 levels
  (`PARSE_DEPTH`; `src/syntax.h`, `test/parse.sh`).
- The checker has `CHECK_FUEL` = 2^24 evaluation steps for each
  declaration and for the fork check (`TYPE_FUEL`, `src/check.c`).
- A program has at most 8 constitutions (`AMEND_LIMIT`,
  `examples/mutants/amend-limit.lang`). With C constitutions and R
  tallies, a table has at most 4096 rows, C R (`TABLE_LIMIT`,
  `test/table.sh`). A constitution k >= 1 that is not the value of
  `constitutionOf r` (the general path) builds a profile of M ballots
  for each row. With 2 candidates, 282 members tabulate
  (`test/table-general-282.lang`). From 283 to 815 members, the arena is
  full (`MEMORY`, `test/table-general-283.lang`). With 816 members and
  more, the evaluation goes past `CHECK_DEPTH` = 4096 nested calls
  (`TYPE_FUEL`). The short path of `constitutionOf r` builds no profile
  and tabulates up to `TABLE_LIMIT` (`test/table-amend-2047.lang`; SPEC
  section 7).
- Universes: `Type 0` has the type `Type 1`, and `Type 1` has no type
  (`TYPE_UNIVERSE`). An explicit `Type 1` annotation is refused. Pi,
  Sigma, product and sum formation take the maximum universe of their
  constituents (`src/check.c`, `infer_bind` and `infer_body`). Besides
  `Option : Type 0 -> Type 0`, definitions such as
  `packed : (A : Type 0) * A := (Nat, 7)`,
  `types : prod (Type 0, Type 0) := tuple (Nat, Flag)` and
  `typeSum : sum (Type 0, Type 0) := inj 0 of 2 Nat` check.
- Recursion is structural only, and only the prelude has it: a `def rec`
  is `fun ... => match` on one of its parameters, and each recursive call
  has a smaller argument (`TYPE_REC`). `Nat` has no eliminator (prelude
  note P10). Thus the host has no unfold (F7).
- The surface has no axiom form (SPEC section 2).
- For one constitution, a table holds at most 4096 tallies, C(members + K - 1,
  K - 1) for K candidates (`src/check.c`; `test/table.sh`: 4095 members
  and 2 candidates pass, 4096 members are `TABLE_LIMIT`).
- The runtime code with the table bytes is at most `EVM_RUNTIME_MAX` =
  24576 bytes (EIP-170). The creation code with the member words is at
  most `EVM_INITCODE_MAX` = 49152 bytes (EIP-3860). Else `EVM_SIZE`, and
  the message names the bound. The table grows with the members, so the
  edges are not fixed numbers: `test/build.sh` finds the EIP-3860 edge of
  `arrow-debreu.lang` by bisection, and `test/evm.sh` tells the two bounds
  apart by the message at the EIP-170 edge with 2 candidates. 0 members
  or 0 candidates is `EVM_LIMIT`.
- The test driver `build/evmtool` clamps its table at 65536 rows
  (`test/evmtool.c`).

## Carried from the origin compiler

The origin compiler is the compiler of SPEC section 8.

- Front end: lexer, parser, printer, arena and diagnostics (`src/`). The
  parse refusals keep their codes: `LEX_TOKEN`, `LEX_NUMBER`,
  `PARSE_PAREN`, `PARSE_EXPECT`, `PARSE_ARITY`, `PARSE_DEPTH`
  (`test/parse.sh`).
- Keccak-256 (`src/keccak.c`). `test/evm.sh` checks the vectors.
- EVM assembler (`src/evm.{h,c}`): opcodes, PUSH widths, labels 0 to 63
  with two passes for the PUSH2 offsets, the dispatch by the selector of a
  signature, and the creation code. Chunk 5a adds the constructor, the entries and
  `lang_abi_write` (SPEC section 7). The target interface is
  `lang_evm_write`. Codes: `EVM_LIMIT`, `EVM_SIZE`,
  `EVM_USAGE`, `EVM_IO`, `EVM_INTERNAL`.

## Checker

`src/check.{h,c}` is a bidirectional checker with conversion by
normalization, ported from the origin compiler. It checks the embedded
prelude, then the program. The core names of prelude note P1 (`Hash`,
`Time`, `AnchorLog`, `logMember`, `logInsert`, `hashNonZero`) are opaque
globals: they do not reduce.

Program refusals (SPEC section 2):

| Code | Cause | Evidence |
|---|---|---|
| `REFUSE_MEMBERS` | the program does not start with `members` | `test/check.sh` |
| `REFUSE_DATA` | a `mu` in a program | `examples/mutants/data-decl.lang` |
| `REFUSE_REC` | a `def rec` in a program | `examples/mutants/rec-def.lang` |
| `REFUSE_NAME` | a program defines a prelude name or a core name | `examples/mutants/prelude-name.lang`, `examples/mutants/core-name.lang` |
| `REFUSE_FORK` | a `two p q` side that is not frozen, or a fork that the check cannot see | `examples/mutants/fork-unfrozen.lang`, `examples/mutants/amend-fork-unfrozen.lang`, `test/check.sh` |
| `REFUSE_AMEND` | `amendments` with no `amendTo`, or `amendTo` with no `amendments` | `examples/mutants/amend-no-to.lang`, `examples/mutants/amend-to-only.lang` |
| `REFUSE_AXIOM` | cannot be reached: the parser gives `PARSE_EXPECT` | `test/check.sh` |

Type codes: `TYPE_SCOPE`, `TYPE_MISMATCH`, `TYPE_SHAPE`, `TYPE_MATCH`,
`TYPE_ERASED`, `TYPE_INFER`, `TYPE_DUPLICATE`, `TYPE_UNIVERSE`, `TYPE_MU`,
`TYPE_REC`, `TYPE_NAT`, `TYPE_FUEL`, `TYPE_INTERNAL`. The mutants
`hash-projection.lang` (`TYPE_SHAPE`) and `log-match.lang` (`TYPE_MATCH`)
cover the opaque core names. `rule-type.lang` (`TYPE_MISMATCH`) covers the
type of `rule`.

The fork check is conservative (SPEC section 2). It explores each `case`
and `match` arm of the outcomes of `rule` and of their freeze flags. Chunk 4
tabulates every constitution and makes the full fork check.

## Planned

M6 is done (SPEC section 10, chunks 9 to 14): the `amend` entry (O3), the
`dispute` entry (O7) and a port of those changes into this kit.

M7 is done (SPEC section 10, chunks 15 to 19). Chunk 15 adds the short
path of `constitutionOf r`. Chunk 16 gives the measured cause of the limit
of the general path, with pins at 282 and 283 members (see `## Limits`).
Chunk 17 adds a counted skip to the 4 chain test files. Chunk 18 ports M7
into this kit. Chunk 19 corrects the text of this file and of the kit
README. The limit of SPEC section 7 is final.
Nothing is planned after M7.

## Gates (2026-10-09)

All GREEN on the chunk 19 kit:

- `make build` (tcc `-Wall -Werror`).
- `make check-clang`.
- `make check`: `test/gate.sh` runs the ten test scripts. Their case
  counts are listed in the README: 417 cases in all, including six
  differential traces and the amendment and dispute laws. The four
  chain test files run on geth `evm` 1.14.12. The summary line is
  `gate: 0 failures`.
- The skip sum (`gate: skip sum N cases`, after the summary line): 0 with
  `evm`, and 193 with no `evm` on PATH (55, 17, 81 and 40 cases in the
  four chain test files).
