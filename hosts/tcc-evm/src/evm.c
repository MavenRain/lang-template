/* The EVM target of the tcc-evm kit (target.h). It writes lowercase hex.

   The assembler keeps a list of items (opcodes, pushes, label pushes and
   labels). Pass 1 gives each label its address and widens each label push
   that does not fit. Widths only grow, so pass 1 repeats until no width
   changes. Pass 2 writes the bytes. Each push has the minimal width.

   Memory: word 0 = the free pointer, local K at 32 + 32K, the heap after the
   last local. A heap word K of a block is at address + 32K. Each entry resets
   the free pointer, so the heap starts empty at each call. Words stay below
   2^64: ADD and MUL are masked, and the carry ops compare against 2^64 - 1. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "front/base.h"
#include "keccak.h"
#include "target.h"

const char target_name[] = "evm";

enum {
  EVM_ADD = 0x01,
  EVM_MUL = 0x02,
  EVM_SUB = 0x03,
  EVM_LT = 0x10,
  EVM_GT = 0x11,
  EVM_EQ = 0x14,
  EVM_ISZERO = 0x15,
  EVM_AND = 0x16,
  EVM_OR = 0x17,
  EVM_SHR = 0x1c,
  EVM_CALLVALUE = 0x34,
  EVM_CALLDATALOAD = 0x35,
  EVM_CALLDATASIZE = 0x36,
  EVM_CODECOPY = 0x39,
  EVM_POP = 0x50,
  EVM_MLOAD = 0x51,
  EVM_MSTORE = 0x52,
  EVM_JUMP = 0x56,
  EVM_JUMPI = 0x57,
  EVM_JUMPDEST = 0x5b,
  EVM_PUSH1 = 0x60,
  EVM_DUP1 = 0x80,
  EVM_DUP2 = 0x81,
  EVM_SWAP1 = 0x90,
  EVM_SWAP2 = 0x91,
  EVM_RETURN = 0xf3,
  EVM_REVERT = 0xfd
};

#define EVM_WORD 32u
#define EVM_MASK64 UINT64_MAX
#define EVM_DEPTH_MAX 4096u
#define EVM_ARENA_LIMIT ((size_t)1 << 30)
#define EVM_ARG_OFFSET 4u

typedef enum {
  ITEM_OP,
  ITEM_PUSH,
  ITEM_PUSH_LABEL,
  ITEM_LABEL
} ItemKind;

typedef struct {
  ItemKind kind;
  uint8_t op;     /* ITEM_OP: the opcode; ITEM_LABEL: JUMPDEST */
  uint8_t width;  /* the push bytes, 1 to 8 */
  uint64_t value; /* ITEM_PUSH: the constant; ITEM_PUSH_LABEL, ITEM_LABEL: the label */
} Item;

typedef struct {
  Arena *arena;
  Item *items;
  size_t count;
  size_t capacity;
  uint64_t label_count;
  uint64_t *label_at;
  const char *code; /* the diagnostic code of a failure */
  const char *def;  /* the entry of a failure, or NULL */
} Asm;

static uint8_t bytes_of(uint64_t v) {
  uint8_t n = 1;
  while (n < 8u && (v >> (8u * n)) != 0) n++;
  return n;
}

static int asm_fail(Asm *a, const char *code) {
  a->code = code;
  return 0;
}

static int asm_init(Asm *a, Arena *arena) {
  a->arena = arena;
  a->count = 0;
  a->capacity = 256;
  a->label_count = 0;
  a->label_at = NULL;
  a->code = "OOM";
  a->def = NULL;
  a->items = arena_alloc(arena, a->capacity * sizeof(Item));
  return a->items != NULL;
}

static int asm_grow(Asm *a) {
  size_t capacity = 2u * a->capacity;
  Item *bigger = arena_alloc(a->arena, capacity * sizeof(Item));
  if (bigger == NULL) return asm_fail(a, "OOM");
  memcpy(bigger, a->items, a->count * sizeof(Item));
  a->items = bigger;
  a->capacity = capacity;
  return 1;
}

static int asm_put(Asm *a, ItemKind kind, uint8_t op, uint64_t value) {
  Item *it;
  if (a->count == a->capacity && !asm_grow(a)) return 0;
  it = &a->items[a->count++];
  it->kind = kind;
  it->op = op;
  it->value = value;
  it->width = kind == ITEM_PUSH ? bytes_of(value) : (uint8_t)(kind == ITEM_PUSH_LABEL);
  return 1;
}

