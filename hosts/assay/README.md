# Assay host kit

This kit makes a language on the assay kernel dialect (`.asy`). It comes
from escrow-lang 279214c, with the escrow domain removed. The assay commit
that the kit uses is in `PIN`. The host facts are in the host
CAPABILITY.md (`docs/CAPABILITY.md` in this kit). The former status is in
`FORMERS.md` of this kit.

## Layout

| Path | Contents |
|---|---|
| `prelude/Kit.asy` | the kit with no domain: `Unit`, `EqNat` with transport, symm, trans and cong, `Le`, the `Option` kit and the `Sum` kit |
| `gen/carrier.sh` | writes the kit of one carrier: an Eq family, a list family or a bridge between two Eq families |
| `domain/Domain.asy` | the sample domain: types above the `-- @carriers` line, operations below it |
| `domain/carriers.txt` | the carriers of the domain, one for each line |
| `domain/header` | the literal parameters, one `NAME : Nat := N` for each line (optional) |
| `assemble.sh` | writes one program file in the order that the kernel needs |
| `refuse.sh` | refuses the forms that a program may not use |
| `run.sh`, `rss.cjs` | run the assay binary with a 4 GB RSS limit |
| `gate.sh` | the gate of the kit |
| `examples/program.asy` | the sample program |
| `examples/mutant.asy` | a sample program that the kernel must refuse |

## Plug-in interface

A domain is a directory with three files.

1. `Domain.asy` must hold one line `-- @carriers`. The lines above it are
   the domain types. The lines below it are the domain operations.
2. `carriers.txt` has one line for each carrier:
   - `eq NAME TYPE` writes the family `EqNAME` on `TYPE`, with `reflNAME`,
     `transportNAME`, `symmNAME`, `transNAME` and `congNAME`.
   - `list NAME ELEM` writes the family `NAME` of `ELEM`, with `nilNAME`,
     `consNAME`, `foldNAME`, `appendNAME`, `pureNAME`, `mapNAME`,
     `bindNAME` and `filterNAME`.
   - `bridge FROM FROMTYPE TO TOTYPE` writes `congFROMTO`. It takes a
     function `FROMTYPE -> TOTYPE` and a proof of `EqFROM x y` to a proof
     of `EqTO (f x) (f y)`.

   Each name must match `^[A-Z][A-Za-z0-9]+$`. A bridge line comes after
   the `eq` lines of its two families. `EqNat` is in the kit.
3. `header` (optional) has one `NAME : Nat := N` for each line. NAME must
   match `^[a-z][A-Za-z0-9]*$` and N must be a literal.

`assemble.sh` writes: the header, `prelude/Kit.asy`, the domain types, the
carrier kits, the domain operations and the program.

## Replace the domain

1. Write your types above `-- @carriers` in `domain/Domain.asy`. Use a
   `mu` family with one constructor for a record, and read it by `match`.
2. Put one line in `domain/carriers.txt` for each Eq family and each list
   family that you need.
3. Write your operations below `-- @carriers`.
4. Put your literal parameters in `domain/header`, or delete the file.
5. Replace `examples/program.asy` and `examples/mutant.asy`. Keep the
   `-- expect:` line in the mutant: the gate looks for that text in the
   refusal.
6. Run the gate.

## Run

```
bash gate.sh
bash assemble.sh examples/program.asy > /path/to/new/program.asy
bash refuse.sh examples/program.asy
bash run.sh LABEL check /path/to/new/program.asy
bash run.sh LABEL axioms /path/to/new/program.asy
```

`run.sh` uses the binary at `ASSAY_BIN` (default
`/Users/oobi/Documents/assay/_build/bin/assay`). It writes a warning when
the assay HEAD is not `PIN` and continues. The logs go to
`ASSAY_SCRATCH/logs` (default `$TMPDIR/lang-template-assay`). For `emit`,
give an output directory that does not exist.

## Known behavior

- `check` and `axioms` write nothing to stdout when they pass.
- For an `EqNat` mutant, the mismatch message shows the index as a
  literal. For an Eq family on a domain type, it shows the core term, for
  example `(In SMu Color [] (ACtor blue) [])`. Write the `-- expect:` line
  to agree with this.
- `refuse.sh` reads tokens outside `--` comments and outside double-quoted
  strings. It does not know block comments.
