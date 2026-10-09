/* The lowering of a checked program to EVM code (slice K4a). `langc build`
   writes the creation code: it stores each nonzero word of the `init` state
   in the storage layout below, then returns the runtime code. In K4a the
   runtime code reverts each call; slice K4b adds the dispatch. The evaluator
   is the reference: `init` is a closed term, so the build stores its normal
   form.

   The storage layout: field i of the state is at slot i. A word field (Nat,
   Flag, U256, Addr) is one word in its slot. A List of words has its length
   at slot s and element j at keccak256(s) + j. A Map of words has the value
   at key k at keccak256(k . s), with k and s each as a 32-byte word. Other
   fields give REFUSE_LOWER. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "evm.h"
#include "front/check.h"
#include "ir.h"
#include "keccak.h"
#include "lower.h"

typedef unsigned char Word[32];

/* The storage form of a state field. */
typedef enum { FORM_WORD, FORM_LIST, FORM_MAP, FORM_NONE } Form;

static int is_word_type(const Value *t) {
  return val_is(t, OP_NAT) || val_is(t, OP_FLAG) || val_is(t, OP_U256) || val_is(t, OP_ADDR);
}

static Form form_of(const Value *t) {
  if (is_word_type(t)) return FORM_WORD;
  if (val_is(t, OP_LIST) && t->argc == 1 && is_word_type(t->args[0])) return FORM_LIST;
  if (val_is(t, OP_KMAP) && t->argc == 2 && is_word_type(t->args[0]) && is_word_type(t->args[1])) return FORM_MAP;
  return FORM_NONE;
}

/* The name of a field type that K4a does not lower, for REFUSE_LOWER. */
static const char *form_name(const Value *t) {
  return val_is(t, OP_OPTION) ? "an Option"
         : val_is(t, OP_PROD) ? "a Prod"
         : val_is(t, OP_LIST) ? "a List of a type that is not a word"
         : val_is(t, OP_KMAP) ? "a Map with a key or a value that is not a word"
         : "not a word, a List of words or a Map of words";
}

/* Big-endian words: Nat as an unsigned 64-bit value, U256 and Addr from the
   limbs (limb 0 is the low limb), Flag as 0 or 1. */
static void word_u64(Word w, uint64_t n) {
  memset(w, 0, sizeof(Word));
  for (int i = 0; i < 8; i++) w[31 - i] = (unsigned char)(n >> (8 * i));
}

static void word_u256(Word w, const U256 *x) {
  for (int i = 0; i < 32; i++) w[31 - i] = (unsigned char)(x->limb[i / 8] >> (8 * (i % 8)));
}

/* Returns 1 and the word of V, or 0 when V is not a closed word value. */
static int word_of(const Value *v, Word w) {
  if (v != NULL && v->kind == VAL_NAT) {
    word_u64(w, v->nat);
    return 1;
  }
  if (v != NULL && v->kind == VAL_WORD && v->word != NULL) {
    word_u256(w, v->word);
    return 1;
  }
  if (v != NULL && (val_is(v, OP_FLAG_YES) || val_is(v, OP_FLAG_NO))) {
    word_u64(w, (uint64_t)val_is(v, OP_FLAG_YES));
    return 1;
  }
  return 0;
}

/* OUT = keccak256(A . B), or keccak256(A) when B is NULL. */
static void word_hash(Word out, const Word a, const Word b) {
  unsigned char data[64];
  memcpy(data, a, 32);
  if (b != NULL) memcpy(data + 32, b, 32);
  keccak256(data, b != NULL ? 64u : 32u, out);
}

/* OUT = BASE + N modulo 2^256. */
static void word_add(Word out, const Word base, uint64_t n) {
  unsigned carry = 0;
  for (int i = 31; i >= 0; i--) {
    unsigned sum = base[i] + (unsigned)(n & 0xffu) + carry;
    out[i] = (unsigned char)sum;
    carry = sum >> 8;
    n >>= 8;
  }
}

/* The (slot, value) word pairs of the start state. AT is NULL in the first
   walk, which only counts the pairs. */
typedef struct {
  unsigned char (*at)[64];
  size_t count;
} Pairs;

