# TinyCC WebGPU host kit

This kit compiles a small typed language to an ES module, to JSON and to
a WGSL module for WebGPU. A C front end (lexer, parser, checker and
evaluator), a JSON writer, a JS writer and a WGSL writer build with
TinyCC into one executable, `build/langc`. The ES module evaluates the
program in JS, and it can run the WGSL kernels on a GPU device. The JSON
document holds the final state of each instance in the program, the same
document as the tcc-json kit, and a kernels manifest when the program has
entries. The WGSL module holds one compute shader for each entry. There
is no IR and no lowering step.

From this kit directory, or from a generated language directory:

```sh
make check
build/langc check examples/inventory.lang
build/langc eval examples/inventory.lang stockTotal
build/langc build examples/inventory.lang -o inventory.json
build/langc build examples/laws.lang --js laws.js --selftest --json laws.json
node laws.js
build/langc build examples/kernels.lang --wgsl kernels.wgsl --js kernels.js --selftest
node kernels.js
```

The build uses TinyCC (`TCC`, default `tcc`). Validation uses TinyCC
0.9.28rc on AArch64 Darwin and Node 23.10.0. `make check-clang` also
compiles the sources with `cc -Wall -Wextra -Wswitch-enum -Werror
-fsyntax-only` (`CC`, default `cc`). The gate uses POSIX `sh`, `awk`,
`cmp`, `od`, ripgrep (`rg`) and Node (for `JSON.parse` and to run the
modules).
No network access or package installation is needed.
`make gpu-check` runs the GPU tests under Deno (`DENO`, default `deno`).
It needs a GPU adapter, so it is not part of `make check`.

## Commands

| Command | Result |
|---|---|
| `langc check PROG` | Checks the program. Prints `ok`. |
| `langc eval PROG NAME [ARGS...]` | Applies the definition `NAME` to the literal arguments and prints the normal form. A Nat overflow prints `trap`. |
| `langc build PROG [-o OUT]` | Checks the program, evaluates each instance and writes one JSON document to stdout, or to `OUT` |
| `langc build PROG [--js OUT.js [--selftest [--gpu]]] [--json OUT.json] [--wgsl OUT.wgsl]` | Checks the program and writes the ES module to `OUT.js`, the JSON document to `OUT.json` and the WGSL module to `OUT.wgsl`, for each flag that is given. `--selftest` adds the guest tests to the module. `--gpu` also adds the GPU tests. |

Exit 0 is success. Exit 1 is a refused program. Exit 2 is a usage or IO
error. A refusal writes one line to stderr: `langc: CODE: NAME: message`.

The second `build` form needs `--js`, `--json` or `--wgsl`. Each flag
can occur once. `--selftest` needs `--js`, and `--gpu` needs
`--selftest`. `-o` does not mix with these flags, and a path cannot
start with `-`. The JS, JSON and WGSL destinations must refer to
different files, including when paths or links alias the same file.
A bad combination writes
`langc: USAGE: ...` and the usage text, and exits 2.

`langc build` prepares each requested artifact fully in memory before
it writes. Thus a refused build writes nothing to stdout and makes
no file. When a write fails, `langc` removes the files that it wrote
before.

## The JSON document

```json
{"{{LANG}}":1,"instances":[{"name":"stockTotal","type":"Nat","value":70}]}
```

