# Type formers on the assay host

This file gives the status of each former of `formers/FORMERS.md` in this
host kit. The status words are DONE, PARTIAL, PLANNED and HOST-LIMIT. A
path is relative to the kit root. In a generated language, the kit root
is the language root. `CAP` is `probe/CAPABILITY.md` and `SPEC` is
`SPEC.md` in escrow-lang 279214c. The host CAPABILITY.md gives each fact.
"Gate" is a step of `gate.sh`. The gate checks `examples/program.asy`
with an empty `axioms` list.

| ID | Former | Status | Where | Evidence |
|---|---|---|---|---|
| F1 | Product | DONE | built-in `prod` and `tuple`; `Unit` at `prelude/Kit.asy:14-16`; the record `Item` at `domain/Domain.asy:17-26` | `examples/program.asy:4-7,20` |
| F2 | Coproduct | DONE | `Sum` at `prelude/Kit.asy:80-95`; the enum `Color` at `domain/Domain.asy:10-13` | `examples/program.asy:24-27` |
| F3 | Option | DONE | `prelude/Kit.asy:51-78` | `domain/Domain.asy:31-37` |
| F4 | List | PARTIAL | one generated family for each element type: `gen/carrier.sh:63-101`; the sample `Items` at `domain/carriers.txt:8` | P2, CAP:141-147; gate step 3 |
| F5 | Monad | DONE for each carrier | `Option` at `prelude/Kit.asy:56,65`; `Sum E` at `prelude/Kit.asy:82,91`; each list at `gen/carrier.sh:81,87` | `examples/program.asy:17` |
| F6 | Algebra fold | PARTIAL | one structural `def rec` for each list family: `gen/carrier.sh:71-75`; no fold over `Nat` | CAP:7-28; `domain/Domain.asy:39-40`, `examples/program.asy:10` |
| F7 | Algebra unfold | HOST-LIMIT | none | CAP:251, SPEC:276-279 |
| F8 | Filterable filter | DONE over `Option` and each list | `prelude/Kit.asy:71-78`, `gen/carrier.sh:91-100`; a test is `A -> Option (prod ())` | `domain/Domain.asy:42`, `examples/program.asy:11` |
| F9 | Pi, not dependent | DONE | built-in | each definition |
| F10 | Pi, dependent | DONE | the motive `(0 P : Nat -> Type 0)` at `prelude/Kit.asy:22-25`; `Le` at `prelude/Kit.asy:44-45` | `examples/program.asy:31` |
| F11 | Sigma | PARTIAL | built, never projected: `Le` at `prelude/Kit.asy:44-45` | P5, CAP:232-244; `examples/program.asy:31` |
| F12 | Eq refl, symm, trans | PARTIAL | one family for each index type in `Type 0`: `EqNat` at `prelude/Kit.asy:19-34`; generated at `gen/carrier.sh:38-53` | P2, CAP:141-155; `examples/program.asy:10-21`; gate step 5 |
| F13 | Eq transport, cong | PARTIAL | `prelude/Kit.asy:22-25,36-39`; generated at `gen/carrier.sh:41-44,55-58`; a bridge between two families at `gen/carrier.sh:103-112` | `examples/program.asy:28` |
| F14 | Universes | PARTIAL | families in `Type 0`; a type function `Type 0 -> Type 0` at `prelude/Kit.asy:51`; no type index in `Type 0` | CAP:148-155, SPEC:58 |
| F15 | Indexed family | DONE for a fixed index type | `EqNat` at `prelude/Kit.asy:19-20`; no parameter | P2, CAP:141-147 |

When a status changes, update this file and the assay column of
`formers/FORMERS.md` in the same change.
