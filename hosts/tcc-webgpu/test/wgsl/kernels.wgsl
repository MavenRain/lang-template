// langc build --wgsl: one compute shader for each entry of the program
// (an entry maps tiles and U32 values to a tile). A buffer holds the cells
// of a tile in row-major order as u32 values (a Flag cell is 0u or 1u).
// Invocation x of an entry writes the cell x of the result.

// copyE: Tile 2 3 U32 -> Tile 2 3 U32
@group(0) @binding(0) var<storage, read> copyE_in0: array<u32>;
@group(0) @binding(1) var<storage, read_write> copyE_out: array<u32>;

@compute @workgroup_size(64)
fn copyE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 6u) {
    return;
  }
  let i = idx / 3u;
  let j = idx % 3u;
  copyE_out[idx] = copyE_in0[i * 3u + j];
}

// addE: Tile 2 3 U32 -> Tile 2 3 U32 -> Tile 2 3 U32
@group(0) @binding(0) var<storage, read> addE_in0: array<u32>;
@group(0) @binding(1) var<storage, read> addE_in1: array<u32>;
@group(0) @binding(2) var<storage, read_write> addE_out: array<u32>;

@compute @workgroup_size(64)
fn addE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 6u) {
    return;
  }
  let i = idx / 3u;
  let j = idx % 3u;
  addE_out[idx] = (addE_in0[i * 3u + j] + addE_in1[i * 3u + j]);
}

// gemmE: Tile 2 3 U32 -> Tile 3 2 U32 -> Tile 2 2 U32
@group(0) @binding(0) var<storage, read> gemmE_in0: array<u32>;
@group(0) @binding(1) var<storage, read> gemmE_in1: array<u32>;
@group(0) @binding(2) var<storage, read_write> gemmE_out: array<u32>;

@compute @workgroup_size(64)
fn gemmE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 4u) {
    return;
  }
  let i = idx / 2u;
  let j = idx % 2u;
  var l0 = 0u;
  for (var l1 = 0u; l1 < 3u; l1 = l1 + 1u) {
    l0 = l0 + (gemmE_in0[i * 3u + l1] * gemmE_in1[l1 * 2u + j]);
  }
  gemmE_out[idx] = l0;
}

// trE: Tile 2 3 U32 -> Tile 3 2 U32
@group(0) @binding(0) var<storage, read> trE_in0: array<u32>;
@group(0) @binding(1) var<storage, read_write> trE_out: array<u32>;

@compute @workgroup_size(64)
fn trE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 6u) {
    return;
  }
  let i = idx / 2u;
  let j = idx % 2u;
  trE_out[idx] = trE_in0[j * 3u + i];
}

// rowSumE: Tile 2 3 U32 -> Tile 2 1 U32
@group(0) @binding(0) var<storage, read> rowSumE_in0: array<u32>;
@group(0) @binding(1) var<storage, read_write> rowSumE_out: array<u32>;

@compute @workgroup_size(64)
fn rowSumE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 2u) {
    return;
  }
  let i = idx / 1u;
  let j = idx % 1u;
  var l0 = 0u;
  for (var l1 = 0u; l1 < 3u; l1 = l1 + 1u) {
    l0 = l0 + rowSumE_in0[i * 3u + l1];
  }
  rowSumE_out[idx] = l0;
}

// rowProdE: Tile 2 3 U32 -> Tile 2 1 U32
@group(0) @binding(0) var<storage, read> rowProdE_in0: array<u32>;
@group(0) @binding(1) var<storage, read_write> rowProdE_out: array<u32>;

@compute @workgroup_size(64)
fn rowProdE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 2u) {
    return;
  }
  let i = idx / 1u;
  let j = idx % 1u;
  var l0 = 1u;
  for (var l1 = 0u; l1 < 3u; l1 = l1 + 1u) {
    l0 = (l0 * rowProdE_in0[i * 3u + l1]);
  }
  rowProdE_out[idx] = l0;
}

// scaleE: U32 -> Tile 2 3 U32 -> Tile 2 3 U32
@group(0) @binding(0) var<storage, read> scaleE_in0: array<u32>;
@group(0) @binding(1) var<storage, read> scaleE_in1: array<u32>;
@group(0) @binding(2) var<storage, read_write> scaleE_out: array<u32>;

@compute @workgroup_size(64)
fn scaleE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 6u) {
    return;
  }
  let i = idx / 3u;
  let j = idx % 3u;
  scaleE_out[idx] = (scaleE_in0[0] * scaleE_in1[i * 3u + j]);
}

// maskE: Tile 2 3 Flag -> Tile 2 3 U32 -> Tile 2 3 U32
@group(0) @binding(0) var<storage, read> maskE_in0: array<u32>;
@group(0) @binding(1) var<storage, read> maskE_in1: array<u32>;
@group(0) @binding(2) var<storage, read_write> maskE_out: array<u32>;

@compute @workgroup_size(64)
fn maskE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 6u) {
    return;
  }
  let i = idx / 3u;
  let j = idx % 3u;
  maskE_out[idx] = select(0u, maskE_in1[i * 3u + j], (maskE_in0[i * 3u + j] != 0u));
}

// upperE: Tile 2 3 Flag -> Tile 2 3 Flag
@group(0) @binding(0) var<storage, read> upperE_in0: array<u32>;
@group(0) @binding(1) var<storage, read_write> upperE_out: array<u32>;

@compute @workgroup_size(64)
fn upperE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 6u) {
    return;
  }
  let i = idx / 3u;
  let j = idx % 3u;
  upperE_out[idx] = select(0u, 1u, select(false, (upperE_in0[i * 3u + j] != 0u), (i <= j)));
}

// shiftE: Tile 2 3 U32 -> Tile 2 2 U32
@group(0) @binding(0) var<storage, read> shiftE_in0: array<u32>;
@group(0) @binding(1) var<storage, read_write> shiftE_out: array<u32>;

@compute @workgroup_size(64)
fn shiftE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 4u) {
    return;
  }
  let i = idx / 2u;
  let j = idx % 2u;
  shiftE_out[idx] = shiftE_in0[select(0u, 1u - i, 1u >= i) * 3u + (j + 1u)];
}

// offsetE: Tile 2 3 U32 -> Tile 2 3 U32
@group(0) @binding(0) var<storage, read> offsetE_in0: array<u32>;
@group(0) @binding(1) var<storage, read_write> offsetE_out: array<u32>;

@compute @workgroup_size(64)
fn offsetE(@builtin(global_invocation_id) id: vec3<u32>) {
  let idx = id.x;
  if (idx >= 6u) {
    return;
  }
  let i = idx / 3u;
  let j = idx % 3u;
  var l0 = 0u;
  for (var l1 = 0u; l1 < ((i * 3u) + (j + 1u)); l1 = l1 + 1u) {
    l0 = l0 + 1u;
  }
  offsetE_out[idx] = (offsetE_in0[i * 3u + j] + l0);
}
