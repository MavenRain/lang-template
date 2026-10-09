# TinyCC EVM anchor host kit

This kit is a C99 compiler, `langc`, that TinyCC builds. It compiles a
program for one governed log of hash and time pairs to EVM bytecode for one
contract. The domain is in `domain/domain.lang`: members, candidates, a
tally, a constitution, the anchor log and the `anchor`, `verify` and
`amend` and `dispute` operations. `src/main.c:1-10` lists the verbs.

The kit is a faithful port of the anchor-lang compiler. The sections
"Origin" and "Renames" tell what changed.

## Commands

- `make` or `make build`: build `build/langc`, `build/parsetool` and
  `build/evmtool` with `tcc -std=c99 -Wall -Werror` (`Makefile:19`).
- `make check-clang`: check each C file with
  `cc -std=c99 -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only`
  (`Makefile:36-37`).
- `make check`: do `build` and `check-clang`, then run `test/gate.sh`
  (`Makefile:39-40`).
- `make clean`: remove `build/`.
- `build/langc check PROG`: the fate report.
- `build/langc table PROG`: the outcome of each tally.
- `build/langc eval PROG NAME`: the normal form of NAME.
- `build/langc build PROG [--runtime] -o OUT`: the contract as hex.
- `build/langc abi PROG`: the entries of the contract.

Exit 0 is ok, 1 is refused and 2 is usage or IO. An error goes to stderr
as `langc: CODE: DEF: message`.

## Layout

- `src/`: the front end (`arena`, `diag`, `lexer`, `parser`, `printer`,
  `syntax.h`), the checker (`check.c`, `check.h`), the EVM writer
  (`evm.c`, `evm.h`, `keccak.c`, `keccak.h`) and `main.c`.
- `domain/domain.lang`: the prelude. `gen/embed.c` writes it as C to
  `build/domain.c` (`Makefile:21-23`), and `langc` reads no prelude file
  at run time (`src/prelude.h:1-2`).
- `examples/programs/`: 5 programs. `examples/mutants/`: 14 programs with
  one defect each.
- `test/`: 10 test files, `test/evmchain.sh` (sourced by the chain tests),
  `test/evmtool.c`, `test/parsetool.c`, `test/parser-arms.lang` and
  `test/gate.sh`.
- `FORMERS.md`: the type former matrix of this host. The cites `(:N)` in
  it are lines of `domain/domain.lang`.
- `docs/CAPABILITY.md`: what the host can do, with evidence.

## Gate

`make check` runs `test/gate.sh`. It runs one step for each test file, in
the order of the anchor-lang `make test` (`test/gate.sh:20-29`). Each step
prints its own counts. Then it scans the kit for an em-dash or an en-dash.
The summary line is `gate: N failures`, and the gate exits 1 when N is more
than 0.

When `evm` is not on PATH, `test/run.sh`, `test/deploy.sh`, `test/diff.sh`
and `test/laws.sh` run no case. Each one prints `skip: N cases (evm not on
PATH)` and adds the line to `build/test/skips`. The gate empties that file
before the first step. After the summary line, the gate prints the skip
sum, `gate: skip sum N cases`, with ` (evm not on PATH)` when N is more
than 0 (`test/gate.sh:36`). A skip is not a pass and not a fail. With
`evm`, the sum is 0. With no `evm`, the sum is 193 (55 + 17 + 81 + 40), and
the gate shows 0 failures.

The ok counts after the M7 port (2026-10-09) are the same as anchor-lang
chunk 17:

| Test file | ok |
| --- | --- |
| `test/parse.sh` | 46 |
| `test/evm.sh` | 9 |
| `test/check.sh` | 53 |
| `test/table.sh` | 26 |
| `test/eval.sh` | 36 |
| `test/build.sh` | 54 |
| `test/run.sh` | 55 |
| `test/deploy.sh` | 17 |
| `test/diff.sh` | 81 |
| `test/laws.sh` | 40 |
| total | 417 |

## Tools

