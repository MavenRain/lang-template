# Type formers on the tcc-evm host

This file is the `tcc-evm` host column of `formers/FORMERS.md` for
anchor-lang (SPEC section 3). The status words are those of
`formers/FORMERS.md` section 4. A status is for the checker of chunk 3: the
checker accepts the former and checks its rules. Chunk 4a evaluates `rule`
at each tally (`langc table`). The contract writer of chunk 5
writes the constructor, the entries and the outcome table. No former runs
on the EVM: the runtime reads the outcomes from the table.

The evidence is a prelude definition (`domain/domain.lang`, cited as
`:LINE`), a case of `test/check.sh` or a mutant of `examples/mutants/`.
Each run of the checker checks the whole prelude (`test/check.sh`, case
"the prelude checks").

| ID | Former | Status | Evidence |
|---|---|---|---|
| F1 | Product | DONE | Built-in `prod`, `tuple`, `.0`, `.1`. `AnchorPair := prod (Hash, Time)` (`:213`); `Flag := sum (prod (), prod ())` (`:14`). Mutant `hash-projection.lang`: a projection on a term that is not a pair is `TYPE_SHAPE`. |
| F2 | Coproduct | DONE | Built-in `sum`, `inj`, `case`. `Flag`, `flagNo`, `flagYes`, `flagAnd` (`:14-20`). The conditional fork cases of `test/check.sh` branch with `case`. |
| F3 | Option | DONE | `Option A := sum (prod (), A)` (`:27`), `optionNone`, `optionPure` (`:29-32`); `candidateAt` gives an `Option Policy` (`:115`). |
| F4 | List | PARTIAL | One `mu` family for each element type: `Candidates` of `Policy` (`:101`), `Profile` of `Ballot` (`:130`). There is no generic `List A`. |
| F5 | Monad | PARTIAL | Over `Option` only: `optionPure`, `optionBind`, `optionMap` (`:32-41`). The list families have no bind. |
| F6 | Algebra fold | PARTIAL | A structural `def rec` in the prelude only: `candidatesFold` (`:105`), `profileFold` (`:134`). A program uses these folds; a `def rec` in a program is `REFUSE_REC` (mutant `rec-def.lang`). There is no fold on `Nat` (prelude note P10). |
| F7 | Algebra unfold | HOST-LIMIT | The checker accepts only structural recursion (`TYPE_REC`), and `Nat` has no eliminator, so an unfold has no measure and no fuel (`probe/CAPABILITY.md`, Limits). |
| F8 | Filterable filter | PARTIAL | Over `Profile` only: `profileFilter : (Ballot -> Flag) -> Profile -> Profile` (`:140`). |
| F9 | Pi, not dependent | DONE | Built-in. `rule : Tally -> Outcome` in each example program. Mutant `rule-type.lang`: a rule of type `Nat -> Outcome` is `TYPE_MISMATCH`. |
| F10 | Pi, dependent | DONE | `(0 B : Type 0) -> ...` in `candidatesFold` (`:105`); `amendKeepsGov` (`:248`). The erasure cases of `test/check.sh`: an erased input that a term uses at run time is `TYPE_ERASED`. |
| F11 | Sigma | DONE | `AnchorDAO F := (L : Aggregation F) * AnchorLog` (`:215-216`), read by `s.0` and `s.1` and built by a pair in `anchor` (`:221`) and `amendDAO` (`:251`). |
| F12 | Eq refl, symm, trans | PARTIAL | One family, `EqOutcome` (`:174`), with refl (`sameOutcome`), `symmOutcome` (`:285`) and `transOutcome` (`:290`); `amendKeepsGov` (`:248`) uses refl. There is no generic `Eq`, so the status stays PARTIAL. |
| F13 | Eq transport, cong | PARTIAL | `transportOutcome` (`:280`) and `congOutcome` (`:294`) for `EqOutcome` only, with no heterogeneous cong. Each one is a dependent match on the proof, so it computes by definition at `sameOutcome` (`test/eval.sh`). The origin compiler (SPEC section 8) has one transport and one cong for each Eq family, with the same checker. |
| F14 | Universes | PARTIAL | `Type 0 : Type 1`. Pi, Sigma, product and sum formation take the maximum universe of their constituents (`src/check.c`, `infer_bind` and `infer_body`). Thus `Option : Type 0 -> Type 0` (`:27`), `(A : Type 0) * A`, `prod (Type 0, Type 0)` and `sum (Type 0, Type 0)` check. An explicit `Type 1` annotation has no type (`TYPE_UNIVERSE`). |
| F15 | Indexed family | DONE | `mu EqOutcome : (0 a : Outcome) -> (0 b : Outcome) -> Type 0` (`:174`); `mu Aggregation : (0 F : Constitution) -> Type 0` (`:182`). Mutant `log-match.lang`: a `match` on the opaque `AnchorLog` is `TYPE_MATCH`. |
