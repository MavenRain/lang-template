# TinyCC EVM DAO host kit

This kit compiles a small dependently typed language for a
self-constituting DAO to EVM bytecode. A C front end (lexer, parser and
NbE checker) and an EVM writer build with TinyCC into one executable,
`build/langc`. The checker evaluates the constitution of the program at
each tally. The writer puts these decisions into a verdict table in the
contract. There is no IR.

From this kit directory, or from a generated language directory:

```sh
make check
build/langc check examples/arrow-debreu.lang
build/langc table examples/arrow-debreu.lang
build/langc verdicts examples/arrow-debreu.lang F
build/langc build examples/arrow-debreu.lang -o debreu.hex
build/langc build examples/arrow-debreu.lang --runtime -o debreu-runtime.hex
```

The build uses TinyCC (`TCC`, default `tcc`). `make check-clang` also
compiles the sources with `cc -Wall -Wextra -Wswitch-enum -Werror
-fsyntax-only` (`CC`, default `cc`). The gate needs `python3`, geth `evm`,
foundry `cast` and `rg`. The versions are in `docs/CAPABILITY.md`.

Names in comments: "host README" is this file (`docs/host/README.md` in a
generated language). "host CAPABILITY.md" is `docs/CAPABILITY.md`
(`docs/host/CAPABILITY.md`). "host FORMERS.md" is `FORMERS.md`
(`formers/tcc-evm-dao.md`).

## Commands

| Command | Result |
|---|---|
| `langc check PROG` | Checks the program. Prints `ok debreu` or `ok impossibility` (the regime). |
| `langc table PROG` | Prints the regime, the member count and the decision code of each tally, on one line. |
| `langc verdicts PROG NAME` | Prints one decision code for each ballot vector of the ChoiceRule `NAME`, compact digits for k <= 9 or space-separated decimal codes for k >= 10. |
| `langc eval PROG NAME` | Prints the normal form of `NAME`. |
| `langc build PROG [--runtime] -o OUT` | Checks the program and writes the creation code (or the runtime code with `--runtime`) to `OUT` as lowercase hex, without `0x`, with a final newline. `--runtime` comes before `-o`. |

Exit 0 is success. Exit 1 is a refused program. Exit 2 is a usage or IO
error. A refusal writes one line to stderr: `langc: CODE: NAME: message`.

## Layout

| Path | Part | Content |
|---|---|---|
| `src/` | core | Lexer, parser, printer, checker (`check.c`), EVM core (`evm.c`), the assembler API for domains (`asm.h`), keccak, arena, diagnostics |
| `domain/domain.lang` | domain | The prelude: the types and operations of the language. `gen/embed.c` embeds it in `build/langc` at build time. |
| `domain/entries.c` | domain | The storage layout and the contract entries of each regime |
| `examples/` | sample | The two sample programs, one for each regime |
| `test/` | gate | `test/gate.sh` and its tests |

A new language edits `domain/domain.lang` and `domain/entries.c`. It does
not edit `src/`.

## The domain

A program holds `def members : Nat := N` (N >= 1) as its first
declaration. The checker reads the members declaration, the prelude and
then the rest of the program. A program cannot declare a `mu`, declare a
`def rec` or redeclare a prelude name. Thus each family and each recursive
function is in `domain/domain.lang`. Each `-- @section` line of the
prelude starts one section. Sections 1 to 6 are the governance core
(decisions, equality, Option and Sum, ballots, tallies, constitution,
aggregation and amendment). Sections 7 and 8 are the sample domain.

The decision space D is the codomain of `ChoiceRule` in the prelude. It
must be a `mu` with no indices and 2 to 64 nullary constructors
(`TABLE_DECISION`). Let k be the constructor count. Code j is constructor
j, from 1, in declaration order. A ballot is a code in 1 to k. A tally
counts each code over the n members. The tallies are the compositions of
n into k parts, in lexicographic order on parts 1 to k-1. The last part is
the remainder. There are C(n+k-1, k-1) tallies.

The regime comes from the program. A program that defines an
`Aggregation G` for a ChoiceRule `G` of the program is Arrow-Debreu: at a
discrete decision space an aggregation of `G` is an orbit rule and a proof
that `G` factors through it. The writer puts the decision of each tally in
a verdict table. Any other program is Arrow-impossibility, with no verdict
table.

