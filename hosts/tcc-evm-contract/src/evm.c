/* The EVM back end. Slice K4 builds the lowering and the contract code.
   Slice K1 keeps the target interface and the ABI selectors. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asm.h"
#include "evm.h"
#include "keccak.h"
#include "target.h"

enum { EVM_SIGNATURE = 256 };

const char target_name[] = "evm";

/* The ABI type name of a scalar (C-K4-6). */
static const char *abi_type(IrScalar s) {
  switch (s) {
  case IR_SCALAR_NAT: return "uint64";
  case IR_SCALAR_FLAG: return "bool";
  case IR_SCALAR_U256: return "uint256";
  case IR_SCALAR_ADDR: return "address";
  }
  return "uint256";
}

/* NAME(TYPE,...) with the ABI type of each of the COUNT TYPES: Nat =
   uint64, Flag = bool, U256 = uint256, Addr = address (an entry, a view or
   an event, C-K4c-4). Returns the length, or 0 when it does not fit. */
static size_t signature_of(char *text, size_t capacity, const char *name, const IrScalar *types, size_t count) {
  int used = snprintf(text, capacity, "%s(", name);
  for (size_t i = 0; used > 0 && (size_t)used < capacity && i < count; i++) {
    used += snprintf(text + used, capacity - (size_t)used, "%s%s", i == 0 ? "" : ",", abi_type(types[i]));
  }
  if (used > 0 && (size_t)used < capacity) used += snprintf(text + used, capacity - (size_t)used, ")");
  return used > 0 && (size_t)used < capacity ? (size_t)used : 0;
}

static size_t signature(char *text, const IrFunc *fn) {
  return signature_of(text, EVM_SIGNATURE, fn->name, fn->params, fn->param_count);
}

/* Event names are not limited by the entry selector's signature buffer. */
static EvmBuild event_topic(const IrStmt *s, unsigned char topic[32]) {
  size_t name_size = strlen(s->name);
  /* Each ABI type is at most seven bytes, plus its comma. */
  if (name_size > SIZE_MAX - 3u || s->field_count > (SIZE_MAX - name_size - 3u) / 8u)
    return EVM_BUILD_SIZE;
  size_t capacity = name_size + 3u + 8u * s->field_count;
  char *text = malloc(capacity);
  if (text == NULL) return EVM_BUILD_OOM;
  size_t size = signature_of(text, capacity, s->name, s->types, s->field_count);
  if (size > 0) keccak256((const unsigned char *)text, size, topic);
  free(text);
  return size > 0 ? EVM_BUILD_OK : EVM_BUILD_IR;
}

