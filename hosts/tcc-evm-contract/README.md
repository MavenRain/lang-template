# tcc-evm-contract

The TinyCC host kit of retainer-lang. It compiles the contract language to EVM bytecode.

## Origin

- The front end, `src/main.c`, `src/ir.h`, `src/target.h`, `gen/`, `domain/`, `examples/` and `test/gate.sh` are copies of `hosts/tcc-wasm`. The SHA-256 of each source file is in `lang-template-work/tcc-evm-contract-snapshot.txt`, with a list of the changes.
- `src/asm.c` and `src/asm.h` come from the escrowc assembler (escrow-lang `src/evm.c`). The labels are numbers, not a fixed enum.
- `src/keccak.c` and `src/keccak.h` are copies of the escrowc files. Only the names are changed.
- `src/evm.c` is the EVM target. It writes the ABI selectors and the code of `langc build` (`evm_build` in `src/evm.h`). `src/lower.c` gives it the storage words of `init` (slice K4a) and the IR of each entry (slice K4b) and each view (slice K4c). The logs and the calls come in slices K4c and K4d.

## Slices

| Slice | Content | Status |
|-------|---------|--------|
| K1 | Kit skeleton, assembler, keccak, ABI selectors, tool probe | Done |
| K2 | U256 and Addr core types | Done |
| K3a | Contract types: State, Env, Out, entries and views | Done |
| K3b | `langc run` and call scripts | Done |
| K3c | History rule: REFUSE_HISTORY_WRITE | Done |
| K4a0 | Map former (front end) | Done |
| K4a1 | Named events (front end) | Done |
| K4a | Build output and storage layout | Done |
| K4b | Entries, dispatch and call data decode | Done |
| K4c | EVM views and logs | In progress |
| K4d to K5 | See the retainer-lang brief | Planned |


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

The helpers are in `src/front/u256.c`. They use no `__int128` and no shift by 64 or more. `examples/words.lang` has the laws and the eval cases. The storage form of these types is in `## EVM output (slice K4)`. In slice K4b, the IR word is the 256-bit EVM word, and each IR scalar (Nat, Flag, U256, Addr) keeps the range of its type.

## Contracts (slice K3)

Slice K3a adds the contract types to the checker and the evaluator. Slice K3b adds `langc run` and call scripts. Slice K3c adds the history rule to `langc check`.

The contract prelude is in `src/front/contract.c`. The front end loads it after `domain/domain.lang` and before the program. Its names are core names, thus a program cannot declare them again.

```
family Env := makeEnv (now : Nat) (caller : Addr)
family Out := pay (payToken : Addr) (payTo : Addr) (payAmount : U256)
  | pull (pullToken : Addr) (pullFrom : Addr) (pullTo : Addr) (pullAmount : U256)
  | emit (emitTag : Nat) (emitFields : List U256)
```

- `Env` gives the time of the call (`now`) and the address of the caller (`caller`).
- `Out` is one effect of an entry. `pay` sends tokens to an address. `pull` moves tokens from an address. `emit` writes an event.
- `emit` stays in slice K4a1. A program can also declare named events (refer to `## Events (slice K4a1)`).
- Slice K4c writes a LOG only for a named event, thus `emit` has no EVM form. `langc build` refuses an entry that makes an `emit` (REFUSE_LOWER, `test/lower/emit.lang`). `langc check`, `langc eval` and `langc run` keep `emit`.

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

`langc check` and `langc eval` do not find the role of a definition. For them, `init` is a normal definition, but the history rule of `langc check` (slice K3c) does not check the body of `init` and refuses a reference to `init` from a definition with `State` in its type. `langc run` finds the roles and the start state (slice K3b).