static int op(Asm *a, uint8_t code) { return asm_put(a, ITEM_OP, code, 0); }
static int push(Asm *a, uint64_t v) { return asm_put(a, ITEM_PUSH, 0, v); }
static uint64_t new_label(Asm *a) { return a->label_count++; }
static int place(Asm *a, uint64_t label) { return asm_put(a, ITEM_LABEL, EVM_JUMPDEST, label); }
static int jump_to(Asm *a, uint64_t label) { return asm_put(a, ITEM_PUSH_LABEL, 0, label) && op(a, EVM_JUMP); }
static int jump_if(Asm *a, uint64_t label) { return asm_put(a, ITEM_PUSH_LABEL, 0, label) && op(a, EVM_JUMPI); }

static int ops(Asm *a, const uint8_t *codes, size_t n) {
  size_t i;
  int ok = 1;
  for (i = 0; i < n && ok; i++) ok = op(a, codes[i]);
  return ok;
}

static size_t item_size(const Item *it) {
  switch (it->kind) {
    case ITEM_OP:
    case ITEM_LABEL:
      return 1;
    case ITEM_PUSH:
    case ITEM_PUSH_LABEL:
      return 1u + it->width;
  }
  return 0;
}

/* Pass 1, repeated until no width changes. Returns the code size. */
static size_t asm_layout(Asm *a) {
  int changed = 1;
  size_t size = 0;
  size_t i;
  while (changed) {
    changed = 0;
    size = 0;
    for (i = 0; i < a->count; i++) {
      if (a->items[i].kind == ITEM_LABEL) a->label_at[a->items[i].value] = size;
      size += item_size(&a->items[i]);
    }
    for (i = 0; i < a->count; i++) {
      Item *it = &a->items[i];
      uint8_t need = it->kind == ITEM_PUSH_LABEL ? bytes_of(a->label_at[it->value]) : it->width;
      changed |= need > it->width;
      it->width = need > it->width ? need : it->width;
    }
  }
  return size;
}

/* Pass 2: the bytes, big-endian push data. */
static void asm_emit(const Asm *a, uint8_t *out) {
  size_t i;
  size_t at = 0;
  unsigned k;
  for (i = 0; i < a->count; i++) {
    const Item *it = &a->items[i];
    uint64_t v = it->kind == ITEM_PUSH_LABEL ? a->label_at[it->value] : it->value;
    switch (it->kind) {
      case ITEM_OP:
      case ITEM_LABEL:
        out[at++] = it->op;
        break;
      case ITEM_PUSH:
      case ITEM_PUSH_LABEL:
        out[at++] = (uint8_t)(EVM_PUSH1 + it->width - 1u);
        for (k = it->width; k > 0; k--) out[at++] = (uint8_t)(v >> (8u * (k - 1u)));
        break;
    }
  }
}

/* Lays out and writes the code. Sets *code and *size. */
static int asm_finish(Asm *a, uint8_t **code, size_t *size) {
  a->label_at = arena_alloc(a->arena, (a->label_count + 1u) * sizeof(uint64_t));
  if (a->label_at == NULL) return asm_fail(a, "OOM");
  *size = asm_layout(a);
  *code = arena_alloc(a->arena, *size + 1u);
  if (*code == NULL) return asm_fail(a, "OOM");
  asm_emit(a, *code);
  return 1;
}

static uint64_t local_addr(uint32_t local) { return EVM_WORD * (1u + (uint64_t)local); }

static int load_local(Asm *a, uint32_t local) { return push(a, local_addr(local)) && op(a, EVM_MLOAD); }

static int store_local(Asm *a, uint32_t local) { return push(a, local_addr(local)) && op(a, EVM_MSTORE); }

static int gen_trap(Asm *a) { return push(a, 0) && op(a, EVM_DUP1) && op(a, EVM_REVERT); }