The key is the language name and the value is the format version, 1.
The generator reserves the names `instances` and `kernels` for this host to keep the three
document keys distinct. When the program has entries, the document also
has a `kernels` member after `instances` (see
[The kernels manifest](#the-kernels-manifest)).
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
| `U32` | A number from 0 to 4294967295, in decimal |
| `Tile R C A` | `{"rows": R, "cols": C, "data": [...]}`. `data` holds the R times C cells in row-major order, each at the type `A`. |
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
| `EVAL_OVERFLOW` | An instance holds a trap: a Nat operation overflows, `natDiv` or `natMod` divides by 0, or `at` is outside its tile |
| `EVAL_TILE_SIZE` | A tile in an instance has more than 1048576 cells |
| `JS_UTF8` | `--js` only: a `Str` literal is not valid UTF-8 |
| `BOUNDS` | An `at` in a WGSL entry can read outside its tile (see [The WGSL module](#the-wgsl-module)) |
| `WGSL_FORM` | A WGSL entry has no WGSL form (see [The WGSL module](#the-wgsl-module)) |

The build makes the WGSL module first, also when `--wgsl` is not given.
Thus `BOUNDS` and `WGSL_FORM` refuse each build of the program, for each
target. The JSON codes apply to `--js` too: the build also validates the
JSON document before writing a target. The check and evaluation codes are the same as
in `langc check` and `langc eval`. `test/parse`, `test/check` and `test/emit` hold a program
for each refusal.

`langc eval` also refuses an incomplete normal form with `EVAL_PRINT`
(a 65536-byte buffer and the same printer depth limit).

## The WGSL module

`langc build PROG --wgsl OUT.wgsl` writes one WGSL module. Its text is
ASCII only, and the same program gives the same bytes. The module starts
with a header comment. A program without entries gives only the header,
so each program builds with `--wgsl`.

An entry is a definition of the program (not of `domain/`) whose type
normalizes to a function type with 1 to 7 parameters and a tile result.
Each parameter is `Tile R C A` or `U32`, and the result is `Tile R C A`.
`R` and `C` are closed `Nat` values, and `A` is `U32` or `Flag`. The
writer ignores the other definitions. Thus a definition with more than 7
parameters and a result that is not a tile is not an entry, and the
writer does not refuse it.

For each entry, the module holds one `@compute @workgroup_size(64)`
function with the name of the entry:

- Parameter `P` (from 0) is the read-only storage buffer `NAME_inP`, an
  `array<u32>` at `@group(0) @binding(B)`. A `U32` parameter is a buffer
  with 1 cell. The result is the read_write storage buffer `NAME_out`.
  The binding numbers are unique in the module: the bindings of an entry
  come after the bindings of the entries before it. An unused parameter
  stays in the layout (`_ = arrayLength(&NAME_inP);`).
- One invocation writes one result cell. The cell index is `i * C + j`
  for row `i` and column `j` (row-major order).
- A `Flag` is a u32 0 or 1 in a buffer, and a bool in an expression.
- The writer applies the entry to its parameters and normalizes the body
  in a symbolic mode. In this mode, a `tabulate` with a closed shape does
  not unroll, and `sumTo` and `foldTo` stay loops. The writer fuses an
  `at` of a `tabulate` into the cell expression. Thus each entry is one
  pass with no intermediate buffers. The symbolic mode does not change
  `langc check`, `langc eval`, the JSON document or the JS values.
- `u32Add`, `u32Sub` and `u32Mul` wrap modulo 2^32 in WGSL, as in the C
  evaluator. A Nat term becomes a u32 term: `natAdd` is `+`, `natMul` is
  `*`, `natDiv` is `/`, `natMod` is `%`, `natSub` is
  `select(0u, a - b, a >= b)`, `natEq` is `==` and `natLe` is `<=`.
  `flagIf` is `select(e, t, c)`, and WGSL evaluates both arms. `sumTo` and
  `foldTo` are a `var` and a `for` loop before the expression that uses
  them.

The writer checks each `at` before it writes the entry. It gives each
Nat term an interval over the bound variables: row `i` is in [0, R-1],
column `j` is in [0, C-1], and a loop counter is in [0, n-1]. A fold
accumulator is in [0, 2^32-1], because each Nat in the shader is a
literal, a loop counter, or a `natAdd` or `natMul` result that passed the
check. The check includes both arms of each `flagIf`, because WGSL
`select` evaluates both arms. Thus the writer can refuse a program that
`langc check` and the lazy C evaluator accept. The check is sound, but it
is not complete.

Each build refuses an entry with these codes:

| Code | Cause |
|---|---|
| `BOUNDS` | The index interval of an `at` can leave the tile. This applies to an `at` on a parameter and to an `at` on a fused `tabulate`. |
| `WGSL_FORM` | The entry has no WGSL form. The message gives the cause. |

The causes of `WGSL_FORM`:

- An `at` index, or the result of a `natAdd` or `natMul`, can reach 2^32.
  WGSL indexes and u32 arithmetic have 32 bits, but the C evaluator uses
  64-bit Nat values.
- A `natDiv` or `natMod` divisor is not a closed Nat above 0. In WGSL,
  `x / 0u` is `x`, but in C it is a trap.
- A tile is not in the first argument of `at`, a tile is not a
  `tabulate` or a parameter, or a `tabulate` has an open shape.
- The body holds a function, a type, a `Str`, a trap, a value of a
  family, a list, a `Sigma` or an `Eq` value, or a Nat above 2^32-1.
- The entry has more than 7 parameters (WebGPU gives a shader 8 storage
  buffers), more than 64 bound variables, or a tile with more than
  1048576 cells.
- The entry name is not a WGSL identifier (ASCII letters, digits and
  `_`), or it is a WGSL keyword or reserved word.
- The entry name is the name of a binding of another entry (`NAME_inP`
  or `NAME_out`). This applies in both orders. The writer does not
  rename.

`test/emit/wgsl-*.lang` hold refused programs, and `test/emit/wgsl.txt`
gives their codes. `test/wgsl` holds the golden modules.

### The kernels manifest

When the program has 1 or more entries, the JSON document has a
`kernels` member after `instances`. A program without entries has no
`kernels` member, so its document does not change. The member holds one
object for each entry. This is the object for an entry `f` with a 2 by 3
`U32` tile parameter, a `U32` parameter and a 2 by 3 `U32` tile result:

```json
{"name":"f","params":[{"name":"f_in0","binding":0,"rows":2,"cols":3,"elem":"U32"},{"name":"f_in1","binding":1,"elem":"U32"}],"result":{"name":"f_out","binding":2,"rows":2,"cols":3,"elem":"U32"},"workgroup":64,"dispatch":1}
```

`binding` is the WGSL binding number. `elem` is `"U32"` or `"Flag"`. A
`U32` parameter has no `rows` and no `cols`. `workgroup` is the
workgroup size. `dispatch` is the workgroup count: R times C, divided by
64 and rounded up. `src/wgsl.c` writes the manifest text one time. The
JSON writer and the ES module use the same text, so the two documents
cannot differ.

## The JS module

`langc build PROG --js OUT.js` writes one ES module. Its text is ASCII
only and ends with one newline, and the same program gives the same
bytes. The module holds a small runtime, the family tables, one lazy
definition for each program and domain definition, and these exports:

| Export | Content |
|---|---|
| `document` | The JSON document as a frozen JS object: `{kind: 'document', lang, instances: [{name, type, value}]}`, evaluated in JS. When the program has entries, it also holds `kernels`, the manifest. |
| `encode(v)` | The JSON text of a value. `encode(document)` is the same bytes as `--json OUT.json`. |
| `entries` | One JS function for each definition that `langc eval` runs as an entry (domain definitions too) |
| `wgsl` | The WGSL module text, the same bytes as `--wgsl OUT.wgsl` |
| `kernels` | A frozen object with one async function for each WGSL entry. It is empty when the program has no WGSL entries. |

The values in JS:

- `Nat` is a BigInt, an exact unsigned 64-bit value. `natAdd` and `natMul`
  give a trap on overflow, and `natSub` is truncated at 0, as in the C
  evaluator. A trap is a value (`TRAP`) that stops a computation only
  when the computation needs it.
- `U32` is a JS Number from 0 to 2^32-1. `u32Add`, `u32Sub` and `u32Mul`
  wrap modulo 2^32 (`>>> 0` and `Math.imul`), as in the C evaluator.
  They do not give a trap.
- `natDiv` and `natMod` are BigInt `/` and `%`. A divisor of 0 gives a
  trap, as in the C evaluator.
- `Tile R C A` is a frozen `{kind: 'tile', rows, cols, data}`. `rows` and
  `cols` are BigInts. `data` is a frozen array of the R times C cells in
  row-major order. `at` outside the tile gives a trap. `tabulate` throws
  `RangeError('EVAL_TILE_SIZE: ...')` for more than 1048576 cells, as the
  C evaluator refuses with `EVAL_TILE_SIZE`.
- `Flag` is a boolean and `Str` is a JS string.
- A constructor value is a frozen `{kind: 'ctor', ctor, params, fields}`.
  A list is a chain of cells. Types are `null` and a proof is `REFL`.

`entries.NAME(...args)` takes a BigInt for each `Nat` parameter and a
boolean for each `Flag` parameter. `entries.NAME.params` holds one letter
for each parameter (`N` or `F`), and `entries.NAME.result` gives the
result kind. A wrong argument count throws a `TypeError`. A trap throws
`RangeError('EVAL_OVERFLOW: NAME: ...')`, and `encode` throws the same
error for a trap. `encode` throws an `Error` for a type or a function.

`kernels.NAME(device, ...inputs)` runs the WGSL entry `NAME` on a
`GPUDevice`. It gives a Promise of a frozen tile, with the same shape as
a JS tile value. A tile input is a JS tile with the shape of the
parameter, and a `U32` input is a Number from 0 to 2^32-1. A `Flag` cell
is a boolean in JS and a u32 0 or 1 in a buffer. Before it uses the
device, the function checks the input count, then each input (a tile or
a U32, the rows and the columns, and the cell kind), then the device. A
failed check rejects the Promise with a `TypeError`. The function
compiles the WGSL module one time for each device, makes one storage
buffer for each input, dispatches `dispatch` workgroups and reads the
result back.

With `--selftest`, top-level code in the module runs the guest tests.
Each `Eq`-typed definition of the program is a test: `langc check` proves
it, and the module evaluates its two sides in JS and compares them. A
test whose carrier holds a function or a type has no JS equality; the
module skips it with a comment line. `node OUT.js` exits 0 and prints
nothing when each test passes. On a mismatch it throws
`Error('selftest: NAME: the two sides differ')` and exits 1. Without
`--selftest` the module has no top-level code to run.

When the program has WGSL entries, the selftest also checks each kernel
with no GPU. It calls the kernel with a wrong input count, a wrong first
input, a tile of the wrong cell kind (only when the first parameter is a
tile) and a `null` device. Each call must reject with the expected
`TypeError`, before any use of the device.

With `--gpu` (it needs `--selftest`), the module also holds GPU tests.
A GPU test is a test whose carrier is a tile and whose left side, as
written, applies a definition to 1 to 7 closed inputs. The build stores
the C value of the left side. The module runs the GPU tests whose
definition is a WGSL entry:

1. The C value must equal the JS value of the left side. Else the
   module throws `Error('selftest: NAME: the C value and the JS value differ')`.
2. The module asks `navigator.gpu` for an adapter. When there is no
   adapter (for example under node), it throws
   `Error('selftest: N GPU tests and no GPU adapter (run: deno run --unstable-webgpu)')`.
   It does not skip the GPU tests.
3. The module runs each kernel on the device and compares the GPU tile
   with the C value. On a mismatch it throws
   `Error('selftest: NAME: the GPU tile of ENTRY differs from the C value')`.

A module without GPU tests runs as without `--gpu`. `make gpu-check`
builds each example with `--js build/gpu/NAME.js --selftest --gpu` and
runs it with `deno run --unstable-webgpu`. It stops at the first
failure. The gate (`make check`) builds each example with `--gpu` and
runs it under node: `examples/kernels.lang` must throw the no-adapter
error, and the other examples must exit 0. Thus the gate checks the C
values against the JS values, but only `make gpu-check` runs the
kernels on a GPU.

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
| WGSL entry parameters | 7 | `src/wgsl.c:12` |
| WGSL bound variables, for each entry | 64 | `src/wgsl.c:14` |
| WGSL workgroup size | 64 | `src/wgsl.c:16` |
| WGSL entry tile | 1048576 cells | `src/wgsl.c:19` |

## Origin

This kit is a fork of the tcc-js kit in this repository at commit
`c9b3c52` (`git archive c9b3c52:hosts/tcc-js`, tree
`0d75e48beb1478310471baf54c856acfe0c6da06`). It adds the `U32` and
`Tile R C A` primitives, `natDiv` and `natMod`, the WGSL writer, the
kernels manifest, the flags `--wgsl` and `--gpu`, and `make gpu-check`.

The table gives the SHA-256 of each tcc-js file at the fork that this
kit changes (paths relative to the kit). To see a change, compare the
file with `git show c9b3c52:hosts/tcc-js/PATH`. Each other file of the
kit is the same as in tcc-js at the fork. The kit removes no file.

| Path | SHA-256 in tcc-js at the fork | Change |
|---|---|---|
| `src/front/ast.h` | `8fa3972281ecffac1aca2cbffcffe6e3a885280d7121bb43f45aff0dadda34ec` | The `U32` literal term |
| `src/front/lexer.c` | `4fe2a52759cb275974240a8ed4f3b3fc2bf8e54a9f007586fe367c7cf4a98cb8` | `U32` literals (`LEX_U32_RANGE`) |
| `src/front/lexer.h` | `67d8d19e45fd2f5f1bedfa30692a2947a859724856cfd522c56725cd3fc84902` | The `U32` literal token |
| `src/front/parser.c` | `63f8d3dbf7f84cc6a7b0faec38c93846f89860e80408d187896b37565fb74fae` | `U32` literals |
| `src/front/core.h` | `f17c2689cd8daf9e05c2899d49d8fea44a28916ce0db2f6cfa1945fe5c419a9b` | `U32` and tile values |
| `src/front/check.c` | `548cafade085cd33bad3808c8270c102ae176a0b4f93f9d27f3348bbce68c8e0` | The `U32` and `Tile R C A` types |
| `src/front/eval.c` | `a33334d684ca1b9fbeadd921eac6c8eace80353a035adb12201faee3814af7c3` | `U32` and tile values |
| `src/json.c` | `6ce8b276d3449c96212be5d518b05d7fe6d266a16146315fb8b08888b32d6d2a` | `U32` and tile values; the `kernels` member |
| `src/json.h` | `76cce5876b5dc794f45889607e00cd683452310f9c58c5237da7493d679f4f2a` | Declarations for the changes above |
| `src/js.c` | `f762410850dbad531cff2d441b2a2503e17ce3a2fcdbefc5a5cbf64e8f7b754e` | `U32` and tile values; the GPU tests |
| `src/js.h` | `da0a0a47d6f3b7b5735547073cb1eb4ff8f3c04f054ff6aeeb3c35df018a9825` | Declarations for the changes above |
| `src/main.c` | `4fe66d19462979260e293076d5f6a1e5ee309cf37e7a6a5ce1c5d39c6c94a88b` | The flags `--wgsl` and `--gpu` |
| `Makefile` | `71e8285fabc055d6adb26f4de8d488a6fc679f0c6d05b1c7bec8a265e4bdfde1` | `TARGET` adds `src/wgsl.c`; the target `gpu-check` |
| `test/gate.sh` | `b9d68441ff19c907bde4195fd5df8efb0b4c7bef4c952da572ceb79075038978` | The `U32`, tile, WGSL and GPU checks |
| `test/regressions.js` | `7c0a4f14bdfe24f7ae9a26a32abe1639dac480e29c8f3f148825abe5e6e0f12c` | Line 62: the build line adds `src/wgsl.c`. No case is added. |
| `test/parse/expect.txt` | `ab7b4da2844cd7e629682454a4d9542bd137cb91f80a44c536c7307d1fb38de1` | `LEX_U32_RANGE` |
| `test/check/expect.txt` | `f0da952b9b551e9cf49e58f70c8bfdb29075fc93c05004c71352d8b8f1863fdc` | The `U32` and tile refusals |
| `test/eval/expect.txt` | `4e0df53f4a924670221a6ad75c909c487791d023c62d5ae0d4d7f75478e87b6b` | The `U32`, tile, `natDiv` and `natMod` evaluations |
| `README.md` | `535e67576fdeb6545406f520273c278ab005536082f7329e7155da24889157f5` | The `U32`, tile and WGSL docs |
| `FORMERS.md` | `a599d6b51f5d08482b4157de04f18202d0dd020a336a4f53673d45a201004096` | The `U32`, tile and WGSL docs |
| `docs/CAPABILITY.md` | `f8b48823eda1fadbe64995b9385d62d6d57fab1ba3ed13398f81c40255dec7cc` | The `U32`, tile and WGSL facts |
| `domain/README.md` | `d6aa1d6dddc0d47be662cc674f0f7bafe51d3d307c896aa727a52544f9766ef3` | WGSL entries |

New files: `src/wgsl.c` and `src/wgsl.h` (the WGSL writer);
`examples/u32.lang`, `examples/tiles.lang` and `examples/kernels.lang`,
with their goldens in `test/json/`; the golden modules
`test/wgsl/header.wgsl` and `test/wgsl/kernels.wgsl`;
`test/parse/u32-range.lang`; the seven refused programs
`test/check/u32-*.lang`, `test/check/nat-from-u32.lang` and
`test/check/tile-*.lang`; and the refused WGSL entries
`test/emit/wgsl-*.lang`, with their codes in `test/emit/wgsl.txt`.

### The tcc-js fork of tcc-json

The tcc-js kit is a fork of the tcc-json kit in this repository at commit
`e4464ab` (`git archive e4464ab:hosts/tcc-json`, tree
`25a78e56233656d000ac80dbe8121a9b6556e35b`). Its slice K1 adds the `Str`
primitive (string literals, `strEq` and `showNat`) to the front end, the
JSON writer and the printers. Its slice K2 adds the JS writer and the `--js`,
`--json` and `--selftest` flags. Its slice K3 adds the JS docs.

The table gives the SHA-256 of each tcc-json file at that fork that the
tcc-js kit changes (paths relative to the kit). To see a change, compare the
file with `git show e4464ab:hosts/tcc-json/PATH`. Each other file of the
tcc-js kit is the same as in tcc-json at that fork.

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

New files in tcc-js: `src/js.c` and `src/js.h` (the JS writer), `examples/text.lang`,
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

`FORMERS.md`, copied to `formers/tcc-webgpu.md` in a generated language,
gives the status of each former. `docs/CAPABILITY.md` gives the host
facts.