`examples/contract.lang` has the state, `init`, the entries `deposit`, `withdraw` and `stamp`, the helper `checkpoint` and the view `balance`. `langc eval` prints a state in the constructor form, for example `makeState 0x00000000000000000000000000000000000000aa 0u 0u 0`. `langc build` writes the EVM code (refer to `## EVM output (slice K4)`). `langc abi` writes one line for each entry and view (in source order), then one line for each event: the selector, the signature and the kind, for example `0xb6b55f25 deposit(uint256) entry` (slice K4c, C-K4-16). An event line has the 32 bytes of topic 0. test/abi.expect holds the lines of the example programs; each selector and topic equals the `cast sig` or `cast sig-event` output. `langc abi PROG --json` writes the Solidity JSON ABI (Q-K4-3, slice K4c): one array with one item for each entry, view and event, in the same order. An entry is `nonpayable` with no outputs, a view is `view` with one output, and an event is not `anonymous` and has no indexed field. The inputs of an entry or a view have an empty name; the inputs of an event have the field names. test/abi.expect also holds the JSON of contract.lang and events.lang; `cast interface` reads each array.

The history rule (slice K3c) makes sure that a call cannot make a history field less than before. For a state with at least one history field, `langc check` checks the rule on the core term of each definition, after it resolves the names. It does not check the body of `init` or of a definition with the type `State` after normalization, for example `initial` in `examples/state-aliases.lang`. States without history fields do not need this rule.

- Each use of the state constructor has all of its arguments.
- The argument for a history field `F` is `F t` or `ADD (F t) e`. `ADD` is `u256Add` for a `U256` field and `natAdd` for a `Nat` field. `t` and `e` can be any terms of the correct type.
- A definition with `State` in its type cannot refer to `init` or to a definition with the type `State`, because a return of one of them can put a history field back to its start value. A definition without `State` in its type can refer to them, for example `savedEarned` in `examples/state-aliases.lang`.
- An open type variable is conservatively treated as possibly containing `State`. For example, `Sigma (A : Type 0) A` can package a state even though its written type does not name `State`, so it cannot hide a reference to `init` or another unchecked state definition.
- The rule is strict. It refuses a literal (`0u`), a subtraction (`u256Sub (earned s) 1u`), the operands in the other order (`u256Add 1u (earned s)`), the constructor without all of its arguments (`makeState` alone), and a history value that goes through a `fun` binder. A binder with the name of a field, for example `fun (earned : U256) => ...`, is a local and not the field. A view with `State` in its type that refers to `init` or to a definition with the type `State` is also refused.
- If a definition does not obey the rule, the checker gives `REFUSE_HISTORY_WRITE`.

Why the rule is sound: each state in an entry comes from the input state, or from a constructor that keeps or adds to each history field of a state. A definition without `State` in its type cannot give a state to an entry, because a program cannot declare a family that holds a state (rule R1). Thus each history field of the result is not less than the same field of the input. `u256Add` and `natAdd` trap on overflow, so a value cannot wrap. The test files are `test/check/history-*.lang`.

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
- The start state also gets the full fuel limit. A NUL byte in a script line is a `RUN_SCRIPT` error.

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
  event Stamped 3u 2u
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
  - `RUN_SCRIPT`: a line has fewer than 3 words, a bad `NOW` or `CALLER`, or a NUL byte. The message gives `SCRIPT:LINE`.
  - `EVAL_ENTRY`: `NAME` is not an entry or a view.
  - `EVAL_FUEL`, `EVAL_DEPTH` and `EVAL_PRINT` stop the run, as in `langc eval`.
- 2: a usage error, an `IO` error for the script, or `EVAL_ARGS` (a bad argument or an incorrect number of arguments), as in `langc eval`.

## Maps (slice K4a0)

`Map K V` is a finite map. K4a0 adds it to the front end only (`langc check`, `langc eval` and `langc run`). There is no EVM code for a Map yet.

- The key type K must be Nat, U256 or Addr. The value type V must be Nat, Flag, U256 or Addr. Other types give REFUSE_MAP.
- `mapEmpty` is the empty map. `mapGet m k` gives the value at the key k. `mapSet m k v` gives the map with the value v at the key k.
- A key with no value gives the zero value of V: 0, flagNo or the zero word of the sort (EVM storage reads 0 from a slot that is not set). `mapSet m k` with the zero value removes the key. There is no delete, fold, size or key list, because an EVM mapping cannot be iterated.
- The value is canonical: the keys are in order and no value is zero. Thus two maps with the same entries are equal by `refl`, in any order of the sets.
- `mapGet` infers its map argument, so `mapGet mapEmpty k` gives TYPE_INFER. A map argument that is not a Map gives TYPE_MISMATCH.
- Print form: `mapOf [(1, 2), (3, 4)]`, and `mapOf []` for the empty map. As an argument, the form is in parentheses.
- A Map can be a state field at the top level only. A Map in Option, Prod or List, and a history Map, give REFUSE_STATE.
- A Map has no ABI type. A definition with a Map argument or a Map result is a helper, not an entry or a view, so `langc run` gives EVAL_ENTRY for a call to it.
- examples/map.lang has the laws as `refl` definitions and a state with a Map field. test/run/map.script calls it. The run gate runs test/run/NAME.script on examples/NAME.lang when that file exists, else on examples/contract.lang.

