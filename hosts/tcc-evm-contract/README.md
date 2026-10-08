# tcc-evm-contract

The TinyCC host kit of retainer-lang. It compiles the contract language to EVM bytecode.

## Origin

- The front end, `src/main.c`, `src/ir.h`, `src/target.h`, `gen/`, `domain/`, `examples/` and `test/gate.sh` are copies of `hosts/tcc-wasm`. The SHA-256 of each source file is in `lang-template-work/tcc-evm-contract-snapshot.txt`, with a list of the changes.
- `src/asm.c` and `src/asm.h` come from the escrowc assembler (escrow-lang `src/evm.c`). The labels are numbers, not a fixed enum.
- `src/keccak.c` and `src/keccak.h` are copies of the escrowc files. Only the names are changed.
- `src/evm.c` is the EVM target. It writes the ABI selectors. The bytecode output comes in slice K4.

## Slices

| Slice | Content | Status |
|-------|---------|--------|
| K1 | Kit skeleton, assembler, keccak, ABI selectors, tool probe | Done |
| K2 | U256 and Addr core types | Done |
| K3a | Contract types: State, Env, Out, entries and views | Done |
| K3b | `langc run` and call scripts | Done |
| K3c to K5 | See the retainer-lang brief | Planned |


## U256 and Addr (slice K2)

The core has two more types next to `Nat` and `Flag`:

- `U256` is a 256-bit unsigned number. The evaluator keeps it in 4 limbs of 64 bits.
- `Addr` is a 160-bit address. The only operation on two addresses is the equality test `addrEq`.

`Nat` stays a 64-bit number for time and counts.

Literal forms:

- `1000u` is a U256 in decimal. `0xffu` is a U256 in hex, with 1 to 64 hex digits. A value above 2^256 - 1 is a parse refusal (`LEX_U256_RANGE`).
- `0x` and exactly 40 hex digits, with no suffix, is an Addr. Another digit count is a parse refusal (`LEX_HEX`).
- The printer writes a U256 as decimal digits and `u`, and an Addr as `0x` and 40 lowercase hex digits. The parser reads each printed form again.

Prelude names:

| Name | Type | Result |
|------|------|--------|
| `u256Add` | `U256 -> U256 -> U256` | The sum. An overflow past 2^256 - 1 traps. |
| `u256Sub` | `U256 -> U256 -> U256` | The difference. The result stops at 0. |
| `u256Mul` | `U256 -> U256 -> U256` | The product. An overflow traps. |
| `u256Div` | `U256 -> U256 -> U256` | The quotient, rounded down. Division by 0 traps. |
| `u256Le` | `U256 -> U256 -> Flag` | `flagYes` when the first is less than or equal to the second. |
| `u256Eq` | `U256 -> U256 -> Flag` | `flagYes` when the two are equal. |
| `u256Min` | `U256 -> U256 -> U256` | The smaller of the two. |
| `toU256` | `Nat -> U256` | The same number as a U256. This is the only conversion. |
| `addrEq` | `Addr -> Addr -> Flag` | `flagYes` when the two addresses are equal. |

The helpers are in `src/front/u256.c`. They use no `__int128` and no shift by 64 or more. `examples/words.lang` has the laws and the eval cases. The EVM output for these types is planned for slice K4: `langc build` still stops with `langc: PLANNED: ...`.

## Contracts (slice K3)

Slice K3a adds the contract types to the checker and the evaluator. Slice K3b adds `langc run` and call scripts. Slice K3c adds the history rule. K3c is planned.

The contract prelude is in `src/front/contract.c`. The front end loads it after `domain/domain.lang` and before the program. Its names are core names, thus a program cannot declare them again.

```
family Env := makeEnv (now : Nat) (caller : Addr)
family Out := pay (payToken : Addr) (payTo : Addr) (payAmount : U256)
  | pull (pullToken : Addr) (pullFrom : Addr) (pullTo : Addr) (pullAmount : U256)
  | emit (emitTag : Nat) (emitFields : List U256)
```

- `Env` gives the time of the call (`now`) and the address of the caller (`caller`).
- `Out` is one effect of an entry. `pay` sends tokens to an address. `pull` moves tokens from an address. `emit` writes an event.
- The emit tag is an event index. Slice K4 maps it to a log topic.

The state form:

```
state State := makeState (owner : Addr) (held : U256) history (earned : U256) (cursor : Nat)
```

- `state` is a keyword. A program can declare one state only.
- The state type must have the name `State`, no parameters and one constructor.
- Each field type must be first order: `Nat`, `Flag`, `U256`, `Addr`, or `Option`, `Prod` or `List` of first-order types. A field cannot have the type `State`, a function type or a `Type`.
- Field types are checked after normalization, so type aliases are allowed, including inside containers and in history fields.
- The word `history` marks each field of the next `(...)` group as a history field. A history field must have the type `Nat` or `U256`.
- `history` is not a reserved word. It has this function only in the field list of a state.
- Each field name is a projection, as in a family. For example, `held s` is the `held` field of the state `s`.
- If a state does not obey these rules, the checker gives `REFUSE_STATE`. A `family` in a program is still `REFUSE_DATA`.