`domain/entries.c` gives `lang_domain_entries(regime, &count)` from
`src/asm.h`: the entries of a regime in dispatch order (1 to 16). Each
`Entry` has:

- `name`: the function name of the selector;
- `words`: the count of `uint256` calldata words before the ballots;
- `ballots`: 1 if n ballot words follow (Arrow-Debreu only), else 0;
- `payment`: `ENTRY_PAYABLE` or `ENTRY_NONPAYABLE`;
- `emit`: the function that writes the body.

The core writes the dispatcher, then for each entry a head (callvalue
guard unless payable, calldata size check), then calls `emit`. An unknown
selector reverts. `emit` gets an `EntryContext`: `members` (n), `decisions`
(k, or 0 in Arrow-impossibility) and `packed` (the amend word, Arrow-Debreu
only). The core entries `lang_entry_cast` and `lang_entry_amend` can go in
a list. The `asm_*` helpers of `src/asm.h` write opcodes, pushes, labels
(64 for each contract), jumps, calldata words, mapping slots
(keccak(key . slot)), checked addition, memory words, an address guard and
`asm_tally` (the decision code of the n ballots from the verdict table).
Memory 0x00 to 0x3f is core scratch. A domain uses 0x80 and up.

`test/domains/k4.lang` and `test/domains/decision-arg.lang` are the sample
prelude with a fourth decision value (nullary, then with an argument).
They test k = 4 and `TABLE_DECISION`. A new domain must update them, or
remove them from `DOMAINS` in the `Makefile`. The small
`test/domains/review-k10-domain.lang` checks decimal verdict output for
k = 10. `test/settlement.py` and the
entry sequences of `test/differential.py` model the sample domain. A new
domain replaces them.

## The sample domain

The sample is an escrow. The decision space is `Decision` = {release,
refund, hold}, so k = 3.

Storage: slot 0 is the ledger (address to balance), slot 1 is the claim
count, slots 2, 3 and 4 are the payer, the payee and the amount of each
claim. Mappings live at keccak(key . slot).

| Entry | Regime | Calldata | Effect |
|---|---|---|---|
| `deposit` (payable) | both | 3 words | Guards the two addresses, requires amount <= callvalue, credits the payer, adds a claim and returns its index. |
| `cast` | Arrow-Debreu | n ballots | Returns the decision code of the ballots. |
| `settle` | Arrow-Debreu | claim index, n ballots | Requires amount <= the payer balance. Code 1 (release) moves the amount from the payer to the payee. Code 2 (refund) debits the payer. Other codes hold. Returns the code. |
| `amend` | Arrow-Debreu | none | Returns the packed verdict table word. |

Limits of the sample (not of the core): `settle` does not close the claim,
so one claim can settle more than one time while the payer balance covers
it. A refund removes the amount from the ledger with no recipient, and no
entry sends funds out of the contract.

## Refusals

| Codes | Cause |
|---|---|
| `REFUSE_MEMBERS` | The first declaration is not `def members : Nat := N` with N >= 1 |
| `REFUSE_MU`, `REFUSE_REC`, `REFUSE_PRELUDE_NAME` | The program declares a `mu`, a `def rec` or a prelude name |
| `REFUSE_FORM` | A refused assay form: `nu`, `axiom`, `contract`, `storage`, `entry`, `payable`, `constructor`, `fallback`, `error`, `invariant`, `predicate`, `proof`, `guard`, `sload`, `sstore` |
| `LEX_TOKEN`, `LEX_NUMBER`, `PARSE_EXPECT`, `PARSE_PAREN`, `PARSE_ARITY`, `PARSE_DEPTH` | Lexer and parser errors |
| `TYPE_SCOPE`, `TYPE_DUPLICATE`, `TYPE_MISMATCH`, `TYPE_SHAPE`, `TYPE_ERASED`, `TYPE_MATCH`, `TYPE_UNIVERSE`, `TYPE_INFER`, `TYPE_REC`, `TYPE_MU`, `TYPE_NAT`, `TYPE_FUEL` | Checker errors. `TYPE_NAT` is a Nat overflow. `TYPE_FUEL` is out of fuel or too deep. |
| `TABLE_DECISION`, `TABLE_STUCK`, `TABLE_LIMIT` | D is not a valid decision space, the rule does not reduce at a tally, or too many members or tallies |
| `VERDICT_TYPE`, `VERDICT_LIMIT` | `NAME` is not a ChoiceRule, or k^n is more than 59049 |
| `EVM_LIMIT`, `EVM_TABLE`, `EVM_SIZE` | Too many members for the amend word, a bad verdict table, or the code is too large |
| `EVM_INTERNAL`, `EVM_USAGE`, `EVM_IO` | A writer fault, a bad writer call, or an output error |
| `MEMORY`, `IO_READ`, `IO_SIZE`, `IO_WRITE`, `USAGE`, `TYPE_INTERNAL` | Arena full, file errors, bad arguments, checker fault |