/* The stack holds the left operand on top of the right one. */
static int gen_binary(Asm *a, IrOp o) {
  /* (left >= right) * (left - right): a truncated subtraction. */
  static const uint8_t sub[] = {EVM_DUP2, EVM_DUP2,  EVM_LT,  EVM_ISZERO,
                                EVM_SWAP2, EVM_SWAP1, EVM_SUB, EVM_MUL};
  switch (o) {
    case IR_OP_ADD:
      return op(a, EVM_ADD) && push(a, EVM_MASK64) && op(a, EVM_AND);
    case IR_OP_SUB:
      return ops(a, sub, sizeof sub);
    case IR_OP_MUL:
      return op(a, EVM_MUL) && push(a, EVM_MASK64) && op(a, EVM_AND);
    case IR_OP_EQ:
      return op(a, EVM_EQ);
    case IR_OP_LE:
      return op(a, EVM_GT) && op(a, EVM_ISZERO);
    case IR_OP_ADD_CARRY: /* 2^64 - 1 < left + right */
      return op(a, EVM_ADD) && push(a, EVM_MASK64) && op(a, EVM_LT);
    case IR_OP_MUL_CARRY:
      return op(a, EVM_MUL) && push(a, EVM_MASK64) && op(a, EVM_LT);
    case IR_OP_OR:
      return op(a, EVM_OR);
  }
  return 0;
}

static int gen_expr(Asm *a, const IrExpr *e, unsigned depth) {
  if (depth > EVM_DEPTH_MAX) return asm_fail(a, "IR_DEPTH");
  switch (e->kind) {
    case IR_EXPR_CONST:
      return push(a, e->value);
    case IR_EXPR_LOCAL:
      return load_local(a, e->local);
    case IR_EXPR_BINARY:
      return gen_expr(a, e->right, depth + 1u) && gen_expr(a, e->left, depth + 1u) && gen_binary(a, e->op);
    case IR_EXPR_LOAD:
      return gen_expr(a, e->left, depth + 1u) &&
             (e->field == 0 || (push(a, EVM_WORD * (uint64_t)e->field) && op(a, EVM_ADD))) && op(a, EVM_MLOAD);
  }
  return 0;
}

static int gen_block(Asm *a, const IrBlock *b, unsigned depth);

/* The address stays on the stack while the fields are stored. The local is
   set last, as in the Wasm target. */
static int gen_alloc(Asm *a, const IrStmt *st, unsigned depth) {
  size_t k;
  int ok = push(a, 0) && op(a, EVM_MLOAD) && op(a, EVM_DUP1) && push(a, EVM_WORD * (uint64_t)st->field_count) &&
           op(a, EVM_ADD) && push(a, 0) && op(a, EVM_MSTORE);
  for (k = 0; k < st->field_count && ok; k++) {
    ok = gen_expr(a, st->fields[k], depth + 1u) && op(a, EVM_DUP2) &&
         (k == 0 || (push(a, EVM_WORD * (uint64_t)k) && op(a, EVM_ADD))) && op(a, EVM_MSTORE);
  }
  return ok && store_local(a, st->local);
}

/* A tag compare chain. A tag outside 0 to arm_count - 1 reverts. */
static int gen_switch(Asm *a, const IrStmt *st, unsigned depth) {
  uint64_t end = new_label(a);
  uint64_t first = a->label_count;
  size_t k;
  int ok = gen_expr(a, st->expr, depth + 1u);
  a->label_count += st->arm_count;
  for (k = 0; k < st->arm_count && ok; k++) ok = op(a, EVM_DUP1) && push(a, k) && op(a, EVM_EQ) && jump_if(a, first + k);
  ok = ok && gen_trap(a);
  for (k = 0; k < st->arm_count && ok; k++) {
    ok = place(a, first + k) && op(a, EVM_POP) && gen_block(a, &st->arms[k], depth + 1u) && jump_to(a, end);
  }
  return ok && place(a, end);
}

/* REPEAT and WHILE. The counter goes down by 1 after each run of the body,
   as in the Wasm target. */
static int gen_loop(Asm *a, const IrStmt *st, int is_while, unsigned depth) {
  uint64_t top = new_label(a);
  uint64_t end = new_label(a);
  return gen_expr(a, st->expr, depth + 1u) && store_local(a, st->counter) && place(a, top) &&
         load_local(a, st->counter) && op(a, EVM_ISZERO) && jump_if(a, end) &&
         (!is_while || (load_local(a, st->local) && op(a, EVM_ISZERO) && jump_if(a, end))) &&
         gen_block(a, &st->body, depth + 1u) && push(a, 1) && load_local(a, st->counter) && op(a, EVM_SUB) &&
         store_local(a, st->counter) && jump_to(a, top) && place(a, end);
}