static void pair_add(Pairs *p, const Word slot, const Word value) {
  if (p->at != NULL) {
    memcpy(p->at[p->count], slot, 32);
    memcpy(p->at[p->count] + 32, value, 32);
  }
  p->count++;
}

/* Adds the pairs of field I of FORM with the value V. Returns 1, or 0 when V
   is not a closed value of FORM. */
static int lower_field(Pairs *p, uint64_t i, Form form, const Value *v) {
  Word slot, value, at, key;
  word_u64(slot, i);
  if (form == FORM_WORD) {
    if (!word_of(v, value)) return 0;
    pair_add(p, slot, value);
    return 1;
  }
  if (form == FORM_LIST) {
    Word base;
    uint64_t n = 0;
    word_hash(base, slot, NULL);
    for (; val_is(v, OP_CONS) && v->argc == 2; v = v->args[1], n++) {
      if (!word_of(v->args[0], value)) return 0;
      word_add(at, base, n);
      pair_add(p, at, value);
    }
    if (!val_is(v, OP_NIL)) return 0;
    word_u64(value, n);
    pair_add(p, slot, value);
    return 1;
  }
  if (!val_is(v, OP_KMAP_OF) || v->argc % 2u != 0) return 0;
  for (uint32_t j = 0; j < v->argc; j += 2u) {
    if (!word_of(v->args[j], key) || !word_of(v->args[j + 1u], value)) return 0;
    word_hash(at, key, slot);
    pair_add(p, at, value);
  }
  return 1;
}

static const Value *field_type(Machine *m, const CtorInfo *ci, uint32_t i) {
  return eval_core(m, NULL, ci->fields[i].type);
}

/* Walks the fields of STATE into P. Returns 1, or 0 after a diagnostic. */
static int lower_state(Machine *m, const CtorInfo *ci, const Value *state, Pairs *p) {
  for (uint32_t i = 0; i < ci->field_count; i++)
    if (!lower_field(p, i, form_of(field_type(m, ci, i)), state->args[i]))
      return diag_fail(m->diag, "REFUSE_LOWER", ci->fields[i].name,
                       "the init value of the field is not a closed value (a trap)");
  return 1;
}

static int build_status(Machine *m, EvmBuild result) {
  switch (result) {
    case EVM_BUILD_OK: return 0;
    case EVM_BUILD_SIZE:
      return diag_fail(m->diag, "EVM_SIZE", NULL, "the contract code is longer than the EIP-3860 or EIP-170 limit") + 1;
    case EVM_BUILD_OOM: return diag_fail(m->diag, "OOM", NULL, "out of memory") + 1;
    case EVM_BUILD_WRITE: return diag_fail(m->diag, "IO_WRITE", NULL, "the code buffer write failed") + 2;
    case EVM_BUILD_IR:
      return diag_fail(m->diag, "REFUSE_LOWER", NULL, "an entry has an IR form that the EVM back end does not lower") + 1;
    case EVM_BUILD_SIGNATURE:
      return diag_fail(m->diag, "EVM_SIGNATURE", NULL, "the signature of an entry is longer than 255 bytes") + 1;
    case EVM_BUILD_SELECTOR:
      return diag_fail(m->diag, "EVM_SELECTOR", NULL, "two entries have the same four-byte ABI selector") + 1;
  }
  return 1;
}

/* The residual of an entry (slice K4b, C-K4-2): the normal form of the
   entry body with the Env at level 0, the state at level 1 and the arguments
   at levels 2, 3, ... The walk is the walk of role_of in check.c, with the
   Env family found by name. */
typedef struct {
  uint32_t def;
  uint32_t arg_count;
  const Value *arg_types[ENTRY_PARAMS_MAX];
  const Value *body; /* Option (Prod State (List Out)), open in the levels */
} Residual;

static uint32_t family_named(const Machine *m, const char *name) {
  uint32_t found = m->family_count;
  for (uint32_t i = 0; i < m->family_count; i++)
    if (strcmp(m->families[i].name, name) == 0) found = i;
  return found;
}

static int is_family_value(const Value *t, uint32_t family) {
  return t != NULL && val_is(t, OP_FAMILY) && t->inst == family;
}

/* 1 when def I is an entry; then R holds its residual. 0 for a helper or a
   view (views are K4c, C-K4b-4). -1 after a diagnostic. */