- `tcc`: TinyCC 0.9.28rc builds the compiler and runs `gen/embed.c`.
- `cc`: clang does `make check-clang` only.
- `evm`: geth `evm` runs the contract in `test/run.sh`, `test/deploy.sh`,
  `test/diff.sh` and `test/laws.sh`.
- `cast`: foundry `cast` is used by `test/evm.sh`, `test/build.sh`,
  `test/run.sh`, `test/diff.sh` and `test/laws.sh`.
- `rg`: the dash scan of `test/gate.sh`.

## Origin

The initial kit was a port of anchor-lang at commit `248705e` (the `src`, `test`,
`tools`, `prelude` and `examples` trees, `formers/tcc-evm.md` and
`probe/CAPABILITY.md`). The anchor-lang checker and front end came from
escrowc, at escrow-lang commits `8351635` and `cfe211b`. This note is the
only place in the kit that names that origin. The other anchor-lang docs
are not in the kit.

M6 refresh (2026-10-09): the 50 mapped compiler, prelude, example and test
files match anchor-lang at `cae26a1`, with the same renames below. The
source repository's `tools/port.py` checks this mapping. It preserves
kit-owned files, including this README and `docs/CAPABILITY.md`, which
are maintained separately. The refresh adds amendment and dispute
examples, guarded EVM entries and their chain tests.

SPEC cites (M7, 2026-10-09): each "SPEC section N" in this kit is a
section of the anchor-lang `SPEC.md` at commit `211dc59`. Choice a keeps
that file out of the kit.

## Renames

The initial port copied each file from `248705e` and changed only the names below.
The line count of each file did not change, so each cite keeps its line
number. The domain word "anchor" (the `anchor` entry, the `Anchored` log,
`AnchorLog`) did not change.

| From | To | Count |
| --- | --- | --- |
| `prelude/Prelude.anc` | `domain/domain.lang` | 9 |
| `tools/embed.c` | `gen/embed.c` | 5 |
| `build/prelude.c` | `build/domain.c` | 4 |
| `anchorc` (whole word) | `langc` | 154 |
| `anchor_` (start of a word) | `lang_` | 106 |
| `ANCHOR_` (start of a word) | `LANG_` | 115 |
| `.anc` (suffix) | `.lang` | 128 |
| the name anchor-lang on line 1 of `src/main.c` and `src/syntax.h` | the language placeholder that `bin/new-lang.sh` rewrites, as in tcc-evm-dao | 2 |

File moves: `prelude/Prelude.anc` to `domain/domain.lang`, `tools/embed.c`
to `gen/embed.c`, each `.anc` file to the same path with `.lang`,
`formers/tcc-evm.md` to `FORMERS.md` and `probe/CAPABILITY.md` to
`docs/CAPABILITY.md`. `Makefile`, `test/gate.sh`, `.gitignore` and this
README are new.

## Choices not ruled by the USER

The USER ruled the host name, the faithful port, the scan excludes
(`build/` only) and the path-limited staging. These choices are defaults:

- a. The source is `git show 248705e:PATH` for each tracked file, not a
  work tree. SPEC.md, design/, formers/FORMERS.md, README.md, the license
  files and .claude/ are not copied.
- b. The path map is the list in "Renames". `.gitignore` has `build/`
  only.
- c. The renames are the table in "Renames". The domain word stays.
- d. One script did the copy and the renames. The language placeholder is
  only at the 2 sites that tcc-evm-dao also has.
- e. The targets have the tcc-evm-dao names: `build`, `check-clang`,
  `check` and `clean`. There is no `test` target, because `test/gate.sh`
  replaces it. The TCC and clang flags and the `FRONT` and `BACK` lists
  are the anchor-lang ones (`Makefile:5-12`).
- f. `test/gate.sh` has the tcc-evm-dao `step()` form (`test/gate.sh:12-15`).
- g. The port kept the silent skips. M7 changed them to a counted skip
  (see "Gate").
- h. This README has the tcc-evm-dao form.
- i. The port did not change anchor-lang.

## Kit debt

- The domain entries are in `src/evm.c`. The tcc-evm-dao split
  (`src/asm.h` for the writer API and `domain/entries.c` for the entries)
  is not done.
