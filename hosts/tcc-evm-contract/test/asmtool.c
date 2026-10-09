/* The self-test of the assembler, keccak and the ABI selectors.
   Usage: asmtool            runs the checks; exit 0 when all pass
          asmtool runtime    the runtime hex of a program that returns 42
          asmtool creation   the creation hex of that program
          asmtool abi        the `langc abi` line of addPrice(Nat, Flag) */
#include <stdio.h>
#include <string.h>

#include "asm.h"
#include "keccak.h"
#include "target.h"

enum { CHECKS = 8 };

static void to_hex(const unsigned char *bytes, size_t size, char *text) {
  for (size_t i = 0; i < size; i++) snprintf(text + 2 * i, 3, "%02x", bytes[i]);
  text[2 * size] = '\0';
}

/* Returns 1 when the hex of keccak256(INPUT) starts with WANT, else 0 after
   a FAIL line. */
static int check_keccak(const char *input, const char *want) {
  unsigned char digest[32];
  char text[65];
  keccak256((const unsigned char *)input, strlen(input), digest);
  to_hex(digest, 32, text);
  if (strncmp(text, want, strlen(want)) == 0) return 1;
  printf("FAIL keccak256(\"%s\"): want %s, got %s\n", input, want, text);
  return 0;
}

/* Jumps forward to `start`, which jumps back to `back`, which returns the
   word 42. */
static int answer(Asm *a, FILE *err) {
  asm_init(a);
  Label start = asm_label(a);
  Label back = asm_label(a);
  asm_jump(a, start);
  asm_jumpdest(a, back);
  asm_push(a, 42);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_MSTORE);
  asm_push(a, 32);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_RETURN);
  asm_jumpdest(a, start);
  asm_jump(a, back);
  return asm_finish(a, err);
}

static int check_answer(const Asm *a) {
  static const char want[] = "61000d565b602a5f5260205ff35b61000456";
  char text[2 * sizeof want];
  int ok = 2 * a->size + 1 == sizeof want;
  if (ok) to_hex(a->code, a->size, text);
  if (ok && strcmp(text, want) == 0) return 1;
  printf("FAIL answer: the code differs from %s\n", want);
  return 0;
}

/* asm_finish refuses an unbound label, a stray label and a full label table. */
static int check_refusals(FILE *quiet) {
  static Asm a;
  asm_init(&a);
  asm_jump(&a, asm_label(&a));
  int unbound = !asm_finish(&a, quiet);
  asm_init(&a);
  asm_bind(&a, 7);
  int stray = !asm_finish(&a, quiet);
  asm_init(&a);
  for (size_t i = 0; i <= ASM_LABELS; i++) asm_label(&a);
  int full = !asm_finish(&a, quiet);
  if (unbound && stray && full) return 1;
  printf("FAIL refusals: unbound %d, stray %d, full %d\n", unbound, stray, full);
  return 0;
}

/* One row with each scalar type, then the Trap() selector of C-K4-9 (a
   name with no parameters). */
static int abi(FILE *out, FILE *err) {
  static const IrScalar params[] = {IR_SCALAR_NAT, IR_SCALAR_FLAG, IR_SCALAR_U256, IR_SCALAR_ADDR};
  IrFunc fn;
  IrFunc trap;
  memset(&fn, 0, sizeof fn);
  memset(&trap, 0, sizeof trap);
  fn.name = "addPrice";
  fn.params = params;
  fn.param_count = 4;
  trap.name = "Trap";
  return target_abi_line(&fn, out, err) && target_abi_line(&trap, out, err);
}

static int checks(const Asm *runtime) {
  FILE *quiet = fopen("/dev/null", "w");
  int passed = check_keccak("", "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470")
             + check_keccak("abc", "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45")
             + check_keccak("transfer(address,uint256)", "a9059cbb")
             + check_keccak("transferFrom(address,address,uint256)", "23b872dd")
             + check_keccak("approve(address,uint256)", "095ea7b3")
             + check_keccak("balanceOf(address)", "70a08231")
             + check_answer(runtime)
             + check_refusals(quiet != NULL ? quiet : stderr);
  if (quiet != NULL) fclose(quiet);
  printf("asmtool: %d of %d checks passed\n", passed, CHECKS);
  return passed == CHECKS ? 0 : 1;
}

int main(int argc, char **argv) {
  static Asm runtime;
  static Asm creation;
  const char *mode = argc == 2 ? argv[1] : "";
  if (argc > 2) return fputs("usage: asmtool [runtime|creation|abi]\n", stderr), 2;
  if (!answer(&runtime, stderr)) return 1;
  if (strcmp(mode, "runtime") == 0) return asm_write_hex(&runtime, stdout, stderr) ? 0 : 1;
  if (strcmp(mode, "creation") == 0) return asm_creation(&creation, &runtime, stderr) && asm_write_hex(&creation, stdout, stderr) ? 0 : 1;
  if (strcmp(mode, "abi") == 0) return abi(stdout, stderr) ? 0 : 1;
  if (argc == 2) return fputs("usage: asmtool [runtime|creation|abi]\n", stderr), 2;
  return checks(&runtime);
}
