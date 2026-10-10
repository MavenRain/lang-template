# TinyCC JS host kit

This kit compiles a small typed language to an ES module and to JSON. A C
front end (lexer, parser, checker and evaluator), a JSON writer and a JS
writer build with TinyCC into one executable, `build/langc`. The ES module
evaluates the program in JS. The JSON document holds the final state of
each instance in the program, the same document as the tcc-json kit.
There is no IR and no lowering step.

From this kit directory, or from a generated language directory:

```sh
make check
build/langc check examples/inventory.lang
build/langc eval examples/inventory.lang stockTotal
build/langc build examples/inventory.lang -o inventory.json
build/langc build examples/laws.lang --js laws.js --selftest --json laws.json
node laws.js
```

The build uses TinyCC (`TCC`, default `tcc`). Validation uses TinyCC
0.9.28rc on AArch64 Darwin and Node 23.10.0. `make check-clang` also
compiles the sources with `cc -Wall -Wextra -Wswitch-enum -Werror
-fsyntax-only` (`CC`, default `cc`). The gate uses POSIX `sh`, `awk`,
`cmp`, `od`, ripgrep (`rg`) and Node (for `JSON.parse` and to run the
modules).
No network access or package installation is needed.

## Commands

| Command | Result |
|---|---|
| `langc check PROG` | Checks the program. Prints `ok`. |
| `langc eval PROG NAME [ARGS...]` | Applies the definition `NAME` to the literal arguments and prints the normal form. A Nat overflow prints `trap`. |
| `langc build PROG [-o OUT]` | Checks the program, evaluates each instance and writes one JSON document to stdout, or to `OUT` |
| `langc build PROG [--js OUT.js [--selftest]] [--json OUT.json]` | Checks the program and writes the ES module to `OUT.js`, the JSON document to `OUT.json`, or both. `--selftest` adds the guest tests to the module. |

Exit 0 is success. Exit 1 is a refused program. Exit 2 is a usage or IO
error. A refusal writes one line to stderr: `langc: CODE: NAME: message`.

The second `build` form needs `--js` or `--json`. Each flag can occur
once. `--selftest` needs `--js`. `-o` does not mix with these flags, and
a path cannot start with `-`. The JS and JSON destinations must refer to
different files, including when paths or links alias the same file.
A bad combination writes
`langc: USAGE: ...` and the usage text, and exits 2.

`langc build` prepares each requested artifact fully in memory before
it writes. Thus a refused build writes nothing to stdout and makes
no file. When the second write fails, `langc` removes the first file.

## The JSON document

```json
{"{{LANG}}":1,"instances":[{"name":"stockTotal","type":"Nat","value":70}]}
```

The key is the language name and the value is the format version, 1.
The generator reserves the name `instances` for this host to keep the two
document keys distinct.
`instances` holds one object for each instance, in source order. An
instance is a definition of the program (not of `domain/`) whose type is
data. These definitions are checked, but they are not instances:

- functions (a Pi type, dependent or not);
- types and families (a universe type);
- equality proofs (an `Eq` type);
- the definitions in `domain/domain.lang`.

`type` is the normal form of the definition type in source syntax, for
example `Sigma (n : Nat) (Eq Nat n 1200)`. Binder names are freshened when
needed to preserve scope. The complete text must fit a 4096-byte buffer;
the build refuses a type that exceeds this size or the printer depth of
200, rather than emitting an abbreviated type.
`value` is the evaluated value. Values carry no types, so the writer walks
the type and the value together (`src/json.c`). The fuel of the evaluator
starts again for each instance.

