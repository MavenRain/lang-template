/* The assembler self-test of the tcc-evm kit. It checks keccak256 against
   two known vectors, then prints `NAME HEX WANT` for hand-built snippets.
   test/gate.sh runs each HEX with `evm` and compares the result to WANT.
   It includes src/evm.c to reach the static assembler. */
#include "evm.c"

static int ret(Asm *a) { return push(a, 0) && op(a, EVM_MSTORE) && push(a, EVM_WORD) && push(a, 0) && op(a, EVM_RETURN); }

static int put_snippet(Asm *a, const char *name, const char *want) {
  uint8_t *code = NULL;
  size_t size = 0;
  int ok = asm_finish(a, &code, &size);
  printf("%s ", name);
  ok = ok && put_hex(stdout, code, size);
  printf(" %s\n", want);
  return ok;
}

static int check_hash(const char *text, const char *want_hex, size_t want_bytes) {
  unsigned char digest[32];
  char got[65];
  size_t i;
  keccak256((const unsigned char *)text, strlen(text), digest);
  for (i = 0; i < want_bytes; i++) snprintf(got + 2u * i, 3, "%02x", digest[i]);
  if (strcmp(got, want_hex) != 0) {
    printf("FAIL keccak256(\"%s\") = %s, want %s\n", text, got, want_hex);
    return 0;
  }
  return 1;
}

int main(void) {
  Arena arena;
  Asm a;
  uint64_t l1;
  uint64_t l2;
  int k;
  int ok = check_hash("", "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470", 32) &&
           check_hash("transfer(address,uint256)", "a9059cbb", 4);
  printf("keccak: 2 vectors %s\n", ok ? "ok" : "FAILED");
  arena_init(&arena, EVM_ARENA_LIMIT);
  /* 2 + 3 */
  ok = ok && asm_init(&a, &arena) && push(&a, 2) && push(&a, 3) && op(&a, EVM_ADD) && ret(&a) &&
       put_snippet(&a, "add", "5");
  /* A forward jump over a revert. */
  l1 = 0;
  ok = ok && asm_init(&a, &arena) && (l1 = new_label(&a), jump_to(&a, l1)) && gen_trap(&a) && place(&a, l1) &&
       push(&a, 7) && ret(&a) && put_snippet(&a, "forward", "7");
  /* A backward jump: the sum of 1 to 10, with local 0 = n and local 1 = sum. */
  l1 = 0;
  l2 = 0;
  ok = ok && asm_init(&a, &arena) && (l1 = new_label(&a), l2 = new_label(&a), push(&a, 10)) && store_local(&a, 0) &&
       push(&a, 0) && store_local(&a, 1) && place(&a, l1) && load_local(&a, 0) && op(&a, EVM_ISZERO) &&
       jump_if(&a, l2) && load_local(&a, 0) && load_local(&a, 1) && op(&a, EVM_ADD) && store_local(&a, 1) &&
       push(&a, 1) && load_local(&a, 0) && op(&a, EVM_SUB) && store_local(&a, 0) && jump_to(&a, l1) &&
       place(&a, l2) && load_local(&a, 1) && ret(&a) && put_snippet(&a, "loop", "55");
  /* A label past byte 255 needs a 2-byte push. */
  l1 = 0;
  ok = ok && asm_init(&a, &arena) && (l1 = new_label(&a), jump_to(&a, l1));
  for (k = 0; k < 300 && ok; k++) ok = op(&a, EVM_JUMPDEST);
  ok = ok && place(&a, l1) && push(&a, 9) && ret(&a) && put_snippet(&a, "wide", "9");
  ok = ok && a.items[0].width == 2;
  /* The truncated subtraction of the IR: 3 - 5 = 0, 5 - 3 = 2. */
  ok = ok && asm_init(&a, &arena) && push(&a, 5) && push(&a, 3) && gen_binary(&a, IR_OP_SUB) && ret(&a) &&
       put_snippet(&a, "sub-floor", "0");
  ok = ok && asm_init(&a, &arena) && push(&a, 3) && push(&a, 5) && gen_binary(&a, IR_OP_SUB) && ret(&a) &&
       put_snippet(&a, "sub", "2");
  /* 2^64 - 1 + 1 wraps to 0 and sets the carry. */
  ok = ok && asm_init(&a, &arena) && push(&a, 1) && push(&a, EVM_MASK64) && gen_binary(&a, IR_OP_ADD) && ret(&a) &&
       put_snippet(&a, "add-wrap", "0");
  ok = ok && asm_init(&a, &arena) && push(&a, 1) && push(&a, EVM_MASK64) && gen_binary(&a, IR_OP_ADD_CARRY) &&
       ret(&a) && put_snippet(&a, "add-carry", "1");
  ok = ok && asm_init(&a, &arena) && gen_trap(&a) && put_snippet(&a, "revert", "trap");
  arena_release(&arena);
  printf("selftest: %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