## Events (slice K4a1)

An event is a named effect with fields. K4a1 adds it to the front end (`langc check`, `langc eval` and `langc run`). Slice K4c writes a LOG for it (C-K4-13, see `## EVM output`).

```
event Opened
event Paid (to : Addr) (amount : U256)
```

- Syntax: `event Name (f1 : T1) ... (fn : Tn)`. An event can have no fields. `event` is a reserved word.
- An event is a constructor of `Out`. `Paid a w` is a value of type `Out`, thus an entry can put it in its `List Out` result. An incorrect number of arguments gives TYPE_ARITY. An argument of an incorrect type gives TYPE_MISMATCH.
- Field types: Nat, Flag, U256 and Addr (the ABI word types). Another field type gives REFUSE_EVENT. More than 16 fields give REFUSE_EVENT. Two fields of one event with the same name give REFUSE_EVENT.
- Names: an event name is a core name. A second event with the same name, or a definition with the name of an event, gives REFUSE_NAME (rule R4, `X is an event`). A field name is local to its event. It is not a projection and not a core name, thus two events and a definition can use the same field name.
- Signature: `Name(t1,...,tn)`, with the ABI type name of each field, as for an entry (C-K4c-4): Nat is `uint64`, Flag is `bool`, U256 is `uint256` and Addr is `address`. Slice K4c computes it (C-K4-13): topic 0 of the LOG is the keccak256 of the signature. All fields go in the log data, one 32-byte word each, and no field is indexed (LOG1).
- Scope: the checker adds the events to `Out` when it checks the `Out` family of the prelude, before all program definitions. Thus a definition can use an event that the program declares after it. A field type cannot use an alias definition of the program, because that definition is not in scope yet. An `event` in `domain/domain.lang` also adds a constructor to `Out`.
- An event is not a state field: a state field of type `Out` gives REFUSE_STATE. `Out` has no ABI type, thus a definition with an `Out` argument or an `Out` result is a helper, not an entry or a view.
- Print form: `langc eval` prints an event in the constructor form, for example `Paid 0x00000000000000000000000000000000000000bb 7u`. `langc run` prints each event of a call on a line `  event Name a1 ... an` after the call line. A compound argument is in parentheses.

`test/run/events.script` calls `deposit` of `examples/events.lang` two times. The `langc run` output (`test/run/events.out`) is:

```
1 deposit ok
  event Opened
  event Paid 0x00000000000000000000000000000000000000bb 5u
2 deposit ok
  event Opened
  event Paid 0x00000000000000000000000000000000000000cc 2u
state makeState 7u
```

## EVM output (slice K4)

Slice K4a writes the creation code of a contract. Slice K4b writes the runtime: the dispatcher and one block for each entry. Slice K4c adds one block for each view. `src/lower.c` walks the state of `init` and makes a list of (slot, value) words. It also lowers the body of each entry and each view from Core to IR (`src/ir.h`). `evm_build` in `src/evm.c` makes the code from the words and the IR. `src/evm.h` has no front-end type.

- `langc build` evaluates `init` with the evaluator of `langc eval`. The creation code has PUSH value, PUSH slot, SSTORE for each word that is not zero. Then it copies the runtime and returns it.
- The runtime has the dispatcher, then one shared REVERT block with empty data, then the shared `Trap()` block, then one block for each entry and each view.
- A contract with no entry or view has a runtime of 37 bytes: the dispatcher head, the REVERT block and the `Trap()` block. Each call to it reverts with empty data.

Storage layout:

