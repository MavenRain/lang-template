/* Execute emitted hashing bytecode with EVM stack and memory semantics. */
#include <stdio.h>
#include <string.h>

#include "../src/evm.c"

typedef unsigned char TestWord[32];

static int offset_of(const TestWord word, size_t limit, size_t *at) {
  for (size_t i = 0; i < 31; i++)
    if (word[i] != 0) return 0;
  *at = word[31];
  return *at <= limit;
}

/* Only the opcodes used by hash expressions are accepted. Unsupported code
   fails the test instead of silently treating an instruction as a no-op. */
static int run_hash(const Asm *a, unsigned char memory[128], TestWord result) {
  TestWord stack[16];
  size_t sp = 0;
  for (size_t pc = 0; pc < a->size;) {
    unsigned op = a->code[pc++];
    if (op >= EVM_OP_PUSH0 && op <= EVM_OP_PUSH32) {
      size_t n = op - EVM_OP_PUSH0;
      if (sp == 16 || n > a->size - pc) return 0;
      memset(stack[sp], 0, 32);
      memcpy(stack[sp++] + 32 - n, a->code + pc, n);
      pc += n;
    } else if (op == EVM_OP_MSTORE) {
      size_t at;
      if (sp < 2 || !offset_of(stack[--sp], 96, &at)) return 0;
      memcpy(memory + at, stack[--sp], 32);
    } else if (op == EVM_OP_MLOAD) {
      size_t at;
      if (sp == 0 || !offset_of(stack[sp - 1], 96, &at)) return 0;
      memcpy(stack[sp - 1], memory + at, 32);
    } else if (op == EVM_OP_SHA3) {
      size_t at, n;
      if (sp < 2 || !offset_of(stack[--sp], 128, &at)
          || !offset_of(stack[--sp], 128 - at, &n)) return 0;
      keccak256(memory + at, n, stack[sp++]);
    } else {
      return 0;
    }
  }
  if (sp != 1) return 0;
  memcpy(result, stack[0], 32);
  return 1;
}

static int hash_case(const char *name, const IrExpr *expr, const TestWord left,
                     const TestWord right, const TestWord local) {
  Asm a;
  asm_init(&a);
  Emit e = {&a, 32, asm_label(&a), asm_label(&a), EVM_BUILD_IR};
  unsigned char memory[128] = {0}, preimage[64];
  TestWord actual, expected;
  memcpy(memory, local, 32);
  memcpy(preimage, left, 32);
  memcpy(preimage + 32, right, 32);
  keccak256(preimage, sizeof preimage, expected);
  int ok = emit_expr(&e, expr) && asm_finish(&a, stderr)
    && run_hash(&a, memory, actual) && memcmp(actual, expected, 32) == 0
    && memcmp(memory, local, 32) == 0;
  if (!ok) fprintf(stderr, "FAIL %s: KECCAK must hash left || right and preserve locals\n", name);
  return ok;
}

int main(void) {
  TestWord zero = {0}, seven = {0}, eleven = {0}, one = {0}, key;
  seven[31] = 7;
  eleven[31] = 11;
  one[31] = 1;
  for (size_t i = 0; i < 32; i++) key[i] = (unsigned char)(i + 1);
  IrExpr k7 = {.kind = IR_EXPR_CONST, .value = 7};
  IrExpr k11 = {.kind = IR_EXPR_CONST, .value = 11};
  IrExpr f0 = {.kind = IR_EXPR_CONST, .value = 0};
  IrExpr f1 = {.kind = IR_EXPR_CONST, .value = 1};
  IrExpr word = {.kind = IR_EXPR_WORD, .word = key};
  IrExpr local = {.kind = IR_EXPR_LOCAL, .local = 0};
  IrExpr h7 = {.kind = IR_EXPR_BINARY, .op = IR_OP_KECCAK, .left = &k7, .right = &f0};
  IrExpr h11 = {.kind = IR_EXPR_BINARY, .op = IR_OP_KECCAK, .left = &k11, .right = &f1};
  IrExpr hword = {.kind = IR_EXPR_BINARY, .op = IR_OP_KECCAK, .left = &word, .right = &f1};
  IrExpr hlocal = {.kind = IR_EXPR_BINARY, .op = IR_OP_KECCAK, .left = &local, .right = &f0};
  IrExpr nested = {.kind = IR_EXPR_BINARY, .op = IR_OP_KECCAK, .left = &h7, .right = &h11};
  unsigned char preimage[64] = {0};
  TestWord left_hash, right_hash;
  memcpy(preimage, seven, 32);
  keccak256(preimage, sizeof preimage, left_hash);
  memcpy(preimage, eleven, 32);
  memcpy(preimage + 32, one, 32);
  keccak256(preimage, sizeof preimage, right_hash);
  int ok = hash_case("map key 7, field 0", &h7, seven, zero, key);
  ok = hash_case("full word key", &hword, key, one, seven) && ok;
  ok = hash_case("local map key", &hlocal, key, zero, key) && ok;
  ok = hash_case("nested hashes", &nested, left_hash, right_hash, key) && ok;
  if (!ok) return 1;
  puts("evm hashing regressions: passed");
  return 0;
}
