/* Execute emitted expressions with geth, including the map storage layout. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/evm.c"

static unsigned executions;

static int execute(const char *name, Asm *a, const unsigned char *want, int trap) {
  char command[2 * ASM_CAPACITY + 64];
  char got[256];
  char expected[68] = "0x";
  size_t used = (size_t)snprintf(command, sizeof command, "evm --code ");
  executions++;
  if (!asm_finish(a, stderr)) { free(a); return 0; }
  for (size_t i = 0; i < a->size; i++)
    used += (size_t)snprintf(command + used, sizeof command - used, "%02x", a->code[i]);
  snprintf(command + used, sizeof command - used, " run 2>&1");
  FILE *pipe = popen(command, "r");
  if (pipe == NULL) { free(a); return 0; }
  size_t n = fread(got, 1, sizeof got - 1, pipe);
  got[n] = '\0';
  int status = pclose(pipe);
  if (want != NULL) {
    for (size_t i = 0; i < 32; i++) snprintf(expected + 2 + 2 * i, 3, "%02x", want[i]);
    strcpy(expected + 66, "\n");
  }
  int ok = trap ? strstr(got, "error: execution reverted") != NULL
                : status == 0 && strcmp(got, expected) == 0;
  if (!ok) fprintf(stderr, "FAIL emit %s: %s", name, got);
  free(a);
  return ok;
}

static int expression(const char *name, const IrExpr *expr, const unsigned char *want, int trap,
                      const unsigned char *slot, int write) {
  Asm *a = calloc(1, sizeof *a);
  if (a == NULL) return 0;
  Emit e = {a, 32, asm_label(a), asm_label(a), EVM_BUILD_IR};
  if (slot != NULL && !write) {
    asm_push(a, 99);
    asm_push_word(a, slot);
    asm_op(a, EVM_OP_SSTORE);
  }
  int ok;
  if (write) {
    const IrExpr value = {.kind = IR_EXPR_CONST, .value = 99};
    const IrStmt store = {.kind = IR_STMT_SSTORE, .expr = expr, .value = &value};
    ok = emit_stmt(&e, &store);
    asm_push_word(a, slot);
    asm_op(a, EVM_OP_SLOAD);
  } else {
    ok = emit_expr(&e, expr);
  }
  if (!ok) { free(a); return 0; }
  emit_store(a, 0);
  asm_push(a, 32);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_RETURN);
  asm_jumpdest(a, e.revert);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_REVERT);
  asm_jumpdest(a, e.trap);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_REVERT);
  return execute(name, a, want, trap);
}

static int binary(const char *name, IrOp op, uint64_t left, uint64_t right, uint64_t want, int trap) {
  const IrExpr x = {.kind = IR_EXPR_CONST, .value = left};
  const IrExpr y = {.kind = IR_EXPR_CONST, .value = right};
  const IrExpr expr = {.kind = IR_EXPR_BINARY, .op = op, .left = &x, .right = &y};
  unsigned char word[32] = {0};
  for (size_t i = 0; i < 8; i++) word[31 - i] = (unsigned char)(want >> (8 * i));
  return expression(name, &expr, word, trap, NULL, 0);
}

int main(void) {
  if (system("command -v evm >/dev/null 2>&1") != 0) {
    puts("emit: skipped (evm not on PATH)");
    return 0;
  }
  int ok = 1;
  unsigned char input[64] = {0}, slot[32], value[32] = {0};
  input[31] = 7;
  input[63] = 4;
  value[31] = 99;
  keccak256(input, sizeof input, slot);
  const IrExpr key = {.kind = IR_EXPR_CONST, .value = 7};
  const IrExpr field = {.kind = IR_EXPR_CONST, .value = 4};
  const IrExpr hash = {.kind = IR_EXPR_BINARY, .op = IR_OP_KECCAK, .left = &key, .right = &field};
  const IrExpr load = {.kind = IR_EXPR_SLOAD, .left = &hash};
  ok &= expression("map-key-slot-hash", &hash, slot, 0, NULL, 0);
  ok &= expression("map-read-initialized-slot", &load, value, 0, slot, 0);
  ok &= expression("map-write-layout", &hash, value, 0, slot, 1);
  ok &= binary("sub", IR_OP_SUB, 9, 4, 5, 0);
  ok &= binary("sub-saturates", IR_OP_SUB, 4, 9, 0, 0);
  ok &= binary("min-left", IR_OP_MIN, 4, 9, 4, 0);
  ok &= binary("min-right", IR_OP_MIN, 9, 4, 4, 0);
  ok &= binary("div", IR_OP_DIV, 9, 4, 2, 0);
  ok &= binary("div-zero", IR_OP_DIV, 9, 0, 0, 1);
  ok &= binary("add64-limit", IR_OP_ADD64, UINT64_MAX, 0, UINT64_MAX, 0);
  ok &= binary("add64-overflow", IR_OP_ADD64, UINT64_MAX, 1, 0, 1);
  ok &= binary("mul64-overflow", IR_OP_MUL64, UINT64_MAX, 2, 0, 1);
  ok &= binary("u256-mul", IR_OP_MUL_TRAP, 9, 4, 36, 0);
  ok &= binary("u256-mul-zero", IR_OP_MUL_TRAP, 0, 4, 0, 0);
  ok &= binary("le-true", IR_OP_LE, 4, 9, 1, 0);
  ok &= binary("le-false", IR_OP_LE, 9, 4, 0, 0);
  ok &= binary("lt-equal", IR_OP_LT, 4, 4, 0, 0);
  unsigned char max[32], zero[32] = {0}, nested_input[64], nested_hash[32];
  memset(max, 255, sizeof max);
  const IrExpr full = {.kind = IR_EXPR_WORD, .word = max};
  const IrExpr one = {.kind = IR_EXPR_CONST, .value = 1};
  const IrExpr two = {.kind = IR_EXPR_CONST, .value = 2};
  const IrExpr empty = {.kind = IR_EXPR_CONST, .value = 0};
  const IrExpr add_over = {.kind = IR_EXPR_BINARY, .op = IR_OP_ADD_TRAP, .left = &full, .right = &one};
  const IrExpr add_ok = {.kind = IR_EXPR_BINARY, .op = IR_OP_ADD_TRAP, .left = &full, .right = &empty};
  const IrExpr mul_over = {.kind = IR_EXPR_BINARY, .op = IR_OP_MUL_TRAP, .left = &full, .right = &two};
  const IrExpr mul_zero = {.kind = IR_EXPR_BINARY, .op = IR_OP_MUL_TRAP, .left = &full, .right = &empty};
  const IrExpr nested = {.kind = IR_EXPR_BINARY, .op = IR_OP_KECCAK, .left = &hash, .right = &field};
  memcpy(nested_input, slot, 32);
  memcpy(nested_input + 32, input + 32, 32);
  keccak256(nested_input, sizeof nested_input, nested_hash);
  ok &= expression("u256-add-overflow", &add_over, NULL, 1, NULL, 0);
  ok &= expression("u256-add-limit", &add_ok, max, 0, NULL, 0);
  ok &= expression("u256-mul-overflow", &mul_over, NULL, 1, NULL, 0);
  ok &= expression("u256-mul-right-zero", &mul_zero, zero, 0, NULL, 0);
  ok &= expression("nested-map-hash", &nested, nested_hash, 0, NULL, 0);
  if (ok) printf("emit: %u execution regressions passed\n", executions);
  else puts("emit: failures");
  return !ok;
}