static int residualize(Machine *m, uint32_t env, uint32_t out, uint32_t i, Residual *r) {
  const Value *t = m->defs[i].type;
  memset(r, 0, sizeof *r);
  r->def = i;
  m->def = m->defs[i].name;
  m->fuel = EVAL_FUEL_STEPS;
  if (t == NULL || t->kind != VAL_PI || !is_family_value(t->dom, env)) return 0;
  t = closure_apply(m, t, val_var(m, 0));
  if (t == NULL) return -1;
  if (t->kind != VAL_PI || !is_family_value(t->dom, m->state_family)) return 0;
  t = closure_apply(m, t, val_var(m, 1));
  if (t == NULL) return -1;
  while (t->kind == VAL_PI && r->arg_count < ENTRY_PARAMS_MAX && is_word_type(t->dom)) {
    r->arg_types[r->arg_count] = t->dom;
    t = closure_apply(m, t, val_var(m, r->arg_count + 2u));
    if (t == NULL) return -1;
    r->arg_count++;
  }
  if (!val_is(t, OP_OPTION) || !val_is(t->args[0], OP_PROD)) return 0;
  const Value *p = t->args[0];
  if (!is_family_value(p->args[0], m->state_family) || !val_is(p->args[1], OP_LIST)
      || !is_family_value(p->args[1]->args[0], out)) return 0;
  m->fuel = EVAL_FUEL_STEPS;
  const Value *v = def_value(m, i);
  for (uint32_t k = 0; v != NULL && k < r->arg_count + 2u; k++) v = apply_value(m, v, val_var(m, k));
  r->body = v;
  return v == NULL ? -1 : 1;
}

/* The lowering of the residuals to IR (slice K4b1, C-K4-2, C-K4b-7). An
   entry body is a tree of flagIf over none, a trap or some (pair STATE OUT).
   STATE gives one SSTORE for each field that is not its own old value, after
   the values are in locals (thus a store does not change a later load). OUT
   has no external effects until K4c (C-K4b-5), but its words are strict so
   that a trap still reverts the call. Other residuals give REFUSE_LOWER. */
typedef struct {
  const IrStmt **items;
  size_t count;
  size_t cap;
} Stmts;

typedef struct {
  Machine *m;
  const CtorInfo *state;
  const CtorInfo *env;
  uint32_t locals;
  const char *entry;
} Low;

static void *low_alloc(Low *l, size_t size) {
  void *p = arena_alloc(l->m->arena, size);
  if (p == NULL) diag_fail(l->m->diag, "OOM", NULL, "out of memory");
  else memset(p, 0, size);
  return p;
}

static int push(Low *l, Stmts *b, const IrStmt *s) {
  if (s == NULL) return 0;
  if (b->count == b->cap) {
    size_t cap = b->cap == 0 ? 8u : b->cap * 2u;
    const IrStmt **items = low_alloc(l, cap * sizeof *items);
    if (items == NULL) return 0;
    if (b->count > 0) memcpy(items, b->items, b->count * sizeof *items);
    b->items = items;
    b->cap = cap;
  }
  b->items[b->count++] = s;
  return 1;
}

static IrBlock block_of(const Stmts *b) {
  IrBlock out = {(const IrStmt *const *)b->items, b->count};
  return out;
}

static IrStmt *new_stmt(Low *l, IrStmtKind kind) {
  IrStmt *s = low_alloc(l, sizeof *s);
  if (s != NULL) s->kind = kind;
  return s;
}

static IrExpr *new_expr(Low *l, IrExprKind kind) {
  IrExpr *e = low_alloc(l, sizeof *e);
  if (e != NULL) e->kind = kind;
  return e;
}

static const IrExpr *ir_const(Low *l, uint64_t n) {
  IrExpr *e = new_expr(l, IR_EXPR_CONST);
  if (e != NULL) e->value = n;
  return e;
}

static const IrExpr *ir_local(Low *l, uint32_t local) {
  IrExpr *e = new_expr(l, IR_EXPR_LOCAL);
  if (e != NULL) e->local = local;
  return e;
}

