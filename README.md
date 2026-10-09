# lang-template

lang-template is a template for domain-specific languages. Each language
from this template has three parts:

1. **The former layer.** The type formers F1 to F15 and their laws:
   products, sums, options, lists, a monad, fold and unfold, filter,
   functions, dependent functions, Sigma, equality, universes and indexed
   families. `formers/FORMERS.md` specifies them. The specification does not
   depend on a host. A language does not add a type former.
2. **The domain.** One domain design. It gives the only core types and the
   only core operations of the language. Paste it into `design/DESIGN.md`.
3. **The host.** The host checks and runs the compiler, or checks the
   program itself. Each host kit in `hosts/` realizes the formers.

The template comes from two languages that share the former layer and no
code:

- ledger-lang at commit 997fa7b: a compiler in mechanism-lang. The host kit
  `hosts/mech/` comes from it.
- escrow-lang at commit 279214c: a restricted assay dialect. The host kit
  `hosts/assay/` comes from it.

## Choose a host

Choose the host by the target.

| Host | Target | Host executable | Kit |
|---|---|---|---|
| mech | One JSON document: the final state of each instance | `mech.exe` (mechanism-lang) | [`hosts/mech/README.md`](hosts/mech/README.md) |
| assay | An EVM contract and a kernel file | `assay` | [`hosts/assay/README.md`](hosts/assay/README.md) |
| tcc-json | One JSON document: the final state of each instance | `build/langc` (TinyCC) | [`hosts/tcc-json/README.md`](hosts/tcc-json/README.md) |
| tcc-evm-contract | A deployable EVM contract (slice K4; the ABI selectors now) | `build/langc` (TinyCC) | [`hosts/tcc-evm-contract/README.md`](hosts/tcc-evm-contract/README.md) |
| tcc-wasm | A Wasm module with one `i64` export for each entry | `build/langc` (TinyCC) | [`hosts/tcc-wasm/README.md`](hosts/tcc-wasm/README.md) |
| tcc-evm | EVM bytecode with one ABI `uint256` function for each entry | `build/langc` (TinyCC) | [`hosts/tcc-evm/README.md`](hosts/tcc-evm/README.md) |
| tcc-evm-dao | EVM bytecode for a self-constituting DAO, with the verdict table in the contract | `build/langc` (TinyCC) | [`hosts/tcc-evm-dao/README.md`](hosts/tcc-evm-dao/README.md) |
| tcc-evm-anchor | EVM bytecode for one governed log of hash and time pairs, with the outcome table in the contract | `build/langc` (TinyCC) | [`hosts/tcc-evm-anchor/README.md`](hosts/tcc-evm-anchor/README.md) |

The realization matrix in `formers/FORMERS.md` section 5 gives the status of
each former on each host. Read it before you choose. For example, assay
cannot realize `unfold` (F7), and mech does not have dependent functions
(F10) yet.

## Start a language

```
bin/new-lang.sh NAME HOST [DEST]
```

- `NAME` is the language name. It must match `^[a-z][a-z0-9-]*$`.
  The `tcc-json` host reserves `instances` for its JSON document key.
- `HOST` is `mech`, `assay`, `tcc-json`, `tcc-evm-contract`, `tcc-wasm`, `tcc-evm`, `tcc-evm-dao` or `tcc-evm-anchor`.
- `DEST` is the new directory. The default is `../NAME` beside the template
  root. The script refuses a `DEST` that exists.

The script copies the template files and the host kit into `DEST`. It
replaces `{{LANG}}` with `NAME` and `{{HOST}}` with `HOST`. It runs
`git init -b main`. It does not stage or commit. The comment at the top of
`bin/new-lang.sh` gives the path of each file in `DEST`. In short:

- The `*.template.md` files lose `.template`.
- The host kit files land at the `DEST` root.
- The host `FORMERS.md` becomes `formers/HOST.md`.
- The host `README.md` and `docs/` files go to `docs/host/`.
- The script refuses overlapping template and host paths in `DEST`,
  including two host files that map to the same destination.

## Workflow

1. Probe the host. Answer each question in `probe/CAPABILITY.md`. Run each
   step below `probe/guard.py`, which stops a command at a memory limit or a
   time limit.
2. Paste the domain design into `design/DESIGN.md`, verbatim.
3. Fill `SPEC.md`. The quoted line under each heading says what to write.
   Sections 5 and 6 hold only the types and operations of the design.
4. Replace the sample domain in `domain/` with the domain of the design.
   `docs/host/README.md` tells how for the host.
5. Run the gate of the host kit. Record the result in
   `docs/VALIDATION.md` and the state in `docs/STATUS.md`.

## Layout

| Path | Content |
|---|---|
| `formers/FORMERS.md` | The former specification, the refusal rules and the realization matrix |
| `SPEC.template.md` | The section skeleton of a language specification |
| `design/DESIGN.md` | The domain slot |
| `probe/CAPABILITY.template.md` | The host probe form and its questions |
| `probe/guard.py` | A memory and time limit for one command |
| `docs/STATUS.template.md`, `docs/VALIDATION.template.md` | The status and validation forms |
| `bin/new-lang.sh` | Makes a new language |
| `bin/doc-check.pl` | The check behind `make doc-check` |
| `hosts/mech/`, `hosts/assay/`, `hosts/tcc-json/`, `hosts/tcc-evm-contract/`, `hosts/tcc-wasm/`, `hosts/tcc-evm/`, `hosts/tcc-evm-dao/`, `hosts/tcc-evm-anchor/` | The host kits |

## Gates

- `make check` runs the documentation check, tool regression tests, and the
  gate of each host kit that is present. It also checks that
  `hosts/tcc-wasm` and `hosts/tcc-evm` have the same shared files. The probe
  guard tests need macOS, as does `probe/guard.py` itself.
- `make test` runs the tool regression tests with Python 3. Four of these
  tests make a language from `hosts/tcc-wasm`, `hosts/tcc-evm`,
  `hosts/tcc-evm-dao` and `hosts/tcc-evm-anchor` and run its gate, so they
  need the tools that the kit READMEs list.
- `make doc-check` fails on an em-dash or an en-dash in a Markdown file, and
  on a placeholder other than `{{LANG}}` and `{{HOST}}`.

## License

MIT (`LICENSE-MIT`) or Apache 2.0 (`LICENSE-APACHE`), at your option.
