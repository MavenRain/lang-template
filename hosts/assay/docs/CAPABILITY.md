# Assay host capability

These facts apply to the assay kernel at PIN (`092fa7ab`). This file holds
host facts only. Put the facts of your domain in the `probe/CAPABILITY.md`
of your language.

Each fact cites escrow-lang 279214c. `CAP` is `probe/CAPABILITY.md` in
that commit and `SPEC` is `SPEC.md` in that commit. To read a citation:
`git -C <escrow-lang> show 279214c:probe/CAPABILITY.md`.

## Kernel

- A `mu` family can have indices. The kernel has Pi, Sigma, the built-in
  `sum` and `prod`, universes, and `Nat` with the built-ins `natAdd`,
  `natSub`, `natMul`, `natEq` and `natLt` (CAP:7-28).
- `Nat` has no eliminator. Thus no fold over `Nat` is possible (CAP:7-28).
- The kernel has no built-in Eq, List, Option or Bool. `prelude/Kit.asy`
  and `gen/carrier.sh` write them (CAP:7-28).
- Large elimination is only for a subsingleton in `Prop`. `nu` is deferred
  (CAP:7-28).

## Backend

- The runtime value is the 256-bit word only. The runtime operations are
  done, store, load, add, sub, le and abort. `le` is the only runtime
  branch (CAP:32-34).
- A runtime word cannot become a `Nat` (CAP:35).
- Closed records, tags, projections and cases reduce at compile time
  (CAP:36-37).
- The backend refuses closures, indirect calls, unsaturated calls, delay
  and force. It has no heap, no loop and no runtime recursion (CAP:38-40).
- Budgets: 100,000 specialization steps, transaction depth 128, 1,024
  temporary words and 24,576 runtime bytes (CAP:41-42).

## Contracts

- The kernel accepts an axiom witness. Thus the dialect must refuse
  `axiom` itself. `refuse.sh` does this, and the gate checks that
  `axioms` is empty (CAP:57-59).
- A surface contract body cannot hold a core term and has no two-way
  branch (P8, CAP:340-355). This kit refuses the contract forms.

## Modules and CLI

- There is no import form. A program is one file. `assemble.sh` writes
  that file (CAP:61-75).
- The launcher writes its compile cache in the assay tree unless
  `NODE_COMPILE_CACHE` is set. `run.sh` sets it under a scratch directory
  (CAP:71-72).
- `emit` refuses an output directory that exists. Give a new directory
  (CAP:133).
- `emit` checks each top-level definition (P6, CAP:283-287).

## Probe results

- P2: a constructor of a family with a parameter cannot appear in a term
  (CAP:141-147). Thus `gen/carrier.sh` writes one Eq family for each index
  type and one list family for each element type.
- P2: the kernel refuses a type index in `Type 0` (CAP:148-155). An Eq
  family indexes values, not types.
- P2: syntax of `match ... as ... in ... return ... with` and of the
  erased binder in a pattern (CAP:161-166).
- P3: `--print` and `--erased` do not show normal forms. A wrong refl
  candidate gives a mismatch message that shows the normal form
  (CAP:168-180). `examples/mutant.asy` uses this.
- P4: a fold over a list of N items has no step limit. N = 100 takes
  0.41 s and 121 MB, N = 1000 takes 23.82 s and 165 MB, and N = 4000
  takes 121.86 s and 393 MB (CAP:182-195). Keep test lists short.
- P5: the kernel refuses Sigma eta and has no Sigma pattern (CAP:232-244).
  Thus a record is a `mu` family with one constructor, read by `match`.
- A `mu` index of function type checks (CAP:245-248).
- `tuple ()` is the unit value (CAP:249-250).
- There is no unfold: the kernel has no structural measure and no `Nat`
  fuel (CAP:251, SPEC:276-279).
- `natAdd` reduces only on literals. Thus a proof of `Le` takes literal
  values (P6, CAP:289-291).