static int gen_stmt(Asm *a, const IrStmt *st, unsigned depth) {
  uint64_t otherwise;
  uint64_t end;
  if (depth > EVM_DEPTH_MAX) return asm_fail(a, "IR_DEPTH");
  switch (st->kind) {
    case IR_STMT_SET:
      return gen_expr(a, st->expr, depth + 1u) && store_local(a, st->local);
    case IR_STMT_ALLOC:
      return gen_alloc(a, st, depth);
    case IR_STMT_IF:
      otherwise = new_label(a);
      end = new_label(a);
      return gen_expr(a, st->expr, depth + 1u) && op(a, EVM_ISZERO) && jump_if(a, otherwise) &&
             gen_block(a, &st->body, depth + 1u) && jump_to(a, end) && place(a, otherwise) &&
             gen_block(a, &st->otherwise, depth + 1u) && place(a, end);
    case IR_STMT_SWITCH:
      return gen_switch(a, st, depth);
    case IR_STMT_REPEAT:
      return gen_loop(a, st, 0, depth);
    case IR_STMT_WHILE:
      return gen_loop(a, st, 1, depth);
    case IR_STMT_TRAP:
      return gen_trap(a);
    case IR_STMT_RETURN:
      return gen_expr(a, st->expr, depth + 1u) && push(a, 0) && op(a, EVM_MSTORE) && push(a, EVM_WORD) &&
             push(a, 0) && op(a, EVM_RETURN);
  }
  return 0;
}

static int gen_block(Asm *a, const IrBlock *b, unsigned depth) {
  size_t i;
  int ok = 1;
  for (i = 0; i < b->count && ok; i++) ok = gen_stmt(a, b->items[i], depth);
  return ok;
}

/* An entry: check the calldata size and each argument (a Nat below 2^64, a
   Flag 0 or 1), store the arguments in their locals, reset the free
   pointer, then run the body. */
static int gen_func(Asm *a, const IrFunc *fn, uint64_t label, uint64_t fail) {
  size_t i;
  int ok = place(a, label) && op(a, EVM_POP) && push(a, EVM_ARG_OFFSET + EVM_WORD * (uint64_t)fn->param_count) &&
           op(a, EVM_CALLDATASIZE) && op(a, EVM_LT) && jump_if(a, fail);
  a->def = fn->name;
  for (i = 0; i < fn->param_count && ok; i++) {
    ok = push(a, EVM_ARG_OFFSET + EVM_WORD * (uint64_t)i) && op(a, EVM_CALLDATALOAD) && op(a, EVM_DUP1) &&
         push(a, fn->params[i] == IR_SCALAR_FLAG ? 1u : EVM_MASK64) && op(a, EVM_LT) && jump_if(a, fail) &&
         store_local(a, (uint32_t)i);
  }
  return ok && push(a, local_addr(fn->local_count)) && push(a, 0) && op(a, EVM_MSTORE) && gen_block(a, &fn->body, 0);
}

/* The selector: the first 4 bytes of keccak256("NAME(uint256,...)"). */
static int selector_of(Arena *arena, const IrFunc *fn, uint32_t *selector) {
  size_t len = strlen(fn->name);
  size_t at = len;
  size_t i;
  unsigned char digest[32];
  char *sig = arena_alloc(arena, len + 2u + 8u * fn->param_count);
  if (sig == NULL) return 0;
  memcpy(sig, fn->name, len);
  sig[at++] = '(';
  for (i = 0; i < fn->param_count; i++) {
    memcpy(sig + at, &",uint256"[i == 0], 7u + (i != 0));
    at += 7u + (i != 0);
  }
  sig[at++] = ')';
  keccak256((const unsigned char *)sig, at, digest);
  *selector = (uint32_t)digest[0] << 24 | (uint32_t)digest[1] << 16 | (uint32_t)digest[2] << 8 | digest[3];
  return 1;
}

/* Two entries with one selector cannot both be called. */
static int selectors_differ(Asm *a, const IrProgram *prog, const uint32_t *sel) {
  size_t i;
  size_t j;
  for (i = 0; i < prog->func_count; i++) {
    for (j = i + 1u; j < prog->func_count; j++) {
      if (sel[i] == sel[j]) {
        a->def = prog->funcs[j].name;
        return asm_fail(a, "EVM_SELECTOR");
      }
    }
  }
  return 1;
}

