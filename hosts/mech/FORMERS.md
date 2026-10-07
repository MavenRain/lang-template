# mechanism-lang realization

Paths below are relative to this host kit, or to the root of a generated
language. The generic source comes from ledger-lang 997fa7b.
The template's `formers/FORMERS.md` gives the host-neutral laws.

| ID | Former | Status and evidence |
|---|---|---|
| F1 | Product | DONE: `compiler/checker.mech:27`, `test/formers.test.mjs` |
| F2 | Coproduct | DONE: `compiler/checker.mech:1688`, `test/structures.test.mjs` |
| F3 | Option | DONE: `compiler/types.mech:97`, `test/constructors.test.mjs` |
| F4 | List | DONE: `compiler/types.mech:98`, `test/compiler.test.mjs` |
| F5 | Monad pure/map/bind | DONE over Option, List and Sum E: `compiler/checker.mech:1625`, `test/structures.test.mjs` |
| F6 | Algebra fold | DONE over Nat, List, Text, Values, Attrs and Value: `compiler/checker.mech:1717`, `test/algebra.test.mjs` |
| F7 | Algebra unfold | DONE into those carriers with a Nat step limit: `compiler/checker.mech:1771`, `test/algebra.test.mjs` |
| F8 | Filter | DONE over Option, List, Text, Values and Attrs: `compiler/checker.mech:1625`, `test/structures.test.mjs` |
| F9 | Non-dependent Pi | DONE: `compiler/program.mech:286`, `test/functions.test.mjs` |
| F10 | Dependent Pi | PLANNED: signatures cannot depend on a value parameter |
| F11 | Sigma | DONE with a restriction. B refers to the binder only as a whole Eq side. Sigma is only the type of a definition, like Eq. Type `compiler/runtime.mech:126`, parser `compiler/program.mech:150`, checker `compiler/checker.mech:27,1305`, instantiation `compiler/types.mech:175`, `test/sigma.test.mjs` |
| F12 | Eq refl/symm/trans | DONE: `compiler/checker.mech:126`, `test/equality.test.mjs` |
| F13 | Eq transport/cong | PLANNED: names reserved in `compiler/types.mech:199` |
| F14 | Universes | DONE for Type 0 and Type 1: `compiler/parser.mech:30`, `test/formers.test.mjs` |
| F15 | Generic indexed family | PLANNED: the origin's domain-specific Ref family was removed |

F11 changes the compiler build. `bin/build.mjs` builds `compiler/json.mech`
before `compiler/checker.mech`, because `pack` and `payload` print the
witness value. `typeNameAt` (`compiler/types.mech:38`) takes the binder
text and not a Nat depth, because mech has no recursion over Nat.
`parseBinder` (`compiler/program.mech:127`) is new. It parses `(x : A)` and
checks the name against the enclosing binders.

The sample domain contributes records and an enum. It adds no type former.
The original Ref, Query and WritePath forms are outside this kit.
