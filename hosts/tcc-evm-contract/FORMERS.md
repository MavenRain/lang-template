# Formers on the tcc-evm-contract host

The front end of this kit is a copy of the tcc-wasm front end. `langc check`
and `langc eval` accept the formers F1 to F15 as that front end does.
`test/gate.sh` runs the forked examples on them.

The EVM output is PLANNED for each former. Slice K4 adds the lowering to EVM
bytecode. Until then, `langc build` stops with `langc: PLANNED`.

When a slice adds the lowering of a former, record its status and the test
that shows it in this file.
