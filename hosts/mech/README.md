# mechanism-lang host kit

This kit compiles a small typed language to JSON. The parser, checker,
evaluator and JSON writer run in a mechanism-lang Wasm GC reactor.
Node transfers bytes and provides the command line.

From this kit directory, or from a generated language directory:

```sh
make check test MECH_BIN=/absolute/path/to/mech.exe
bin/langc examples/inventory.lang
```

Use Node with Wasm GC support. Validation uses Node v23.10.0.
The default host command is `mech`. Set `MECH_BIN` if it is elsewhere.
`PIN` records the compiler origin and the host used for validation.
No network access or package installation is needed for these commands.
`make test` builds the reactor before running the suites.

Source files use the `.lang` extension. Definitions start with `def`.
The output has a domain name key and an `instances` array. Each instance
has a name, a normalized type and a JSON value. Type definitions, functions
and equality proofs are checked but are not emitted as instances.
The CLI writes JSON to stdout on success. It writes a byte position and a
diagnostic to stderr and returns a nonzero status on failure.

Replace `domain/schema.mech` with your design, then run `make plans` to
generate `domain/plans.mech` from it. `make check` fails when the two files
do not agree. Keep the interface described in `domain/README.md`.
The shipped domain has two
record types, `Item` and `Stock`, and an enum, `Measure`.
It is a sample to replace with your design. Update the domain tests and
examples with it. The source language cannot declare new core families.
The generic compiler and the Node bridge do not need domain-specific edits.

Products, sums, options, lists, pure/map/bind, fold, bounded unfold,
filter, non-dependent functions, Sigma, equality proofs and universes are
present. Sigma is only the type of a definition, and its body refers to the
binder only as a whole side of an Eq type.
Dependent functions, transport, congruence and generic indexed
families are planned. `formers/FORMERS.md` in the template specifies these
formers. This kit's `FORMERS.md`, copied to `formers/mech.md` in a
generated language, records its realization.

Nat literals range from 0 through 1,073,741,823. Source size is at most
65,536 bytes. Source and emitted text must be valid UTF-8.
Evaluation and JSON output have finite budgets and report an error when
the budget is exhausted. The measured limits for one
definition are 511 nested parentheses, 256 nested `Option` type formers,
127 nested `textByte` or `cons` terms, and 102 nested `attrsField` terms.
Each count includes the innermost form and uses only required parentheses.
The compiler tests check these boundaries.

The generic compiler and regression suites were extracted from ledger-lang
commit 997fa7b. Domain operations from that language are not part of this kit.