EvmBuild evm_abi_line(const char *kind, const char *name, const IrScalar *types, size_t count, FILE *out) {
  int event = strcmp(kind, "event") == 0;
  size_t name_size = strlen(name);
  if (name_size > SIZE_MAX - 3u || count > (SIZE_MAX - name_size - 3u) / 8u) return EVM_BUILD_SIZE;
  size_t capacity = name_size + 3u + 8u * count;
  char *text = malloc(capacity);
  if (text == NULL) return EVM_BUILD_OOM;
  size_t size = signature_of(text, capacity, name, types, count);
  int fits = size > 0 && (event || size < EVM_SIGNATURE);
  unsigned char digest[32];
  if (fits) {
    keccak256((const unsigned char *)text, size, digest);
    fputs("0x", out);
    for (size_t i = 0; i < (event ? 32u : 4u); i++) fprintf(out, "%02x", digest[i]);
    fprintf(out, " %s %s\n", text, kind);
  }
  free(text);
  return size == 0 ? EVM_BUILD_IR : !fits ? EVM_BUILD_SIGNATURE : ferror(out) ? EVM_BUILD_WRITE : EVM_BUILD_OK;
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

/* The selector of FN (C-K4-11): the first 4 bytes of the keccak256 of its
   signature, as a number. Returns 0 when the signature does not fit. */
static int selector(const IrFunc *fn, uint64_t *out) {
  char text[EVM_SIGNATURE];
  unsigned char digest[32];
  size_t size = signature(text, fn);
  if (size == 0) return 0;
  keccak256((const unsigned char *)text, size, digest);
  *out = (uint64_t)digest[0] << 24 | (uint64_t)digest[1] << 16 | (uint64_t)digest[2] << 8 | digest[3];
  return 1;
}

static EvmBuild program_selectors(const IrProgram *prog, uint64_t *hits) {
  if (prog->func_count > ASM_LABELS) return EVM_BUILD_SIZE;
  for (size_t i = 0; i < prog->func_count; i++) {
    if (!selector(&prog->funcs[i], &hits[i])) return EVM_BUILD_SIGNATURE;
    for (size_t j = 0; j < i; j++)
      if (hits[j] == hits[i]) return EVM_BUILD_SELECTOR;
  }
  return EVM_BUILD_OK;
}

EvmBuild evm_abi_check(const IrProgram *prog) {
  uint64_t hits[ASM_LABELS];
  return program_selectors(prog, hits);
}

/* The number of low bits that a clean argument word can use (C-K4-6).
   256 = all words are clean. */
static unsigned scalar_bits(IrScalar s) {
  switch (s) {
  case IR_SCALAR_NAT: return 64;
  case IR_SCALAR_FLAG: return 1;
  case IR_SCALAR_U256: return 256;
  case IR_SCALAR_ADDR: return 160;
  }
  return 256;
}

int target_write(const IrProgram *prog, TargetPart part, FILE *out, FILE *err) {
  (void)prog;
  (void)part;
  (void)out;
  fputs("langc: PLANNED: -: the evm back end is not built yet (slice K4)\n", err);
  return 0;
}

/* IR to EVM (C-K4b-1, C-K4b-7). Local k is the memory word at 32 * k. The
   two words after the locals of an entry are a scratch for KECCAK and
   MUL_TRAP: they use it only after both operands are on the stack. The
   words after the scratch are the data of a LOG (C-K4-13). */
typedef struct {
  Asm *a;
  uint64_t scratch;
  Label revert;
  Label trap;
  EvmBuild failure;
} Emit;

static void emit_ops(Asm *a, const Op *ops, size_t count) {
  for (size_t i = 0; i < count; i++) asm_op(a, ops[i]);
}

static int emit_expr(Emit *e, const IrExpr *x);

/* RIGHT, then LEFT on top: the EVM operand order of SUB, DIV, LT and GT. */
static int emit_operands(Emit *e, const IrExpr *x) {
  return emit_expr(e, x->right) && emit_expr(e, x->left);
}

static void emit_load(Asm *a, uint64_t at) {
  asm_push(a, at);
  asm_op(a, EVM_OP_MLOAD);
}

static void emit_store(Asm *a, uint64_t at) {
  asm_push(a, at);
  asm_op(a, EVM_OP_MSTORE);
}

static int emit_binary(Emit *e, const IrExpr *x) {
  static const Op sub_sat[] = {EVM_OP_DUP2,  EVM_OP_DUP2,  EVM_OP_SUB,    EVM_OP_SWAP2,
                               EVM_OP_SWAP1, EVM_OP_LT,    EVM_OP_ISZERO, EVM_OP_MUL};
  static const Op min[] = {EVM_OP_DUP2,   EVM_OP_DUP2,  EVM_OP_SUB,   EVM_OP_SWAP2, EVM_OP_DUP2,
                           EVM_OP_LT,     EVM_OP_ISZERO, EVM_OP_SWAP2, EVM_OP_SWAP1, EVM_OP_SWAP2,
                           EVM_OP_MUL,    EVM_OP_SWAP1, EVM_OP_SUB};
  static const Op add_trap[] = {EVM_OP_DUP1, EVM_OP_SWAP2, EVM_OP_ADD, EVM_OP_DUP1,
                                EVM_OP_SWAP2, EVM_OP_SWAP1, EVM_OP_LT};
  Asm *a = e->a;
  if (x->op == IR_OP_DIV) {
    if (!emit_expr(e, x->right)) return 0;
    emit_ops(a, (const Op[]){EVM_OP_DUP1, EVM_OP_ISZERO}, 2);
    asm_jump_if(a, e->trap);
    if (!emit_expr(e, x->left)) return 0;
    asm_op(a, EVM_OP_DIV);
    return 1;
  }
  if (!emit_operands(e, x)) return 0;
  switch (x->op) {
  case IR_OP_ADD: asm_op(a, EVM_OP_ADD); asm_push(a, UINT64_MAX); asm_op(a, EVM_OP_AND); return 1;
  case IR_OP_MUL: asm_op(a, EVM_OP_MUL); asm_push(a, UINT64_MAX); asm_op(a, EVM_OP_AND); return 1;
  case IR_OP_SUB: emit_ops(a, sub_sat, sizeof sub_sat / sizeof sub_sat[0]); return 1;
  case IR_OP_EQ: asm_op(a, EVM_OP_EQ); return 1;
  case IR_OP_LE: emit_ops(a, (const Op[]){EVM_OP_GT, EVM_OP_ISZERO}, 2); return 1;
  case IR_OP_LT: asm_op(a, EVM_OP_LT); return 1;
  case IR_OP_OR: asm_op(a, EVM_OP_OR); return 1;
  case IR_OP_MIN: emit_ops(a, min, sizeof min / sizeof min[0]); return 1;
  case IR_OP_ADD_CARRY:
  case IR_OP_MUL_CARRY:
    asm_op(a, x->op == IR_OP_ADD_CARRY ? EVM_OP_ADD : EVM_OP_MUL);
    asm_push(a, UINT64_MAX);
    asm_op(a, EVM_OP_LT);
    return 1;
  case IR_OP_ADD64:
  case IR_OP_MUL64:
    emit_ops(a, (const Op[]){x->op == IR_OP_ADD64 ? EVM_OP_ADD : EVM_OP_MUL, EVM_OP_DUP1}, 2);
    asm_push(a, UINT64_MAX);
    asm_op(a, EVM_OP_LT);
    asm_jump_if(a, e->trap);
    return 1;
  case IR_OP_ADD_TRAP:
    emit_ops(a, add_trap, sizeof add_trap / sizeof add_trap[0]);
    asm_jump_if(a, e->trap);
    return 1;
  case IR_OP_MUL_TRAP: /* left != 0 and product / left != right */
    asm_op(a, EVM_OP_DUP1);
    emit_store(a, e->scratch);
    asm_op(a, EVM_OP_DUP2);
    emit_store(a, e->scratch + 32);
    emit_ops(a, (const Op[]){EVM_OP_MUL, EVM_OP_DUP1}, 2);
    emit_load(a, e->scratch);
    emit_ops(a, (const Op[]){EVM_OP_SWAP1, EVM_OP_DIV}, 2);
    emit_load(a, e->scratch + 32);
    emit_ops(a, (const Op[]){EVM_OP_EQ, EVM_OP_ISZERO}, 2);
    emit_load(a, e->scratch);
    emit_ops(a, (const Op[]){EVM_OP_ISZERO, EVM_OP_ISZERO, EVM_OP_AND}, 3);
    asm_jump_if(a, e->trap);
    return 1;
  case IR_OP_KECCAK:
    /* LEFT is on top, and the preimage is LEFT followed by RIGHT. */
    emit_store(a, e->scratch);
    emit_store(a, e->scratch + 32);
    asm_push(a, 64);
    asm_push(a, e->scratch);
    asm_op(a, EVM_OP_SHA3);
    return 1;
  case IR_OP_DIV: return 0;
  }
  return 0;
}

static int emit_expr(Emit *e, const IrExpr *x) {
  switch (x->kind) {
  case IR_EXPR_CONST: asm_push(e->a, x->value); return 1;
  case IR_EXPR_LOCAL: emit_load(e->a, 32u * (uint64_t)x->local); return 1;
  case IR_EXPR_BINARY: return emit_binary(e, x);
  case IR_EXPR_LOAD: return 0; /* no heap blocks on the EVM path */
  case IR_EXPR_WORD: asm_push_word(e->a, x->word); return 1;
  case IR_EXPR_ENV: asm_op(e->a, x->field == IR_ENV_NOW ? EVM_OP_TIMESTAMP : EVM_OP_CALLER); return 1;
  case IR_EXPR_SLOAD:
    if (!emit_expr(e, x->left)) return 0;
    asm_op(e->a, EVM_OP_SLOAD);
    return 1;
  }
  return 0;
}

static int emit_block(Emit *e, IrBlock b);

static int emit_stmt(Emit *e, const IrStmt *s) {
  Asm *a = e->a;
  switch (s->kind) {
  case IR_STMT_SET:
    if (!emit_expr(e, s->expr)) return 0;
    emit_store(a, 32u * (uint64_t)s->local);
    return 1;
  case IR_STMT_IF: {
    Label no = asm_label(a), end = asm_label(a);
    if (!emit_expr(e, s->expr)) return 0;
    asm_op(a, EVM_OP_ISZERO);
    asm_jump_if(a, no);
    if (!emit_block(e, s->body)) return 0;
    asm_jump(a, end);
    asm_jumpdest(a, no);
    if (!emit_block(e, s->otherwise)) return 0;
    asm_jumpdest(a, end);
    return 1;
  }
  case IR_STMT_SSTORE:
    if (!emit_expr(e, s->value) || !emit_expr(e, s->expr)) return 0;
    asm_op(a, EVM_OP_SSTORE);
    return 1;
  case IR_STMT_TRAP: asm_jump(a, e->trap); return 1;
  case IR_STMT_REVERT: asm_jump(a, e->revert); return 1;
  case IR_STMT_STOP: asm_op(a, EVM_OP_STOP); return 1;
  case IR_STMT_RETURN: /* a view (C-K4-10): the word at memory 0, 32 bytes */
    if (!emit_expr(e, s->expr)) return 0;
    emit_ops(a, (const Op[]){EVM_OP_PUSH0, EVM_OP_MSTORE}, 2);
    asm_push(a, 32);
    emit_ops(a, (const Op[]){EVM_OP_PUSH0, EVM_OP_RETURN}, 2);
    return 1;
  case IR_STMT_LOG: { /* an event (C-K4-13): the field words after the scratch, then LOG1 */
    unsigned char topic[32];
    EvmBuild result = event_topic(s, topic);
    if (result != EVM_BUILD_OK) { e->failure = result; return 0; }
    for (size_t i = 0; i < s->field_count; i++) {
      if (!emit_expr(e, s->fields[i])) return 0;
      emit_store(a, e->scratch + 64u + 32u * (uint64_t)i);
    }
    asm_push_word(a, topic);
    asm_push(a, 32u * (uint64_t)s->field_count);
    asm_push(a, e->scratch + 64u);
    asm_op(a, EVM_OP_LOG1);
    return 1;
  }
  case IR_STMT_ALLOC:
  case IR_STMT_SWITCH:
  case IR_STMT_REPEAT:
  case IR_STMT_WHILE: return 0;
  }
  return 0;
}

static int emit_block(Emit *e, IrBlock b) {
  int ok = 1;
  for (size_t i = 0; ok && i < b.count; i++) ok = emit_stmt(e, b.items[i]);
  return ok;
}

/* Entry FN at label LABEL (C-K4b-1): pop the selector, revert on call data
   shorter than 4 + 32 * N, store argument word k at local k after the
   dirty-bit check (C-K4-6: a dirty word gives the empty REVERT), then the
   body. The body of a view ends in RETURN (C-K4-10). Returns 0 on an IR
   form that the EVM back end does not lower. */
static int evm_entry(Emit *e, const IrFunc *fn, Label label) {
  Asm *a = e->a;
  e->scratch = 32u * (uint64_t)fn->local_count;
  asm_jumpdest(a, label);
  asm_op(a, EVM_OP_POP);
  asm_push(a, 4u + 32u * (uint64_t)fn->param_count);
  emit_ops(a, (const Op[]){EVM_OP_CALLDATASIZE, EVM_OP_LT}, 2);
  asm_jump_if(a, e->revert);
  for (size_t k = 0; k < fn->param_count; k++) {
    unsigned bits = scalar_bits(fn->params[k]);
    asm_push(a, 4u + 32u * (uint64_t)k);
    asm_op(a, EVM_OP_CALLDATALOAD);
    if (bits < 256) {
      asm_op(a, EVM_OP_DUP1);
      asm_push(a, bits);
      asm_op(a, EVM_OP_SHR);
      asm_jump_if(a, e->revert);
    }
    emit_store(a, 32u * (uint64_t)k);
  }
  return emit_block(e, fn->body);
}

/* The runtime (C-K4b-1; slice K4c: the entries and the views, C-K4-10). The
   dispatcher head reverts on a call value or on call data shorter than 4
   bytes (C-K4-11), puts the selector on the stack and jumps to function i
   (an entry or a view) at label FIRST + i on a match.
   An unknown selector falls through to the shared empty REVERT block
   (C-K4-8). The shared Trap() REVERT block (C-K4-9), the entries and the
   views come after it. */
static EvmBuild evm_runtime(Asm *a, const IrProgram *prog, Label first) {
  uint64_t hits[ASM_LABELS];
  EvmBuild status = program_selectors(prog, hits);
  if (status != EVM_BUILD_OK) return status;
  Emit e = {a, 0, asm_label(a), asm_label(a), EVM_BUILD_IR};
  asm_op(a, EVM_OP_CALLVALUE);
  asm_jump_if(a, e.revert);
  asm_push(a, 4);
  emit_ops(a, (const Op[]){EVM_OP_CALLDATASIZE, EVM_OP_LT}, 2);
  asm_jump_if(a, e.revert);
  asm_push(a, 0);
  asm_op(a, EVM_OP_CALLDATALOAD);
  asm_push(a, 224);
  asm_op(a, EVM_OP_SHR);
  for (size_t i = 0; i < prog->func_count; i++) {
    asm_op(a, EVM_OP_DUP1);
    asm_push(a, hits[i]);
    asm_op(a, EVM_OP_EQ);
    asm_jump_if(a, first + (Label)i);
  }
  asm_jumpdest(a, e.revert);
  emit_ops(a, (const Op[]){EVM_OP_PUSH0, EVM_OP_PUSH0, EVM_OP_REVERT}, 3);
  asm_jumpdest(a, e.trap);
  asm_push(a, 0xae96083aU);
  asm_push(a, 224);
  emit_ops(a, (const Op[]){EVM_OP_SHL, EVM_OP_PUSH0, EVM_OP_MSTORE}, 3);
  asm_push(a, 4);
  emit_ops(a, (const Op[]){EVM_OP_PUSH0, EVM_OP_REVERT}, 2);
  int ok = 1;
  for (size_t i = 0; ok && i < prog->func_count; i++) ok = evm_entry(&e, &prog->funcs[i], first + (Label)i);
  return ok ? EVM_BUILD_OK : e.failure;
}

static int is_zero(const unsigned char *w) {
  for (int i = 0; i < 32; i++)
    if (w[i] != 0) return 0;
  return 1;
}

/* Slice K4a. The asm messages go to a null sink: the caller writes the
   diagnostic from the return code. */
EvmBuild evm_build(const IrProgram *prog, const unsigned char (*pairs)[64], size_t count, TargetPart part,
                   FILE *out) {
  Asm *a = calloc(3, sizeof(Asm));
  FILE *sink = fopen("/dev/null", "w");
  if (a == NULL || sink == NULL) {
    free(a);
    if (sink != NULL) fclose(sink);
    return EVM_BUILD_OOM;
  }
  Asm *stores = &a[0], *runtime = &a[1], *code = &a[2];
  Label first = prog->func_count > 0 ? asm_label(runtime) : 0;
  for (size_t i = 1; i < prog->func_count; i++) asm_label(runtime);
  EvmBuild built = evm_runtime(runtime, prog, first);
  if (built != EVM_BUILD_OK) {
    fclose(sink);
    free(a);
    return built;
  }
  for (size_t i = 0; part != TARGET_PART_RUNTIME && i < count; i++) {
    if (is_zero(pairs[i] + 32)) continue;
    asm_push_word(stores, pairs[i] + 32);
    asm_push_word(stores, pairs[i]);
    asm_op(stores, EVM_OP_SSTORE);
  }
  EvmBuild result = stores->full || !asm_finish(runtime, sink) || runtime->size > ASM_RUNTIME_MAX
                        || (part != TARGET_PART_RUNTIME && !asm_creation_store(code, stores, runtime, sink))
                        ? EVM_BUILD_SIZE
                    : asm_write_hex(part == TARGET_PART_RUNTIME ? runtime : code, out, sink) ? EVM_BUILD_OK
                                                                                            : EVM_BUILD_WRITE;
  fclose(sink);
  free(a);
  return result;
}