- Field i of the state is at slot i. A `history` field uses the rule of its type.
- Words: Nat is a u64 in the low bits. Flag is 0 or 1. U256 is the full word. Addr is in the low 160 bits.
- A `List T` field at slot s (T is a word type): the length is at slot s. Element j is at slot keccak256(s) + j.
- A `Map K V` field at slot s (K and V are word types): the value at key k is at slot keccak256(k . s). k and s are 32-byte big-endian words. A key that is not in the map reads as zero.

Dispatch (slice K4b):

- All entries are nonpayable. A call with a value (CALLVALUE not zero) reverts with empty data.
- Call data shorter than 4 bytes reverts with empty data.
- The selector is CALLDATALOAD(0) shifted right by 224 bits. The dispatcher compares it with the selector of each entry (DUP1, PUSH4, EQ, JUMPI). An unknown selector goes into the REVERT block and reverts with empty data.
- The selector of an entry is the first 4 bytes of the keccak256 of its signature `name(t1,...,tn)`. The ABI type names are: Nat is `uint64`, Flag is `bool`, U256 is `uint256` and Addr is `address`.
- Slice K4c puts the views in the dispatcher (C-K4-10). The selector of a view has the same form. A call to a view with a value also reverts.

Decode (slice K4b):

- The entry block removes the selector from the stack. Then it checks the size: call data shorter than 4 + 32 * N bytes (N arguments) reverts with empty data.
- Argument k is the word at byte 4 + 32 * k of the call data. The block puts it in memory at byte 32 * k.
- Dirty-bit rule: a Nat argument must be less than 2^64, a Flag argument must be 0 or 1, and an Addr argument must be less than 2^160. When a high bit is set, the call reverts with empty data. A U256 argument has no check.

Entry body (slice K4b):

- `now` is the TIMESTAMP of the block and `caller` is CALLER, the sender of the call.
- `none` reverts with empty data.
- A trap reverts with the 4 bytes of the `Trap()` selector, `0xae96083a`.
- `some (pair STATE OUT)` writes each changed word field and each Map write with SSTORE. Then the block stops (STOP).
- Before the stores, the block writes one LOG1 for each event of the OUT part, in the order of the list (C-K4-13). Topic 0 is the keccak256 of the event signature (see `## Events`). The data is the field words, 32 bytes each, in the order of the fields. An event with no fields has empty data. An event in a `flagIf` branch is logged only when the branch runs. A revert or a trap removes the logs.
- `src/lower.c` lowers the first-order word part of the body: Nat, Flag, U256 and Addr values, Option, Prod and `if`. Each op gives the result of the evaluator: Nat add and mul use the K2 u64 rules; U256 add and mul trap on overflow, sub stops at 0, and div rounds down and traps on 0.

View body (slice K4c):

- A view returns one word. The block computes the word with the rules of an entry body, puts it in memory at byte 0 and returns these 32 bytes (RETURN). The word has the ABI form of the result type: Nat is `uint64`, Flag is `bool`, U256 is `uint256` and Addr is `address`.
- A view writes no storage. A trap in a view reverts with the `Trap()` selector, as in an entry.

Output:

- One line of lowercase hex, with no `0x` and a newline at the end. It goes to stdout, or to OUT with `-o OUT`.
- `--runtime` writes only the runtime bytes. It does not apply the creation code limit (EIP-3860, 49152 bytes). The runtime limit (EIP-170, 24576 bytes) applies to the two output modes.
- The output goes into a temporary buffer first. A refused build writes nothing on stdout, keeps an existing OUT and makes no new OUT.
- Exit 0: the build is correct. Exit 1: `langc: REFUSE_LOWER: ...` or another refusal. Exit 2: `langc: IO_WRITE: -: stdout: ...` when a write fails.

REFUSE_LOWER refuses a state that has no storage form in slice K4a: an Option field, a Prod field, a List of a type that is not a word, a nested List, a Map with a key or a value that is not a word, an `init` with arguments, and a program with no state or no `init`. `langc check` refuses a Map with a List value first (REFUSE_MAP).

