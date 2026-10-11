# Capability probe (slice K1)

Date: 2026-10-07. Machine: macOS, AArch64.

## Tools

- tcc 0.9.28rc 2026-09-04 mob@0fb54300
- geth `evm` 1.14.12-stable
- anvil 0.3.0 (5a8bd89)

## Results

1. `evm run --code HEX` runs the code and writes `0x<output>`. `test/asm.sh` uses it. The runtime of the test program returns the word 42. The creation code returns the runtime. Both pass.
2. `evm transition` (t8n) and `evm statetest` are in geth 1.14.12. K1 did not run a fixture through them.
3. anvil 0.3.0 is installed. It needs a local port and a process that runs for the full test. K1 did not start it.
4. The escrowc precedent (escrow-lang `settlement.py`) chains the state with `evm --verbosity 0 run --prestate FILE --gas GAS --input DATA --value VALUE --json --dump`. Each call reads the state that the previous call wrote.

## Recommendation for K4

- Use the escrowc path: chains of `evm run --prestate ... --dump`. It needs no port and no daemon, and escrowc uses it now.
- Use `evm statetest` only if K4 needs a transaction context that `run` does not give (a nonce, a deploy transaction, a gas price).
- Do not use anvil in the gate. It needs a port and a long-lived process.

## K4 result (slice K4d)

- K4 uses the escrowc path. `test/evm.sh` runs 6 chains of `evm run --prestate ... --dump` (43 steps). `test/diff.sh` runs the CALL failure table (10 rows) and 5 call scripts on the mock ERC-20 at the token address. The gate uses no t8n, no statetest and no anvil.
- Each pay and each pull is one CALL to the token (C-K4d-6). Pay is `transfer(payTo, payAmount)`. Pull is `transferFrom(pullFrom, pullTo, pullAmount)`. The contract is the sender.
- Each CALL forwards all gas (`GAS`) and 0 wei (C-K4d-7). There is no gas cap.
- The CALL must succeed. Its return data must either be 32 bytes equal to the word 1, or be empty with code at the token (`EXTCODESIZE`). If either check fails, the step does a REVERT with empty data (C-K4-14).

## Gas of each chain step (C-K4-19)

Measured on 2026-10-10 at LT 382e224 with geth `evm` 1.14.12: each step of `test/evm.sh` and `test/diff.sh` again with `evm run --statdump`. The value is the gas of the execution. `evm run` does not add the intrinsic gas of a transaction. The list does not include the deploy steps. The gate does not check the gas, and there is no cap.

| Chain of `test/evm.sh` | Program | Gas of each step |
| --- | --- | --- |
| `test/run/basic.script` | `test/contract-nocall.lang` | 22267, 2276, 2269, 27471, 2275, 27786 |
| `test/run/map.script` | `examples/map.lang` | 44473, 27373, 2334, 2334, 10273, 2334 |
| `test/run/residuals.script` | `examples/residuals.lang` | 6817, 2321, 149, 109, 44460, 26881, 5320, 9617, 315 |
| `test/run/events.script` | `examples/events.lang` | 24355, 7255 |
| `test/run/lists.script` | `examples/lists.lang` | 2399, 2342, 2378, 44415, 4638, 71653, 9158, 9435, 27315, 11418, 11778, 16381, 16466, 11418, 2409 |
| `test/same-state.script` | `test/same-state.lang` | 3542, 23597, 3542, 6497, 3542 |

`test/diff.sh` runs `examples/contract.lang` with the mock:

- Part 2, `test/run/basic.script` (mode 0): 73471, 2276, 2318, 80738, 2275, 27786. Step 1 (deposit 5u) does one pull CALL. Step 4 (withdraw 2u) does one pay CALL. Steps 2 and 3 revert before the CALL.
- Part 1, deposit 30u (one pull) with mode 0, 1, 2, 3 and no code: 73471, 27441, 73459, 27249, 25184. Withdraw 30u (one pay), same order: 80738, 34708, 80726, 34516, 32461.
- The other 4 scripts of part 2 use the same gas as in `test/evm.sh`.

## Open

- A t8n or statetest run with a fixture (two transactions on one pre-state): not done. K4 did not need it: the `run --prestate` path is sufficient for the chain test and the differential test.
