// langc build --wgsl: one compute shader for each entry of the program
// (an entry maps tiles and U32 values to a tile). A buffer holds the cells
// of a tile in row-major order as u32 values (a Flag cell is 0u or 1u).
// Invocation x of an entry writes the cell x of the result.