REFUSE_LOWER also refuses an entry body that slice K4b does not lower: a write of a List field (`test/lower/list-write.lang`), a `fold` at a word position (`test/lower/nat-fold.lang`), and each other residual that is not first-order word code. It also refuses an entry that makes an `emit` (`test/lower/emit.lang`), because `emit` has no EVM form: use a named event. The same rules apply to the body of a view. A def with the type of an entry or a view and an argument that is not a word is a helper: it is not in the dispatcher.

The other refusals of `langc build` (exit 1, no output):

- `langc: EVM_SIGNATURE: ...`: the signature of an entry is longer than 255 bytes.
- `langc: EVM_SELECTOR: ...`: two entries have the same 4-byte selector.
- `langc: EVM_SIZE: ...`: the creation code is longer than the EIP-3860 limit, the runtime is longer than the EIP-170 limit, or an assembler buffer or table is full. The assembler allows at most 1024 labels and 4096 jump references (`src/asm.h`); these table limits can refuse code below the EIP byte limits.

`test/build.sh` builds `examples/contract.lang`, `examples/map.lang` and `examples/storage.lang`, runs each code with `evm run --create --dump` and compares each storage slot with `langc eval PROG init`. `build/slottool HEX` computes the keccak256 slots. The script also runs the rows of `test/lower/expect.txt` and the output cases.

`test/dispatch.sh` builds `examples/residuals.lang` and does 20 calls with geth `evm run`: empty call data, a short selector, an unknown selector, a call value (and a control call with no value), `none`, a trap, a caller that is not the owner, short call data, dirty Nat bits, Nat 2^64 - 1, dirty Addr bits (bit 160), a dirty Flag word (the value 2), a control call with an Addr and a Flag argument, and the storage after three entries. The selectors and the Map slots come from the SHA3 op of `evm`, not from `langc`.

`test/evm.sh` is the chain test (the helpers are in `test/evmchain.sh`). It deploys `examples/contract.lang`, `examples/map.lang`, `examples/residuals.lang` and `examples/events.lang` with `evm run --create`. Then it does one `evm run --prestate --dump` step for each call of `test/run/basic.script`, `test/run/map.script`, `test/run/residuals.script` and `test/run/events.script` (23 steps). The block TIMESTAMP of a step is `NOW` (the prestate timestamp) and the sender is `CALLER`. After each step, the storage must equal the state that `langc run` prints for the calls up to that step. The result must agree with the result of the call: ok, revert (`none`) or trap. A view call must return the 32-byte word of the value that `langc run` prints for the view. The logs of each step (`evm run --debug`) must equal the events that `langc run` prints for the call: one LOG1 for each event, in order, with topic 0 and the data words. The selectors come from the SHA3 op of `evm`. The argument types come from the `def` line of the entry.

`build/buildtool` checks the 37-byte runtime of a contract with no entry, the EIP-170 runtime limit in the two output modes, the EVM_SELECTOR refusal (before any output) and IO_WRITE with exit 2 for a closed stdout pipe.

## Commands

- `make check`: the tcc build with `-Wall -Werror`, the clang syntax pass, `test/gate.sh`, `test/run.sh`, `test/asm.sh`, `test/build.sh`, `test/dispatch.sh` and `test/evm.sh`. `test/asm.sh`, `test/build.sh`, `test/dispatch.sh` and `test/evm.sh` need geth `evm` on PATH.
- `build/langc check FILE` and `build/langc eval FILE`: as in tcc-wasm.
- `build/langc run PROG SCRIPT`: does the calls in `SCRIPT` on the program `PROG`. Refer to `## Contracts (slice K3)`. `test/run.sh` runs each `test/run/NAME.script` on `examples/contract.lang` and compares the output with `test/run/NAME.out`. It also has 8 refusal rows and a usage row. `build/runtool` checks fresh fuel for initialization and call classification.
- `build/langc build FILE [-o OUT] [--runtime]` writes the creation code (with `--runtime`, the runtime code: the dispatcher and the entry blocks) as one line of lowercase hex. Refer to `## EVM output (slice K4)`.
- `build/asmtool`: the assembler self-test. `build/asmtool runtime|creation|abi` writes the test program or its ABI line.

The tool versions and the test path for K4 are in `docs/CAPABILITY.md`.