static const IrExpr *ir_binary(Low *l, IrOp op, const IrExpr *a, const IrExpr *b) {
  IrExpr *e = a != NULL && b != NULL ? new_expr(l, IR_EXPR_BINARY) : NULL;
  if (e != NULL) {
    e->op = op;
    e->left = a;
    e->right = b;
  }
  return e;
}

static const IrExpr *ir_sload(Low *l, const IrExpr *slot) {
  IrExpr *e = slot != NULL ? new_expr(l, IR_EXPR_SLOAD) : NULL;
  if (e != NULL) e->left = slot;
  return e;
}

/* B gets local := E; the result is the local, or NULL after a diagnostic. */
static const IrExpr *ir_set(Low *l, Stmts *b, const IrExpr *e) {
  IrStmt *s = e != NULL ? new_stmt(l, IR_STMT_SET) : NULL;
  if (s == NULL) return NULL;
  s->local = l->locals++;
  s->expr = e;
  return push(l, b, s) ? ir_local(l, s->local) : NULL;
}

static int is_op(const Value *v, Op op) {
  return v != NULL && (v->kind == VAL_OP || v->kind == VAL_STUCK) && v->op == op;
}

/* 1 when V is the projection of field FIELD of the variable at LEVEL. */
static int field_of(const Value *v, uint32_t level, uint32_t *field) {
  if (!is_op(v, OP_PROJ) || v->argc < 1 || v->args[0]->kind != VAL_VAR || v->args[0]->nat != level) return 0;
  *field = v->field;
  return 1;
}

static const char *value_name(const Low *l, const Value *v) {
  if (v == NULL) return "no value";
  switch (v->kind) {
    case VAL_OP:
    case VAL_STUCK: return op_name(l->m, v->op, v->inst, v->field);
    case VAL_LAM: return "a closure";
    case VAL_UNIV: return "a universe";
    case VAL_PI: return "a Pi type";
    case VAL_SIGMA: return "a Sigma type";
    case VAL_APP: return "an application of a variable (recursion that is not a List fold)";
    case VAL_VAR: return "the Env or the state as one value";
    case VAL_NAT:
    case VAL_WORD:
    case VAL_TRAP: return "a word";
  }
  return "an unknown value";
}

static int refuse(Low *l, const Value *v, const char *where) {
  return diag_fail(l->m->diag, "REFUSE_LOWER", l->entry, "K4b does not lower %s %s", value_name(l, v), where);
}

static const struct {
  Op op;
  IrOp ir;
} BINARY_OPS[] = {
    {OP_NAT_ADD, IR_OP_ADD64},     {OP_NAT_SUB, IR_OP_SUB},     {OP_NAT_MUL, IR_OP_MUL64},
    {OP_NAT_EQ, IR_OP_EQ},         {OP_NAT_LE, IR_OP_LE},       {OP_U256_ADD, IR_OP_ADD_TRAP},
    {OP_U256_SUB, IR_OP_SUB},      {OP_U256_MUL, IR_OP_MUL_TRAP}, {OP_U256_DIV, IR_OP_DIV},
    {OP_U256_LE, IR_OP_LE},        {OP_U256_EQ, IR_OP_EQ},      {OP_U256_MIN, IR_OP_MIN},
    {OP_ADDR_EQ, IR_OP_EQ},
};

static int form_at(Low *l, uint32_t field) {
  return field < l->state->field_count ? (int)form_of(field_type(l->m, l->state, field)) : (int)FORM_NONE;
}

static const IrExpr *lower_expr(Low *l, Stmts *b, const Value *v);

/* flagIf at a word position: an IF that sets one local in each branch. */
static const IrExpr *lower_if_value(Low *l, Stmts *b, const Value *v) {
  const IrExpr *cond = lower_expr(l, b, v->args[0]);
  uint32_t local = l->locals++;
  Stmts arms[2] = {{NULL, 0, 0}, {NULL, 0, 0}};
  IrStmt *s = cond != NULL ? new_stmt(l, IR_STMT_IF) : NULL;
  for (int k = 0; s != NULL && k < 2; k++) {
    const IrExpr *e = lower_expr(l, &arms[k], v->args[1 + k]);
    IrStmt *set = e != NULL ? new_stmt(l, IR_STMT_SET) : NULL;
    if (set != NULL) {
      set->local = local;
      set->expr = e;
    }
    if (!push(l, &arms[k], set)) s = NULL;
  }
  if (s == NULL) return NULL;
  s->expr = cond;
  s->body = block_of(&arms[0]);
  s->otherwise = block_of(&arms[1]);
  return push(l, b, s) ? ir_local(l, local) : NULL;
}

