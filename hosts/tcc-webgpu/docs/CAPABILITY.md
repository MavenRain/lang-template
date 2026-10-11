# TinyCC WebGPU host capability

This file holds host facts only. Put the facts of your domain in the
`probe/CAPABILITY.md` of your language. Paths are relative to the host
kit, or to the root of a generated language.

## Toolchain

- TinyCC 0.9.28rc (AArch64 Darwin) builds `build/langc` with
  `-std=c99 -Wall -Werror` (`Makefile`).
- `cc -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only` checks the same
  sources (`make check-clang`). Each switch on an enum names every case
  and has no default arm.
- `tcc -run gen/embed.c` turns `domain/domain.lang` into `build/domain.c`
  at build time. The domain is part of the executable.
- Node (validated with 23.10.0) runs the ES modules in `test/gate.sh`.
  The modules use BigInt and need no package.
- Deno runs the GPU tests in `make gpu-check` with
  `deno run --unstable-webgpu`. They need a GPU adapter, so `make check`
  does not run them. Under Node, a module with GPU tests throws the
  no-adapter error (`README.md`, section The JS module).

## Values

- `Nat` is an unsigned 64-bit integer. Literals larger than
  18446744073709551615 are refused by the lexer (`test/parse/nat-range.lang`).
- `natAdd` and `natMul` trap on overflow. `natSub` is truncated at 0.
  `natDiv` and `natMod` trap when the divisor is 0.
  A trap is a value: it stops a computation only when the computation
  needs it. `langc eval` prints `trap` (`test/eval/expect.txt`).
  `langc build` refuses an instance that holds a trap (`EVAL_OVERFLOW`).
- `Str` is a string of bytes. A literal is `"..."` with the escapes `\"`,
  `\\` and `\n`. Bytes from 0x80 are kept as they are. The lexer refuses
  an open string (`LEX_STR_OPEN`), another escape (`LEX_STR_ESCAPE`) and a
  raw newline or control byte (`LEX_STR_CONTROL`) (`test/parse/`). The
  operations are `strEq : Str -> Str -> Flag` and `showNat : Nat -> Str`
  only.
- `U32` is an unsigned 32-bit integer. A literal is a decimal number with
  the suffix `u`, for example `7u`. The lexer refuses a literal larger than
  4294967295 (`LEX_U32_RANGE`, `test/parse/u32-range.lang`). `u32Add`,
  `u32Sub` and `u32Mul` wrap modulo 2^32 and do not trap. `U32` and `Nat`
  are different types (`test/check/u32-from-nat.lang`,
  `test/check/nat-from-u32.lang`).
- `Tile R C A` holds R times C cells of type `A` in row-major order. `at`
  outside the tile gives a trap. A tile in an instance has at most 1048576
  cells (`EVAL_TILE_SIZE`). The checker refuses a bad cell type
  (`TILE_ELEM`, `test/check/tile-elem.lang`).
- Values carry no types. The JSON writer gets each type from the
  definition type and from the family declarations.
- In the JS module, `Nat` is a BigInt with the same trap rules, and a trap
  is a value there too. `Str` is a JS string. A `--js` build refuses a
  `Str` literal that is not valid UTF-8 (`JS_UTF8`). A JSON-only build
  refuses a string in its document that is not valid UTF-8 (`JSON_UTF8`).
- In the JS module, `U32` is a JS Number with the same wrap rules, and a
  tile is a frozen object with BigInt `rows` and `cols`.

## Limits

- The checker and the evaluator stop at a depth of 2000
  (`src/front/check.c:10`, `src/front/core.h:16`), and the parser at a
  nesting of 1000 (`src/front/parser.c:16`). These limits keep the C stack
  bounded. `test/gate.sh` checks a nesting of 1100 parentheses.
- The evaluator has a fuel of 20,000,000 steps for each instance
  (`src/front/core.h:17`). `langc build` starts the fuel again for each
  instance.
- All memory comes from one arena with a limit of 1 GiB (`src/main.c:11`).
  A run that reaches it stops with `OOM`.
- Source files are at most 1 MiB (`src/front/front.h:7`).
- The JS module has no fuel and no depth limit. Deep recursion in JS stops
  at the Node stack limit.
- A WGSL entry has at most 7 parameters (WebGPU gives a shader 8 storage
  buffers: 7 inputs and the result), 64 bound variables and 1048576 cells
  in each tile (`src/wgsl.c:12-19`). The workgroup size is 64
  (`src/wgsl.c:16`).

## Output

- `langc build` makes the full JSON document in the arena, then writes
  it. A refused build writes no partial document and no `-o` file.
- The writer walks list spines in a loop, so a long list does not use C
  stack. Other nesting counts against the JSON depth of 2000
  (`src/json.h:15`).
- `langc build --js` prepares the module text and the JSON document in the
  arena before it writes either file. A refused build writes no file.
  The module text is ASCII only, ends with one newline, and is the same
  for the same program.
- `langc build --wgsl` writes one WGSL module with one compute shader for
  each entry. The text is ASCII only and is the same for the same program.
  A program without entries gives only the header comment.
- WGSL has 32-bit indexes and u32 arithmetic, but the C evaluator uses
  64-bit Nat values. The writer refuses an entry that can index outside a
  tile (`BOUNDS`). It also refuses an entry with a Nat term that can reach
  2^32, or with a term that has no WGSL form (`WGSL_FORM`). The check is
  sound, but it is not complete. A refused build writes no file.