/* The dispatcher: revert on a call value, on calldata shorter than a
   selector and on an unknown selector. Then the entries. */
static int gen_runtime(Asm *a, const IrProgram *prog) {
  uint64_t fail = new_label(a);
  uint64_t first = a->label_count;
  uint32_t *sel = arena_alloc(a->arena, (prog->func_count + 1u) * sizeof(uint32_t));
  size_t k;
  int ok = sel != NULL || asm_fail(a, "OOM");
  for (k = 0; k < prog->func_count && ok; k++) ok = selector_of(a->arena, &prog->funcs[k], &sel[k]) || asm_fail(a, "OOM");
  ok = ok && selectors_differ(a, prog, sel) && op(a, EVM_CALLVALUE) && jump_if(a, fail) && push(a, EVM_ARG_OFFSET) &&
       op(a, EVM_CALLDATASIZE) && op(a, EVM_LT) && jump_if(a, fail) && push(a, 0) && op(a, EVM_CALLDATALOAD) &&
       push(a, 0xe0) && op(a, EVM_SHR);
  a->label_count += prog->func_count;
  for (k = 0; k < prog->func_count && ok; k++) ok = op(a, EVM_DUP1) && push(a, sel[k]) && op(a, EVM_EQ) && jump_if(a, first + k);
  ok = ok && place(a, fail) && gen_trap(a);
  for (k = 0; k < prog->func_count && ok; k++) ok = gen_func(a, &prog->funcs[k], first + k, fail);
  return ok;
}

static int put_hex(FILE *out, const uint8_t *bytes, size_t size) {
  size_t i;
  for (i = 0; i < size; i++) fprintf(out, "%02x", bytes[i]);
  return !ferror(out);
}

/* Creation code: PUSH size, DUP1, PUSH start, PUSH1 0, CODECOPY, PUSH1 0,
   RETURN, then the runtime. start = the creation size = width + 10. */
static int put_creation(FILE *out, const uint8_t *code, size_t size) {
  uint8_t head[20];
  uint8_t width = bytes_of(size);
  size_t n = 0;
  unsigned k;
  head[n++] = (uint8_t)(EVM_PUSH1 + width - 1u);
  for (k = width; k > 0; k--) head[n++] = (uint8_t)((uint64_t)size >> (8u * (k - 1u)));
  head[n++] = EVM_DUP1;
  head[n++] = EVM_PUSH1;
  head[n++] = (uint8_t)(width + 10u);
  head[n++] = EVM_PUSH1;
  head[n++] = 0;
  head[n++] = EVM_CODECOPY;
  head[n++] = EVM_PUSH1;
  head[n++] = 0;
  head[n++] = EVM_RETURN;
  return put_hex(out, head, n) && put_hex(out, code, size);
}

int target_abi_line(const IrFunc *fn, FILE *out, FILE *err) {
  Arena arena;
  uint32_t selector = 0;
  int ok;
  arena_init(&arena, EVM_ARENA_LIMIT);
  ok = selector_of(&arena, fn, &selector);
  arena_release(&arena);
  if (!ok) {
    fprintf(err, "langc: OOM: %s: no memory for the selector\n", fn->name);
    return 0;
  }
  fprintf(out, "%s %08x\n", fn->name, (unsigned)selector);
  return 1;
}

int target_write(const IrProgram *prog, TargetPart part, FILE *out, FILE *err) {
  Arena arena;
  Asm a;
  uint8_t *code = NULL;
  size_t size = 0;
  int ok;
  int written;
  arena_init(&arena, EVM_ARENA_LIMIT);
  ok = asm_init(&a, &arena) && gen_runtime(&a, prog) && asm_finish(&a, &code, &size);
  if (!ok) fprintf(err, "langc: %s: %s: the EVM code was not made\n", a.code, a.def != NULL ? a.def : "-");
  written = ok && (part == TARGET_PART_RUNTIME ? put_hex(out, code, size) : put_creation(out, code, size)) &&
            fprintf(out, "\n") > 0;
  if (ok && !written) fprintf(err, "langc: IO: -: the EVM hex was not written\n");
  arena_release(&arena);
  return written;
}
