# mechanism-lang realization

Paths below are relative to this host kit, or to the root of a generated
language. The generic source comes from ledger-lang 997fa7b.
The template's `formers/FORMERS.md` gives the host-neutral laws.

| ID | Former | Status and evidence |
|---|---|---|
| F1 | Product | DONE: `compiler/checker.mech:20`, `test/formers.test.mjs` |
| F2 | Coproduct | DONE: `compiler/checker.mech:1620`, `test/structures.test.mjs` |
| F3 | Option | DONE: `compiler/types.mech:87`, `test/constructors.test.mjs` |
| F4 | List | DONE: `compiler/types.mech:88`, `test/compiler.test.mjs` |
| F5 | Monad pure/map/bind | DONE over Option, List and Sum E: `compiler/checker.mech:1557`, `test/structures.test.mjs` |
| F6 | Algebra fold | DONE over Nat, List, Text, Values, Attrs and Value: `compiler/checker.mech:1649`, `test/algebra.test.mjs` |
| F7 | Algebra unfold | DONE into those carriers with a Nat step limit: `compiler/checker.mech:1703`, `test/algebra.test.mjs` |
| F8 | Filter | DONE over Option, List, Text, Values and Attrs: `compiler/checker.mech:1557`, `test/structures.test.mjs` |
| F9 | Non-dependent Pi | DONE: `compiler/program.mech:189`, `test/functions.test.mjs` |
| F10 | Dependent Pi | PLANNED: signatures cannot depend on a value parameter |
| F11 | Sigma | PLANNED: names reserved in `compiler/types.mech:154` |
| F12 | Eq refl/symm/trans | DONE: `compiler/checker.mech:86`, `test/equality.test.mjs` |
| F13 | Eq transport/cong | PLANNED: names reserved in `compiler/types.mech:154` |
| F14 | Universes | DONE for Type 0 and Type 1: `compiler/parser.mech:30`, `test/formers.test.mjs` |
| F15 | Generic indexed family | PLANNED: the origin's domain-specific Ref family was removed |

The sample domain contributes records and an enum. It adds no type former.
The original Ref, Query and WritePath forms are outside this kit.