static const IrExpr *lower_expr(Low *l, Stmts *b, const Value *v) {
  uint32_t f;
  if (v != NULL && v->kind == VAL_TRAP) return push(l, b, new_stmt(l, IR_STMT_TRAP)) ? ir_const(l, 0) : NULL;
  if (v != NULL && v->kind == VAL_NAT) return ir_const(l, v->nat);
  if (val_is(v, OP_FLAG_YES) || val_is(v, OP_FLAG_NO)) return ir_const(l, (uint64_t)val_is(v, OP_FLAG_YES));
  if (v != NULL && v->kind == VAL_WORD) {
    IrExpr *e = new_expr(l, IR_EXPR_WORD);
    unsigned char *w = e != NULL ? low_alloc(l, sizeof(Word)) : NULL;
    if (w == NULL || !word_of(v, w)) return NULL;
    e->word = w;
    return e;
  }
  if (v != NULL && v->kind == VAL_VAR && v->nat >= 2u) return ir_local(l, (uint32_t)v->nat - 2u);
  if (field_of(v, 1u, &f) && form_at(l, f) == FORM_WORD) return ir_sload(l, ir_const(l, f));
  if (field_of(v, 0u, &f) && l->env != NULL && f < l->env->field_count) {
    const char *name = l->env->fields[f].name;
    IrExpr *e = strcmp(name, "now") == 0 || strcmp(name, "caller") == 0 ? new_expr(l, IR_EXPR_ENV) : NULL;
    if (e != NULL) e->field = strcmp(name, "now") == 0 ? IR_ENV_NOW : IR_ENV_CALLER;
    return e != NULL ? e : (refuse(l, v, "(an Env field that is not now or caller)"), NULL);
  }
  if (is_op(v, OP_TO_U256) && v->argc == 1) return lower_expr(l, b, v->args[0]);
  if (is_op(v, OP_KMAP_GET) && v->argc == 3 && field_of(v->args[1], 1u, &f) && form_at(l, f) == FORM_MAP) {
    const IrExpr *key = lower_expr(l, b, v->args[2]);
    return ir_sload(l, ir_binary(l, IR_OP_KECCAK, key, key != NULL ? ir_const(l, f) : NULL));
  }
  if (is_op(v, OP_FLAG_IF) && v->argc == 3) return lower_if_value(l, b, v);
  for (size_t k = 0; v != NULL && v->argc == 2 && k < sizeof BINARY_OPS / sizeof BINARY_OPS[0]; k++)
    if (is_op(v, BINARY_OPS[k].op)) {
      const IrExpr *x = lower_expr(l, b, v->args[0]);
      const IrExpr *y = x != NULL ? lower_expr(l, b, v->args[1]) : NULL;
      return ir_binary(l, BINARY_OPS[k].ir, x, y);
    }
  return refuse(l, v, "at a word position"), NULL;
}

static int push_store(Low *l, Stmts *stores, const IrExpr *slot, const IrExpr *value) {
  IrStmt *s = slot != NULL && value != NULL ? new_stmt(l, IR_STMT_SSTORE) : NULL;
  if (s == NULL) return 0;
  s->expr = slot;
  s->value = value;
  return push(l, stores, s);
}

/* A chain of mapSet over the old map FIELD: the inner writes come first. */
static int lower_map_writes(Low *l, Stmts *b, Stmts *stores, const Value *v, uint32_t field) {
  uint32_t f;
  if (field_of(v, 1u, &f) && f == field) return 1;
  if (!is_op(v, OP_KMAP_SET) || v->argc != 4) return refuse(l, v, "as a Map field value (want mapSet over the old field)");
  if (!lower_map_writes(l, b, stores, v->args[1], field)) return 0;
  const IrExpr *key = ir_set(l, b, lower_expr(l, b, v->args[2]));
  const IrExpr *value = key != NULL ? ir_set(l, b, lower_expr(l, b, v->args[3])) : NULL;
  return value != NULL && push_store(l, stores, ir_binary(l, IR_OP_KECCAK, key, ir_const(l, field)), value);
}