| Type | JSON value |
|---|---|
| `Nat` | A number: the full unsigned 64-bit value in decimal |
| `Flag` | `true` or `false` |
| `Str` | A string. `"`, `\` and each byte below 0x20 are escaped (`\u00XX`). Other bytes are copied. |
| `Unit` | `{}` |
| `Prod A B` | `{"first": a, "second": b}` |
| `Sum A B` | `{"inl": a}` or `{"inr": b}` |
| `Option A` | `null` or the value. When `A` is an `Option` or an `Eq` type, `some v` is `{"some": v}`. |
| `List A` | An array |
| `Sigma (x : A) B` | `{"witness": w, "payload": p}`. The payload type is `B` at the witness. |
| `Eq A x y` | `null` |
| A family whose constructors have no fields | The constructor name, as a string |
| A family with one constructor | An object of the fields |
| Any other family | `{"tag": "name", ...}` with the fields of that constructor |

An indexed family uses the same rules. Its index is in `type` (for
example `Lot 3`), not in `value`.

`langc build` refuses a program with these codes:

| Code | Cause |
|---|---|
| `JSON_VALUE` | An instance holds a function, a type or a stuck term |
| `JSON_DEPTH` | An instance nests deeper than 2000 levels. A list spine does not count. |
| `JSON_TYPE` | The complete instance type exceeds the printer's size or depth limit |
| `JSON_UTF8` | A string in the JSON document is not valid UTF-8 |
| `EVAL_OVERFLOW` | A Nat operation in an instance overflows |
| `JS_UTF8` | `--js` only: a `Str` literal is not valid UTF-8 |

The JSON codes apply to `--js` too: the build also validates the JSON
document before writing either target. The check and evaluation codes are the same as
in `langc check` and `langc eval`. `test/parse`, `test/check` and `test/emit` hold a program
for each refusal.

`langc eval` also refuses an incomplete normal form with `EVAL_PRINT`
(a 65536-byte buffer and the same printer depth limit).

## The JS module

`langc build PROG --js OUT.js` writes one ES module. Its text is ASCII
only and ends with one newline, and the same program gives the same
bytes. The module holds a small runtime, the family tables, one lazy
definition for each program and domain definition, and these exports:

| Export | Content |
|---|---|
| `document` | The JSON document as a frozen JS object: `{kind: 'document', lang, instances: [{name, type, value}]}`, evaluated in JS |
| `encode(v)` | The JSON text of a value. `encode(document)` is the same bytes as `--json OUT.json`. |
| `entries` | One JS function for each definition that `langc eval` runs as an entry (domain definitions too) |

The values in JS:

- `Nat` is a BigInt, an exact unsigned 64-bit value. `natAdd` and `natMul`
  give a trap on overflow, and `natSub` is truncated at 0, as in the C
  evaluator. A trap is a value (`TRAP`) that stops a computation only
  when the computation needs it.
- `Flag` is a boolean and `Str` is a JS string.
- A constructor value is a frozen `{kind: 'ctor', ctor, params, fields}`.
  A list is a chain of cells. Types are `null` and a proof is `REFL`.

`entries.NAME(...args)` takes a BigInt for each `Nat` parameter and a
boolean for each `Flag` parameter. `entries.NAME.params` holds one letter
for each parameter (`N` or `F`), and `entries.NAME.result` gives the
result kind. A wrong argument count throws a `TypeError`. A trap throws
`RangeError('EVAL_OVERFLOW: NAME: ...')`, and `encode` throws the same
error for a trap. `encode` throws an `Error` for a type or a function.

With `--selftest`, top-level code in the module runs the guest tests.
Each `Eq`-typed definition of the program is a test: `langc check` proves
it, and the module evaluates its two sides in JS and compares them. A
test whose carrier holds a function or a type has no JS equality; the
module skips it with a comment line. `node OUT.js` exits 0 and prints
nothing when each test passes. On a mismatch it throws
`Error('selftest: NAME: the two sides differ')` and exits 1. Without
`--selftest` the module has no top-level code to run.

## The domain

`domain/domain.lang` is the sample domain. `gen/embed.c` embeds it in the
executable at build time. `domain/README.md` tells how to replace it.

## Limits

| Limit | Value | Source |
|---|---|---|
| Source size | 1 MiB | `src/front/front.h:7` |
| Arena | 1 GiB | `src/main.c:11` |
| Parser nesting | 1000 | `src/front/parser.c:16` |
| Checker depth | 2000 | `src/front/check.c:10` |
| Evaluator depth | 2000 | `src/front/core.h:16` |
| Evaluator fuel, for each instance | 20,000,000 steps | `src/front/core.h:17` |
| JSON nesting | 2000 | `src/json.h:15` |
| `type` text | 4096 bytes | `src/json.c:7` |
| Type and normal-form printer depth | 200 | `src/front/eval.c` |
| JS evaluation | No fuel and no depth limit: deep recursion stops at the Node stack limit | `src/js.c` |

## Origin

This kit is a fork of the tcc-json kit in this repository at commit
`e4464ab` (`git archive e4464ab:hosts/tcc-json`, tree
`25a78e56233656d000ac80dbe8121a9b6556e35b`). Slice K1 adds the `Str`
primitive (string literals, `strEq` and `showNat`) to the front end, the
JSON writer and the printers. Slice K2 adds the JS writer and the `--js`,
`--json` and `--selftest` flags. Slice K3 adds the JS docs.

The table gives the SHA-256 of each tcc-json file at the fork that this
kit changes (paths relative to the kit). To see a change, compare the
file with `git show e4464ab:hosts/tcc-json/PATH`. Each other file of the
kit is the same as in tcc-json at the fork.

| Path | SHA-256 in tcc-json at the fork | Change |
|---|---|---|
| `src/front/ast.h` | `5dbe74e54c8207da17a57810624bf049d729f272c73a0765b9fd504fb3f18720` | `Str` |
| `src/front/lexer.c` | `6812190d933fa5dfb1ad60c9d40a46aeb460aab5342feed6c4697fe31a2425bc` | String literals |
| `src/front/lexer.h` | `85046ffc1f76878b8dc0102db08458a59fef914d7951462c9e7bf590cb5db314` | String literals |
| `src/front/parser.c` | `6220dcc67e0fc4782d60cf9516bae979d2d4eb3c5eb3e047d92b85806b0bb02d` | String literals |
| `src/front/eval.c` | `72b39d6fe883d7ad6131af4fee0c296748d02bda30e0cadc43b420b2c94c2403` | `Str` values and printing |
| `src/front/core.h` | `6f503c4c090f1013c309b51a8c24cc7ab5f90e5e69f51708c198ce4bd73b4efb` | `Str`; the field `DefInfo.type_core` for the selftest |
| `src/front/check.c` | `18b2cd415be58d8a7a73ac9658adb653eb3da484b3972a3407483bbaa8b66f3f` | `Str`; sets `DefInfo.type_core` |
| `src/json.c` | `6e31c503087ece4b89b73e5d4733a9f0309aa98fdf0356629d873497fda986e4` | `Str` values; exports for the JS writer |
| `src/json.h` | `92f3e40b05e013059e2dee0b00500803c9bdabbd3e135087b62cdefafc8152d8` | Declares the exports |
| `src/main.c` | `df8389a3d1eeca7c68cdca43274f5e6958f5dae2e58fef0c09618430a60d777d` | The flags `--js`, `--json` and `--selftest` |
| `Makefile` | `e6caf8a4d5fafaf83fa3107901394b56aa8c06f4c2d05e3680fa2fde53046d7a` | `TARGET` adds `src/js.c` |
| `test/gate.sh` | `a846d1640df7e0f7349e27417ad3b9c4532d9535bcc5d104e80b45c7c24fc7da` | The JS checks |
| `test/regressions.js` | `35bbb1e8155b0af2cfc36581c9d13e77cfcbf49765e6f4633974939b9946c096` | Line 62: the build line adds `src/js.c`. No case is added. |
| `test/parse/expect.txt` | `5c47be37ce86005f25a99004a9f78684a970359d3f76ca6d52476accd4c47845` | The string refusals |
| `test/parse/string.lang` | `3dff9479124b6d85a79ea4e750ed4e46623c81435e36f106d40eb20a470740f0` | An open string (`LEX_STR_OPEN`) |
| `test/check/expect.txt` | `4f905324675591892a65778a910a7967b73dd285207d3a70b0450c12724676cf` | The `Str` type refusals |
| `test/eval/expect.txt` | `d562f350b98fbb3a2f4561e4ed0005b45714160849226fab5e5d48efe14636d3` | The `Str` evaluations |
| `README.md` | `6dc7d4bc638cec4292159286ce73081efa2eb5ef55b7766a8f9ac47d92b0c01f` | The JS docs |
| `FORMERS.md` | `4a211766939b12ad8abba27a2aed0aafbc5b8ae3d529765045b477dcca596fd6` | The JS docs |
| `docs/CAPABILITY.md` | `50ba6416e78fe9ca91d1c205fec679310892f1e3843466fc9ba10aee0387cf68` | The JS docs |
| `domain/README.md` | `2b550583867a9a8986a6ad40d64bcaec9e61ba90837b61e58590342ff5b4ad66` | The JS docs |

New files: `src/js.c` and `src/js.h` (the JS writer), `examples/text.lang`,
`test/json/text.json`, `test/parse/string-control.lang`,
`test/parse/string-escape.lang`, `test/parse/string-newline.lang`,
`test/check/show-nat-str.lang`, `test/check/str-eq-nat.lang` and
`test/check/str-not-nat.lang`.

`src/front/core.h` and `src/front/check.c` have the same paths in
tcc-json. The tcc-json kit does not have `DefInfo.type_core`.

### The tcc-json fork of tcc-wasm

The tcc-json kit is a fork of the tcc-wasm kit front end. It does not have the
Wasm writer (`wasm.c`), the IR (`ir.h`) or the target interface
(`target.h`). It adds `src/json.c` and `src/json.h`, and `src/main.c` has
the verbs `check`, `eval` and `build`. The front end also fixes dependent
result inference, Product and Sigma eta, Sum bind error-type preservation,
domain name checking, the F14 universe rules, and complete,
scope-preserving value printing. The snapshot below records the original
fork, before these fixes. To find the changes after the fork,
compare the hashes with `shasum -a 256 src/front/*` in the tcc-wasm kit.

SHA-256 of the tcc-wasm kit sources at the fork (paths relative to that
kit):

| Path | SHA-256 |
|---|---|
| `src/front/ast.h` | `5dbe74e54c8207da17a57810624bf049d729f272c73a0765b9fd504fb3f18720` |
| `src/front/base.c` | `d994f71d11a352896212d97ed6edb5de062dc37ad6f31bf927487c4d8e675d5b` |
| `src/front/base.h` | `d36daae15e9a79ae298eadf5f1f9c3687e077c7305e63fad6cc5b477b58cebca` |
| `src/front/check.c` | `526a8207c66f3276c18767c62de05026866e7e8adc30e2c5383a2975c0a57114` |
| `src/front/check.h` | `5d781eb3f52a7a17db10d375c0fb7c5b578bf0527dd358cfe49ebb779d008d06` |
| `src/front/core.h` | `d04aa9412bb0f25d470bd3b91e3a073284b19e89aa90083e18d63a6d468fd9dc` |
| `src/front/eval.c` | `5a044b8935f77614a2321883afc8df7d2b1d745e198b8f861538dc1a61773c55` |
| `src/front/front.c` | `9b7d8890d0854fe989780431521189fabf0a79e624359733f5a90822bbf08c27` |
| `src/front/front.h` | `7eb0b8de605ad7336eac1bffc9b65587d7113b2e3849a95a9e6adf75fd12989a` |
| `src/front/lexer.c` | `6812190d933fa5dfb1ad60c9d40a46aeb460aab5342feed6c4697fe31a2425bc` |
| `src/front/lexer.h` | `85046ffc1f76878b8dc0102db08458a59fef914d7951462c9e7bf590cb5db314` |
| `src/front/parser.c` | `6220dcc67e0fc4782d60cf9516bae979d2d4eb3c5eb3e047d92b85806b0bb02d` |
| `src/front/parser.h` | `e9caa8e0dbcfc716b572fa754daa3023ee1a55fb16ff77fb0235c97a49adaa1c` |
| `src/ir.h` | `13021f895667552ec1d527267258e8a3437cadde74744a348e1219dff6d773b9` |
| `src/main.c` | `2294beb677fb4c208811937bbf48f1570911de6e52a6068874e90b3489b211ba` |
| `src/target.h` | `077dff69391f69dbd4623858553dee7b5547a8c1edc0c75660e0aede9b15e429` |
| `src/wasm.c` | `4f69fba5ea1f335e95c7f030d970c4e89da84ee13c57d95879137cf96d8235bd` |

`FORMERS.md`, copied to `formers/tcc-js.md` in a generated language,
gives the status of each former. `docs/CAPABILITY.md` gives the host
facts.
