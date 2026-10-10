# TinyCC JS host capability

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

## Values

- `Nat` is an unsigned 64-bit integer. Literals larger than
  18446744073709551615 are refused by the lexer (`test/parse/nat-range.lang`).
- `natAdd` and `natMul` trap on overflow. `natSub` is truncated at 0.
  A trap is a value: it stops a computation only when the computation
  needs it. `langc eval` prints `trap` (`test/eval/expect.txt`).
  `langc build` refuses an instance that holds a trap (`EVAL_OVERFLOW`).
- `Str` is a string of bytes. A literal is `"..."` with the escapes `\"`,
  `\\` and `\n`. Bytes from 0x80 are kept as they are. The lexer refuses
  an open string (`LEX_STR_OPEN`), another escape (`LEX_STR_ESCAPE`) and a
  raw newline or control byte (`LEX_STR_CONTROL`) (`test/parse/`). The
  operations are `strEq : Str -> Str -> Flag` and `showNat : Nat -> Str`
  only.
- Values carry no types. The JSON writer gets each type from the
  definition type and from the family declarations.
- In the JS module, `Nat` is a BigInt with the same trap rules, and a trap
  is a value there too. `Str` is a JS string. A `--js` build refuses a
  `Str` literal that is not valid UTF-8 (`JS_UTF8`). A JSON-only build
  refuses a string in its document that is not valid UTF-8 (`JSON_UTF8`).

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
