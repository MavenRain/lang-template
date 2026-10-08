/* The EVM back end. Slice K4 builds the lowering and the contract code.
   Slice K1 keeps the target interface and the ABI selectors. */
#include <stdio.h>

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
