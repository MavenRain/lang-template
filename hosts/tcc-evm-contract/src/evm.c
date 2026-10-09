/* The EVM back end. Slice K4 builds the lowering and the contract code.
   Slice K1 keeps the target interface and the ABI selectors. */
#include <stdio.h>
#include <stdlib.h>

#include "asm.h"
#include "evm.h"
#include "keccak.h"
#include "target.h"

enum { EVM_SIGNATURE = 256 };

const char target_name[] = "evm";

/* NAME(uint256,...) with one uint256 for each parameter: Nat and Flag are
   both one word. Returns the length, or 0 when it does not fit. */
static size_t signature(char *text, const IrFunc *fn) {
  int used = snprintf(text, EVM_SIGNATURE, "%s(", fn->name);
  for (size_t i = 0; used > 0 && used < EVM_SIGNATURE && i < fn->param_count; i++) {
    used += snprintf(text + used, EVM_SIGNATURE - (size_t)used, "%suint256", i == 0 ? "" : ",");
  }
  if (used > 0 && used < EVM_SIGNATURE) used += snprintf(text + used, EVM_SIGNATURE - (size_t)used, ")");
  return used > 0 && used < EVM_SIGNATURE ? (size_t)used : 0;
}

int target_abi_line(const IrFunc *fn, FILE *out, FILE *err) {
  char text[EVM_SIGNATURE];
  size_t size = signature(text, fn);
  if (size == 0) {
    fprintf(err, "langc: EVM_SIGNATURE: %s: the signature is longer than %d bytes\n", fn->name, EVM_SIGNATURE - 1);
    return 0;
  }
  unsigned char digest[32];
  keccak256((const unsigned char *)text, size, digest);
  fprintf(out, "%s 0x%02x%02x%02x%02x\n", fn->name, digest[0], digest[1], digest[2], digest[3]);
  return 1;
}

int target_write(const IrProgram *prog, TargetPart part, FILE *out, FILE *err) {
  (void)prog;
  (void)part;
  (void)out;
  fputs("langc: PLANNED: -: the evm back end is not built yet (slice K4)\n", err);
  return 0;
}

static int is_zero(const unsigned char *w) {
  for (int i = 0; i < 32; i++)
    if (w[i] != 0) return 0;
  return 1;
}

/* Slice K4a. The asm messages go to a null sink: the caller writes the
   diagnostic from the return code. */
EvmBuild evm_build(const unsigned char (*pairs)[64], size_t count, TargetPart part, FILE *out) {
  Asm *a = calloc(3, sizeof(Asm));
  FILE *sink = fopen("/dev/null", "w");
  if (a == NULL || sink == NULL) {
    free(a);
    if (sink != NULL) fclose(sink);
    return EVM_BUILD_OOM;
  }
  Asm *stores = &a[0], *runtime = &a[1], *code = &a[2];
  for (size_t i = 0; i < count; i++) {
    if (is_zero(pairs[i] + 32)) continue;
    asm_push_word(stores, pairs[i] + 32);
    asm_push_word(stores, pairs[i]);
    asm_op(stores, EVM_OP_SSTORE);
  }
  asm_op(runtime, EVM_OP_PUSH0);
  asm_op(runtime, EVM_OP_PUSH0);
  asm_op(runtime, EVM_OP_REVERT);
  EvmBuild result = stores->full || !asm_finish(runtime, sink) || !asm_creation_store(code, stores, runtime, sink)
                        ? EVM_BUILD_SIZE
                    : asm_write_hex(part == TARGET_PART_RUNTIME ? runtime : code, out, sink) ? EVM_BUILD_OK
                                                                                            : EVM_BUILD_WRITE;
  fclose(sink);
  free(a);
  return result;
}
