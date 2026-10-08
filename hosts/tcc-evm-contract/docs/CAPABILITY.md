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

## Open

- A t8n or statetest run with a fixture (two transactions on one pre-state). Do it at the start of K4 if the `run --prestate` path is not sufficient.