The type of a definition gives its role. There is no keyword for a role.

- An entry has the type `(env : Env) -> (s : State) -> (a1 : T1) -> ... -> Option (Prod State (List Out))`. Each `Ti` is `Nat`, `Flag`, `U256` or `Addr`. An entry can have zero arguments.
- An entry gives `none` to revert. It gives `some (pair s2 outs)` for the new state `s2` and the effects `outs`, in order.
- A view has the same parameters as an entry. Its result type is `Nat`, `Flag`, `U256` or `Addr`.
- All other definitions are helpers.
- `def init : State := TERM` gives the start state. The name `init` is fixed.

`langc check` and `langc eval` do not find the role of a definition. For them, `init` is a normal definition. `langc run` finds the roles and the start state (slice K3b).

`examples/contract.lang` has the state, `init`, the entries `deposit`, `withdraw` and `stamp`, the helper `checkpoint` and the view `balance`. `langc eval` prints a state in the constructor form, for example `makeState 0x00000000000000000000000000000000000000aa 0u 0u 0`. `langc abi` and `langc build` still stop with `langc: PLANNED: ...`.

`langc run PROG SCRIPT` checks the program `PROG`, gets the start state from `init`, and then does the calls in `SCRIPT` in sequence. Each line of a script is one call:

```
NOW CALLER NAME ARGS...
```

- `NOW` is a `Nat` literal. `CALLER` is an `Addr` literal: `0x` and 40 hex digits. The call gets `makeEnv NOW CALLER` as `env` and the current state as `s`.
- `NAME` is an entry or a view. A helper or an unknown name is not a call.
- Each argument has the literal form of its parameter type: `Nat` (for example `7`), `Flag` (`0` or `1`), `U256` (decimal digits or `0x` and 1 or more hex digits, then `u`, for example `5u` or `0xffu`) or `Addr`. Upper case and lower case hex digits are both correct.
- Spaces, tabs and carriage returns separate the words.
- If the first word of a line starts with `--`, the line is a comment. The run ignores comments and blank lines.
- Each call starts with the full fuel limit.

The output has one line for each call. The calls have the numbers 1, 2, 3 and so on. Comments and blank lines do not get a number. `test/run/basic.script` gives this output (`test/run/basic.out`):

```
1 deposit ok
  pull 0x00000000000000000000000000000000000000ee 0x00000000000000000000000000000000000000bb 0x00000000000000000000000000000000000000cc 5u
2 withdraw revert
3 deposit trap
4 withdraw ok
  pay 0x00000000000000000000000000000000000000ee 0x00000000000000000000000000000000000000aa 2u
5 balance = 3u
6 stamp ok
  emit 1 (cons 3u (cons 2u nil))
state makeState 0x00000000000000000000000000000000000000aa 3u 2u 105
```

- `N NAME ok`: the entry gave `some (pair s2 outs)`. The state changes to `s2`. Each `Out` in `outs` follows on a line of its own, in sequence, in the printer form, with two spaces before it.
- `N NAME revert`: the entry gave `none`. The state does not change.
- `N NAME trap`: the call has a trap, for example an overflow or a division by 0. A trap is a revert, as on the EVM: the state does not change and the run continues. The run does not write an `EVAL_OVERFLOW` line for a trap.
- `N NAME = VALUE`: the view gave `VALUE`. A view does not change the state.
- After the last call, the line `state` and the normal form of the final state.

The exit codes of `langc run`:

- 0: the run did all the calls in the script.
- 1: a check refusal or a script error. The run stops at the first bad line. The output lines of the calls before it stay on stdout.
  - `RUN_INIT`: the program has no state, it has no `def init : State`, or `init` has a trap.
  - `RUN_SCRIPT`: a line has fewer than 3 words, or a bad `NOW` or `CALLER`. The message gives `SCRIPT:LINE`.
  - `EVAL_ENTRY`: `NAME` is not an entry or a view.
  - `EVAL_FUEL`, `EVAL_DEPTH` and `EVAL_PRINT` stop the run, as in `langc eval`.
- 2: a usage error, an `IO` error for the script, or `EVAL_ARGS` (a bad argument or an incorrect number of arguments), as in `langc eval`.

## Commands

- `make check`: the tcc build with `-Wall -Werror`, the clang syntax pass, `test/gate.sh`, `test/run.sh` and `test/asm.sh`. `test/asm.sh` needs geth `evm` on PATH.
- `build/langc check FILE` and `build/langc eval FILE`: as in tcc-wasm.
- `build/langc run PROG SCRIPT`: does the calls in `SCRIPT` on the program `PROG`. Refer to `## Contracts (slice K3)`. `test/run.sh` runs each `test/run/NAME.script` on `examples/contract.lang` and compares the output with `test/run/NAME.out`. It also has 7 refusal rows and a usage row.
- `build/langc build FILE` stops with `langc: PLANNED: ...` until slice K4.
- `build/asmtool`: the assembler self-test. `build/asmtool runtime|creation|abi` writes the test program or its ABI line.

The tool versions and the test path for K4 are in `docs/CAPABILITY.md`.