`test/refusal.sh` holds one program for each `REFUSE_*` code and one for
each checker error.

## Gate

`make check` builds `build/langc`, `build/parsetool` and the test domain
compilers, runs `make check-clang`, then `test/gate.sh`. The gate runs, in
order: `test/parse.sh`, `test/embed-safety.sh`, `test/check.sh`, `test/refusal.sh`,
`test/normal-forms.py`, `test/differential.py` (k = 3, geth against
`langc table` and `langc verdicts`), `test/domains.sh`,
`test/differential.py --langc build/k4/langc --decisions 4 --program
test/domains/plural4.lang`, `test/settlement.py` (60 ledger cases in geth)
and a scan for en and em dashes. It ends with `gate: 0 failures`.
`make clean` removes `build/` and `.gatework/`.

## Limits

| Limit | Value | Source |
|---|---|---|
| Source size | 1 MiB | `src/syntax.h:36` |
| Arena | 256 MiB | `src/syntax.h:37` |
| Parser nesting | 512 | `src/syntax.h:35` |
| Checker depth | 4096 | `src/check.c:16` |
| Checker fuel, for each declaration, tally and ballot vector | 2^24 steps | `src/check.c:15` |
| Members for `table` | 1000 | `src/check.c:18` |
| Tallies for `table` | 501501 | `src/check.c:19` |
| Ballot vectors for `verdicts` | 59049 (3^10) | `src/check.c:17` |
| Decision values k | 2 to 64 | `src/evm.h:7` |
| Members for `build` (Arrow-Debreu) | largest n with C(n+k-1, k-1) * ceil(log2(k+1)) <= 256 (k = 3: 14) | `src/evm.c:374` |
| Verdict table | n(n+1)^(k-2) + 1 bytes, at most 4096 | `src/evm.c:382` |
| Code buffer, fixups, labels | 8192 bytes, 512, 64 | `src/asm.h:17` |
| Entries for each regime | 16 | `src/evm.c:22` |
| Runtime code | 24576 bytes (EIP-170) | `src/evm.c:22` |

## Origin

This kit is a fork of escrowc at escrow-lang commit `b55630b` (the
`Makefile`, `src`, `test`, `tools`, `prelude` and `examples` trees). The
escrow-lang docs are not in the kit. Their host notes that still apply are
in `docs/CAPABILITY.md`. Changes after the fork:

- Names follow the sibling TinyCC kits: `build/langc`, the diagnostic
  prefix `langc: `, `.lang` files, `domain/domain.lang` embedded by the
  shared `gen/embed.c`, the targets `build`, `check`, `check-clang` and
  `clean`, and `test/gate.sh`.
- The decision space is the codomain of `ChoiceRule` with k values, not a
  fixed three. The tally, the verdict table, the ballot weights and the
  amend packing take k.
- The EVM writer is split: `src/evm.c` is the core, `src/asm.h` is its API
  for domains, and `domain/entries.c` holds the escrow entries.

At k = 3 the tables, the creation code and the runtime code of both
examples are byte-identical to escrowc at `b55630b`.

`FORMERS.md`, copied to `formers/tcc-evm-dao.md` in a generated language,
gives the status of each former. `docs/CAPABILITY.md` gives the host
facts.