static int lower_state_write(Low *l, Stmts *b, const Value *st) {
  Stmts stores = {NULL, 0, 0};
  uint32_t f;
  if (st != NULL && st->kind == VAL_VAR && st->nat == 1u) return push(l, b, new_stmt(l, IR_STMT_STOP));
  if (!is_op(st, OP_CTOR) || st->argc != l->state->field_count) return refuse(l, st, "as the new state");
  for (uint32_t i = 0; i < st->argc; i++) {
    const Value *v = st->args[i];
    int form = form_at(l, i);
    if (field_of(v, 1u, &f) && f == i) continue;
    if (form == FORM_WORD) {
      const IrExpr *value = ir_set(l, b, lower_expr(l, b, v));
      if (value == NULL || !push_store(l, &stores, ir_const(l, i), value)) return 0;
    } else if (form == FORM_MAP) {
      if (!lower_map_writes(l, b, &stores, v, i)) return 0;
    } else {
      return diag_fail(l->m->diag, "REFUSE_LOWER", l->entry, "K4b does not lower a write of the List field %s",
                       l->state->fields[i].name);
    }
  }
  for (size_t k = 0; k < stores.count; k++)
    if (!push(l, b, stores.items[k])) return 0;
  return push(l, b, new_stmt(l, IR_STMT_STOP));
}

/* Force the words of ignored outputs before any state store. Keep flagIf
   branches lazy, including at List positions and within emit fields. */
static int lower_out_words(Low *l, Stmts *b, const Value *v) {
  uint32_t field;
  if (val_is(v, OP_NIL)) return 1;
  if (is_op(v, OP_FLAG_IF) && v->argc == 3) {
    const IrExpr *cond = lower_expr(l, b, v->args[0]);
    Stmts yes = {NULL, 0, 0};
    Stmts no = {NULL, 0, 0};
    IrStmt *s = cond != NULL && lower_out_words(l, &yes, v->args[1]) && lower_out_words(l, &no, v->args[2])
                    ? new_stmt(l, IR_STMT_IF) : NULL;
    if (s == NULL) return 0;
    s->expr = cond;
    s->body = block_of(&yes);
    s->otherwise = block_of(&no);
    return push(l, b, s);
  }
  if (is_op(v, OP_CONS) || is_op(v, OP_CTOR)) {
    for (uint32_t i = 0; i < v->argc; i++)
      if (!lower_out_words(l, b, v->args[i])) return 0;
    return 1;
  }
  /* A stored List contains words that were already forced when stored. */
  if (field_of(v, 1u, &field) && form_at(l, field) == FORM_LIST) return 1;
  return ir_set(l, b, lower_expr(l, b, v)) != NULL;
}

static int lower_result(Low *l, Stmts *b, const Value *v) {
  if (v != NULL && v->kind == VAL_TRAP) return push(l, b, new_stmt(l, IR_STMT_TRAP));
  if (is_op(v, OP_NONE)) return push(l, b, new_stmt(l, IR_STMT_REVERT));
  if (is_op(v, OP_FLAG_IF) && v->argc == 3) {
    const IrExpr *cond = lower_expr(l, b, v->args[0]);
    Stmts yes = {NULL, 0, 0};
    Stmts no = {NULL, 0, 0};
    IrStmt *s = cond != NULL && lower_result(l, &yes, v->args[1]) && lower_result(l, &no, v->args[2])
                    ? new_stmt(l, IR_STMT_IF) : NULL;
    if (s == NULL) return 0;
    s->expr = cond;
    s->body = block_of(&yes);
    s->otherwise = block_of(&no);
    return push(l, b, s);
  }
  if (is_op(v, OP_SOME) && v->argc >= 1 && is_op(v->args[v->argc - 1], OP_PAIR) && v->args[v->argc - 1]->argc == 2)
    return lower_out_words(l, b, v->args[v->argc - 1]->args[1])
           && lower_state_write(l, b, v->args[v->argc - 1]->args[0]);
  return refuse(l, v, "as an entry result");
}

static IrScalar scalar_of(const Value *t) {
  return val_is(t, OP_FLAG) ? IR_SCALAR_FLAG
         : val_is(t, OP_U256) ? IR_SCALAR_U256
         : val_is(t, OP_ADDR) ? IR_SCALAR_ADDR
         : IR_SCALAR_NAT;
}

/* PROGRAM gets one IrFunc for each entry. Returns 1, or 0 after a diagnostic. */
static int lower_entries(Machine *m, const CtorInfo *ci, IrProgram *program) {
  uint32_t env = family_named(m, "Env");
  uint32_t out = family_named(m, "Out");
  IrFunc *funcs = m->def_count > 0 ? arena_alloc(m->arena, m->def_count * sizeof *funcs) : NULL;
  size_t n = 0;
  if (m->def_count > 0 && funcs == NULL) return diag_fail(m->diag, "OOM", NULL, "out of memory");
  for (uint32_t i = 0; i < m->def_count; i++) {
    Residual r;
    int role = residualize(m, env, out, i, &r);
    if (role < 0) return 0;
    if (role == 0) continue;
    Low l = {m, ci, env < m->family_count ? &m->ctors[m->families[env].first_ctor] : NULL, r.arg_count, m->defs[i].name};
    Stmts b = {NULL, 0, 0};
    IrScalar *params = r.arg_count > 0 ? low_alloc(&l, r.arg_count * sizeof *params) : NULL;
    if ((r.arg_count > 0 && params == NULL) || !lower_result(&l, &b, r.body)) return 0;
    for (uint32_t k = 0; k < r.arg_count; k++) params[k] = scalar_of(r.arg_types[k]);
    IrFunc func = {m->defs[i].name, params, r.arg_count, IR_SCALAR_NAT, l.locals, block_of(&b)};
    funcs[n++] = func;
  }
  program->funcs = funcs;
  program->func_count = n;
  return 1;
}

int lower_build(Machine *m, TargetPart part, FILE *out) {
  uint32_t init = m->def_count;
  for (uint32_t i = 0; i < m->def_count; i++)
    if (strcmp(m->defs[i].name, "init") == 0) init = i;
  m->def = "init";
  m->fuel = EVAL_FUEL_STEPS;
  if (!m->has_state || init == m->def_count)
    return diag_fail(m->diag, "REFUSE_LOWER", NULL, "a build needs a state and def init : State") + 1;
  const Value *type = m->defs[init].type;
  if (type != NULL && type->kind == VAL_PI)
    return diag_fail(m->diag, "REFUSE_LOWER", "init", "init has arguments; K4a does not read constructor arguments") + 1;
  if (!val_is(type, OP_FAMILY) || type->inst != m->state_family)
    return diag_fail(m->diag, "REFUSE_LOWER", "init", "the type of init is not the state") + 1;
  const CtorInfo *ci = &m->ctors[m->families[m->state_family].first_ctor];
  for (uint32_t i = 0; i < ci->field_count; i++) {
    const Value *t = field_type(m, ci, i);
    if (t == NULL) return 1;
    if (form_of(t) == FORM_NONE)
      return diag_fail(m->diag, "REFUSE_LOWER", ci->fields[i].name,
                       "K4a stores only words (Nat, Flag, U256, Addr), Lists of words and Maps of words; the field is %s",
                       form_name(t)) + 1;
  }
  IrProgram program = {NULL, 0};
  if (!lower_entries(m, ci, &program)) return 1;
  m->def = "init";
  m->fuel = EVAL_FUEL_STEPS;
  const Value *state = def_value(m, init);
  if (state == NULL) return 1;
  if (!val_is(state, OP_CTOR) || state->argc != ci->field_count)
    return diag_fail(m->diag, "REFUSE_LOWER", "init", "the value of init is not a closed state") + 1;
  Pairs pairs = {NULL, 0};
  if (!lower_state(m, ci, state, &pairs)) return 1;
  pairs.at = pairs.count > 0 ? arena_alloc(m->arena, pairs.count * 64u) : NULL;
  if (pairs.count > 0 && pairs.at == NULL) return diag_fail(m->diag, "OOM", NULL, "out of memory") + 1;
  pairs.count = 0;
  if (!lower_state(m, ci, state, &pairs)) return 1;
  return build_status(m, evm_build(&program, (const unsigned char (*)[64])pairs.at, pairs.count, part, out));
}
