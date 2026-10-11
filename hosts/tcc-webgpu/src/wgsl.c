/* The WGSL target. The tcc-webgpu kit only. See wgsl.h. */
#include "wgsl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "front/base.h"

/* WebGPU gives a shader stage 8 storage buffers by default: the output and
   at most 7 parameters. */
#define WGSL_PARAMS_MAX 7u
/* The bound variables of one entry: i, j and 1 or 2 for each loop. */
#define WGSL_VARS_MAX 64u
#define WGSL_NAME_MAX 64u
#define WGSL_WORKGROUP "64"
#define WGSL_WORKGROUP_N 64u /* WGSL_WORKGROUP as a number */
/* The evaluator keeps a tile at or below this many cells (eval.c). */
#define WGSL_CELLS_MAX 1048576u
/* WGSL indexes and Nat terms are u32 (D9). */
#define WGSL_U32_LIMIT ((uint64_t)1 << 32)

/* The WGSL names that an entry cannot have: the keywords, the reserved
   words, and the predeclared names that the shader uses. */
static const char RESERVED[] =
  " alias break case const const_assert continue continuing default diagnostic discard else enable false fn for if"
  " let loop override requires return struct switch true var while"
  " array arrayLength atomic bool f16 f32 i32 u32 vec2 vec3 vec4 mat2x2 mat2x3 mat2x4 mat3x2 mat3x3 mat3x4 mat4x2"
  " mat4x3 mat4x4 ptr sampler sampler_comparison select"
  " NULL Self abstract active alignas alignof as asm asm_fragment async attribute auto await become binding_array"
  " cast catch class co_await co_return co_yield coherent column_major common compile compile_fragment concept"
  " const_cast consteval constexpr constinit crate debugger decltype delete demote demote_to_helper do"
  " dynamic_cast enum explicit export extends extern external fallthrough filter final finally friend from"
  " fxgroup get goto groupshared highp impl implements import inline instanceof interface layout lowp macro"
  " macro_rules match mediump meta mod module move mut mutable namespace new nil noexcept noinline"
  " nointerpolation noperspective null nullptr of operator package packoffset partition pass patch"
  " pixelfragment precise precision premerge priv protected pub public readonly ref regardless register"
  " reinterpret_cast require resource restrict self set shared sizeof smooth snorm static static_assert"
  " static_cast std subroutine super target template this thread_local throw trait try type typedef typeid"
  " typename typeof union unless unorm unsafe unsized use using varying virtual volatile wgsl where with"
  " writeonly yield ";

static const char HEADER[] =
  "// langc build --wgsl: one compute shader for each entry of the program\n"
  "// (an entry maps tiles and U32 values to a tile). A buffer holds the cells\n"
  "// of a tile in row-major order as u32 values (a Flag cell is 0u or 1u).\n"
  "// Invocation x of an entry writes the cell x of the result.\n";

typedef enum {
  TY_U32, /* a Nat or a U32 */
  TY_BOOL /* a Flag */
} Ty;

typedef struct {
  char *bytes;
  size_t len;
  size_t cap;
  int failed;
} Buf;

/* A statement list: the function body or a loop body. */
typedef struct {
  Buf text;
  unsigned indent;
} Block;

typedef struct {
  int tile; /* 0: a U32 value */
  uint64_t rows;
  uint64_t cols;
  Ty elem;
} Shape;

/* An interval [lo, hi] of Nat values (D9). */
typedef struct {
  uint64_t lo;
  uint64_t hi;
} Range;

typedef struct {
  Machine *m;
  const char *name;
  Shape params[WGSL_PARAMS_MAX];
  int used[WGSL_PARAMS_MAX];
  uint32_t param_count;
  Shape result;
  uint32_t level; /* the next free level; levels below param_count are the parameters */
  uint32_t locals;
  char names[WGSL_VARS_MAX][16];
  Ty types[WGSL_VARS_MAX];
  Range ranges[WGSL_VARS_MAX];
} Kernel;

static const char *const BINARY[] = {
  [OP_NAT_ADD] = "+", [OP_NAT_MUL] = "*", [OP_NAT_DIV] = "/", [OP_NAT_MOD] = "%",
  [OP_NAT_EQ] = "==", [OP_NAT_LE] = "<=",
  [OP_U32_ADD] = "+", [OP_U32_SUB] = "-", [OP_U32_MUL] = "*",
};

static void put_n(Buf *b, const char *s, size_t n) {
  size_t cap = b->cap == 0 ? 256u : b->cap;
  char *grown;
  while (!b->failed && cap < b->len + n + 1u)
    cap *= 2u;
  grown = b->failed || cap == b->cap ? b->bytes : realloc(b->bytes, cap);
  if (grown == NULL)
    b->failed = 1;
  if (b->failed)
    return;
  b->bytes = grown;
  b->cap = cap;
  memcpy(b->bytes + b->len, s, n);
  b->len += n;
  b->bytes[b->len] = '\0';
}

static void put(Buf *b, const char *s) {
  put_n(b, s, strlen(s));
}

static void put_u64(Buf *b, uint64_t n) {
  char t[24];
  int w = snprintf(t, sizeof t, "%llu", (unsigned long long)n);
  put_n(b, t, w > 0 ? (size_t)w : 0u);
}

static void put_buf(Buf *b, const Buf *s) {
  put_n(b, s->bytes == NULL ? "" : s->bytes, s->len);
}

static void indent(Block *b) {
  unsigned i;
  for (i = 0; i < b->indent; i++)
    put(&b->text, "  ");
}

/* A name or a literal: it needs no local. */
static int is_atom(const Buf *b) {
  return b->bytes == NULL || strpbrk(b->bytes, " (") == NULL;
}

static void free_bufs(Buf *a, Buf *b, Buf *c) {
  free(a->bytes);
  free(b->bytes);
  free(c->bytes);
}

static int refuse(Kernel *k, const char *what) {
  return diag_fail(k->m->diag, "WGSL_FORM", k->m->def, "%s", what);
}

static void param_name(const Kernel *k, uint32_t p, Buf *out) {
  put(out, k->name);
  put(out, "_in");
  put_u64(out, p);
}

static const Value *bind(Kernel *k, const char *name, Ty ty, Range r) {
  uint32_t at = k->level - k->param_count;
  if (at >= WGSL_VARS_MAX) {
    refuse(k, "an entry with more than 64 bound variables has no WGSL form");
    return NULL;
  }
  snprintf(k->names[at], sizeof k->names[at], "%s", name);
  k->types[at] = ty;
  k->ranges[at] = r;
  return val_var(k->m, k->level++);
}

static int expr(Kernel *k, Block *b, const Value *v, Buf *out, Ty *ty);

/* The expression of V into OUT; its type must be WANT. */
static int typed(Kernel *k, Block *b, const Value *v, Ty want, Buf *out) {
  Ty ty = want;
  if (!expr(k, b, v, out, &ty))
    return 0;
  return ty == want ? 1 : refuse(k, "a Flag and a Nat or U32 do not agree (internal)");
}

/* Binds X to a new local in B unless it is an atom; OUT gets the atom. */
static void share(Kernel *k, Block *b, const Buf *x, Buf *out) {
  char name[16];
  if (is_atom(x)) {
    put_buf(out, x);
    return;
  }
  snprintf(name, sizeof name, "l%u", (unsigned)k->locals++);
  indent(b);
  put(&b->text, "let ");
  put(&b->text, name);
  put(&b->text, " = ");
  put_buf(&b->text, x);
  put(&b->text, ";\n");
  put(out, name);
}

/* [0, n - 1], or [0, 0] when n is 0. */
static Range upto(uint64_t n) {
  Range r = {0u, n == 0u ? 0u : n - 1u};
  return r;
}

static uint64_t sat_add(uint64_t a, uint64_t b) {
  return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

static uint64_t sat_mul(uint64_t a, uint64_t b) {
  return a != 0u && b > UINT64_MAX / a ? UINT64_MAX : a * b;
}

static Range join(Range a, Range b) {
  Range r = {a.lo < b.lo ? a.lo : b.lo, a.hi > b.hi ? a.hi : b.hi};
  return r;
}

static int nat_op(const Value *v) {
  return v->kind == VAL_STUCK && v->argc == 2u &&
         (v->op == OP_NAT_ADD || v->op == OP_NAT_MUL || v->op == OP_NAT_SUB || v->op == OP_NAT_DIV || v->op == OP_NAT_MOD);
}

static Range range(Kernel *k, const Value *v);

static Range range_op(Kernel *k, const Value *v) {
  Range a = range(k, v->args[0]);
  Range b = range(k, v->args[1]);
  Range r = {0u, a.hi};
  uint64_t d = v->args[1]->kind == VAL_NAT ? v->args[1]->nat : 0u;
  if (v->op == OP_NAT_ADD) {
    r.lo = sat_add(a.lo, b.lo);
    r.hi = sat_add(a.hi, b.hi);
  }
  if (v->op == OP_NAT_MUL) {
    r.lo = sat_mul(a.lo, b.lo);
    r.hi = sat_mul(a.hi, b.hi);
  }
  if (v->op == OP_NAT_SUB) {
    r.lo = a.lo > b.hi ? a.lo - b.hi : 0u;
    r.hi = a.hi > b.lo ? a.hi - b.lo : 0u;
  }
  if (v->op == OP_NAT_DIV && d != 0u) {
    r.lo = a.lo / d;
    r.hi = a.hi / d;
  }
  if (v->op == OP_NAT_MOD && d != 0u) {
    r.lo = a.lo / d == a.hi / d ? a.lo % d : 0u;
    r.hi = a.lo / d == a.hi / d ? a.hi % d : d - 1u;
  }
  return r;
}

/* D9: the interval of the Nat value V over the intervals of the bound
   variables. natAdd, natMul, natSub and natDiv by a closed divisor are
   monotone in each argument, so the interval is exact when each variable
   occurs once and sound otherwise. flagIf gives the join of its arms (WGSL
   select runs both). A value of an other form gets [0, 2^64 - 1]. */
static Range range(Kernel *k, const Value *v) {
  Range r = {0u, UINT64_MAX};
  const Value *t = v != NULL && v->kind == VAL_STUCK && v->op == OP_AT && v->argc == 3u ? v->args[0] : NULL;
  if (v == NULL)
    return r;
  if (v->kind == VAL_NAT) {
    r.lo = v->nat;
    r.hi = v->nat;
    return r;
  }
  if (v->kind == VAL_VAR && v->nat >= k->param_count && v->nat < k->level)
    return k->ranges[v->nat - k->param_count];
  if (nat_op(v))
    return range_op(k, v);
  if (v->kind == VAL_STUCK && v->op == OP_FLAG_IF && v->argc == 3u)
    return join(range(k, v->args[1]), range(k, v->args[2]));
  if (t != NULL && t->kind == VAL_STUCK && t->op == OP_TABULATE && t->argc == 3u)
    return range(k, apply_value(k->m, apply_value(k->m, t->args[2], v->args[1]), v->args[2]));
  return r;
}

/* D9: the indexes X and Y of an at on a ROWS x COLS tile. */
static int inside(Kernel *k, const Value *x, const Value *y, uint64_t rows, uint64_t cols) {
  Range a = range(k, x);
  Range b = range(k, y);
  if (a.hi >= WGSL_U32_LIMIT || b.hi >= WGSL_U32_LIMIT)
    return refuse(k, "an at index that can reach 2^32 has no WGSL form (WGSL indexes are u32)");
  if (a.hi < rows && b.hi < cols)
    return 1;
  return diag_fail(k->m->diag, "BOUNDS", k->m->def,
                   "an at index can leave the %llu x %llu tile (row in [%llu, %llu], column in [%llu, %llu]; WGSL select runs both arms of flagIf)",
                   (unsigned long long)rows, (unsigned long long)cols, (unsigned long long)a.lo, (unsigned long long)a.hi,
                   (unsigned long long)b.lo, (unsigned long long)b.hi);
}

/* D9: natAdd and natMul are the only Nat operations with a result above
   their operands. A Nat is a u64 in C (a trap on overflow) and a u32 in WGSL
   (it wraps), so the result must stay below 2^32. */
static int fits(Kernel *k, const Value *v) {
  return range(k, v).hi < WGSL_U32_LIMIT ? 1 : refuse(k, "a natAdd or natMul that can reach 2^32 has no WGSL form (WGSL u32 arithmetic wraps)");
}

static int binary(Kernel *k, Block *b, const Value *v, const char *op, Buf *out, Ty *ty) {
  Buf x = {0};
  Buf y = {0};
  Buf none = {0};
  int ok;
  /* In WGSL x / 0u is x; in the C evaluator it is a trap. */
  if ((v->op == OP_NAT_DIV || v->op == OP_NAT_MOD) && (v->args[1]->kind != VAL_NAT || v->args[1]->nat == 0u))
    return refuse(k, "a natDiv or natMod divisor that is not a closed Nat above 0 has no WGSL form");
  if ((v->op == OP_NAT_ADD || v->op == OP_NAT_MUL) && !fits(k, v))
    return 0;
  ok = typed(k, b, v->args[0], TY_U32, &x) && typed(k, b, v->args[1], TY_U32, &y);
  put(out, "(");
  put_buf(out, &x);
  put(out, " ");
  put(out, op);
  put(out, " ");
  put_buf(out, &y);
  put(out, ")");
  *ty = v->op == OP_NAT_EQ || v->op == OP_NAT_LE ? TY_BOOL : TY_U32;
  free_bufs(&x, &y, &none);
  return ok;
}

/* natSub a b: 0 when b > a. */
static int monus(Kernel *k, Block *b, const Value *v, Buf *out, Ty *ty) {
  Buf x = {0};
  Buf y = {0};
  Buf sx = {0};
  Buf sy = {0};
  Buf none = {0};
  int ok = typed(k, b, v->args[0], TY_U32, &x) && typed(k, b, v->args[1], TY_U32, &y);
  share(k, b, &x, &sx);
  share(k, b, &y, &sy);
  put(out, "select(0u, ");
  put_buf(out, &sx);
  put(out, " - ");
  put_buf(out, &sy);
  put(out, ", ");
  put_buf(out, &sx);
  put(out, " >= ");
  put_buf(out, &sy);
  put(out, ")");
  *ty = TY_U32;
  free_bufs(&x, &y, &none);
  free_bufs(&sx, &sy, &none);
  return ok;
}

/* flagIf c t e: WGSL select evaluates both arms. */
static int choose(Kernel *k, Block *b, const Value *v, Buf *out, Ty *ty) {
  Buf c = {0};
  Buf t = {0};
  Buf e = {0};
  Ty tt = TY_U32;
  int ok = typed(k, b, v->args[0], TY_BOOL, &c) && expr(k, b, v->args[1], &t, &tt) && typed(k, b, v->args[2], tt, &e);
  put(out, "select(");
  put_buf(out, &e);
  put(out, ", ");
  put_buf(out, &t);
  put(out, ", ");
  put_buf(out, &c);
  put(out, ")");
  *ty = tt;
  free_bufs(&c, &t, &e);
  return ok;
}

/* at t x y: a tabulate fuses (D11); a parameter is a buffer read. The
   indexes must stay inside the tile (D9): the fusion removes the bounds
   trap of the evaluator, and WGSL clamps or discards an index out of range. */
static int cell(Kernel *k, Block *b, const Value *t, const Value *x, const Value *y, Buf *out, Ty *ty) {
  Buf i = {0};
  Buf j = {0};
  Buf none = {0};
  uint64_t p;
  int flag;
  int ok;
  if (t == NULL || x == NULL || y == NULL)
    return 0;
  if (t->kind == VAL_STUCK && t->op == OP_TABULATE && t->argc == 3u)
    return t->args[0]->kind != VAL_NAT || t->args[1]->kind != VAL_NAT
               ? refuse(k, "a tabulate with an open shape has no WGSL form")
               : inside(k, x, y, t->args[0]->nat, t->args[1]->nat) &&
                     expr(k, b, apply_value(k->m, apply_value(k->m, t->args[2], x), y), out, ty);
  p = t->nat;
  if (t->kind != VAL_VAR || p >= k->param_count || !k->params[p].tile)
    return refuse(k, "a tile that is not a tabulate or a parameter has no WGSL form");
  if (!inside(k, x, y, k->params[p].rows, k->params[p].cols))
    return 0;
  k->used[p] = 1;
  flag = k->params[p].elem == TY_BOOL;
  ok = typed(k, b, x, TY_U32, &i) && typed(k, b, y, TY_U32, &j);
  put(out, flag ? "(" : "");
  param_name(k, (uint32_t)p, out);
  put(out, "[");
  put_buf(out, &i);
  put(out, " * ");
  put_u64(out, k->params[p].cols);
  put(out, "u + ");
  put_buf(out, &j);
  put(out, flag ? "] != 0u)" : "]");
  *ty = k->params[p].elem;
  free_bufs(&i, &j, &none);
  return ok;
}

/* sumTo n f and foldTo n op e f: a var and a for loop, hoisted into B. */
static int loop(Kernel *k, Block *b, const Value *v, int fold, Buf *out, Ty *ty) {
  Machine *m = k->m;
  Block body;
  Buf init = {0};
  Buf bound = {0};
  Buf step = {0};
  char acc[16];
  char counter[16];
  Ty acc_ty = TY_U32;
  Range n;
  const Value *kv;
  const Value *av;
  const Value *g;
  int ok;
  memset(&body, 0, sizeof body);
  body.indent = b->indent + 1u;
  ok = typed(k, b, v->args[0], TY_U32, &bound) && (!fold || expr(k, b, v->args[2], &init, &acc_ty));
  put(&init, fold ? "" : "0u");
  snprintf(acc, sizeof acc, "l%u", (unsigned)k->locals++);
  snprintf(counter, sizeof counter, "l%u", (unsigned)k->locals++);
  /* The counter is below the bound. Each Nat that the shader holds is a
     literal, a counter or a result that passed fits, so the accumulator is
     below 2^32 (D9). */
  n = range(k, v->args[0]);
  kv = ok ? bind(k, counter, TY_U32, upto(n.hi)) : NULL;
  av = kv != NULL && fold ? bind(k, acc, acc_ty, upto(WGSL_U32_LIMIT)) : NULL;
  g = fold ? apply_value(m, apply_value(m, v->args[1], av), apply_value(m, v->args[3], kv)) : apply_value(m, v->args[1], kv);
  ok = g != NULL && typed(k, &body, g, acc_ty, &step);
  indent(&body);
  put(&body.text, acc);
  put(&body.text, " = ");
  put(&body.text, fold ? "" : acc);
  put(&body.text, fold ? "" : " + ");
  put_buf(&body.text, &step);
  put(&body.text, ";\n");
  indent(b);
  put(&b->text, "var ");
  put(&b->text, acc);
  put(&b->text, " = ");
  put_buf(&b->text, &init);
  put(&b->text, ";\n");
  indent(b);
  put(&b->text, "for (var ");
  put(&b->text, counter);
  put(&b->text, " = 0u; ");
  put(&b->text, counter);
  put(&b->text, " < ");
  put_buf(&b->text, &bound);
  put(&b->text, "; ");
  put(&b->text, counter);
  put(&b->text, " = ");
  put(&b->text, counter);
  put(&b->text, " + 1u) {\n");
  put_buf(&b->text, &body.text);
  indent(b);
  put(&b->text, "}\n");
  b->text.failed |= body.text.failed;
  put(out, acc);
  *ty = acc_ty;
  free_bufs(&init, &bound, &step);
  free(body.text.bytes);
  return ok;
}

static int stuck(Kernel *k, Block *b, const Value *v, Buf *out, Ty *ty) {
  const char *op = (size_t)v->op < sizeof BINARY / sizeof BINARY[0] ? BINARY[v->op] : NULL;
  if (op != NULL && v->argc == 2u)
    return binary(k, b, v, op, out, ty);
  if (v->op == OP_NAT_SUB && v->argc == 2u)
    return monus(k, b, v, out, ty);
  if (v->op == OP_FLAG_IF && v->argc == 3u)
    return choose(k, b, v, out, ty);
  if (v->op == OP_AT && v->argc == 3u)
    return cell(k, b, v->args[0], v->args[1], v->args[2], out, ty);
  if ((v->op == OP_SUM_TO && v->argc == 2u) || (v->op == OP_FOLD_TO && v->argc == 4u))
    return loop(k, b, v, v->op == OP_FOLD_TO, out, ty);
  if (v->op == OP_TABULATE)
    return refuse(k, "a tile outside the first argument of at has no WGSL form");
  return diag_fail(k->m->diag, "WGSL_FORM", k->m->def, "%s has no WGSL form", op_name(k->m, v->op, v->inst, v->field));
}

static int constant(Kernel *k, const Value *v, Buf *out, Ty *ty) {
  *ty = TY_BOOL;
  if (v->op == OP_FLAG_YES || v->op == OP_FLAG_NO) {
    put(out, v->op == OP_FLAG_YES ? "true" : "false");
    return 1;
  }
  return diag_fail(k->m->diag, "WGSL_FORM", k->m->def, "%s has no WGSL form", op_name(k->m, v->op, v->inst, v->field));
}

static int variable(Kernel *k, const Value *v, Buf *out, Ty *ty) {
  uint64_t level = v->nat;
  if (level < k->param_count && k->params[level].tile)
    return refuse(k, "a tile parameter outside the first argument of at has no WGSL form");
  if (level < k->param_count) {
    k->used[level] = 1;
    param_name(k, (uint32_t)level, out);
    put(out, "[0]");
    *ty = TY_U32;
    return 1;
  }
  if (level >= k->level)
    return refuse(k, "an unbound variable (internal)");
  put(out, k->names[level - k->param_count]);
  *ty = k->types[level - k->param_count];
  return 1;
}

static int expr(Kernel *k, Block *b, const Value *v, Buf *out, Ty *ty) {
  if (v == NULL)
    return 0;
  switch (v->kind) {
  case VAL_U32:
    *ty = TY_U32;
    put_u64(out, v->nat);
    put(out, "u");
    return 1;
  case VAL_NAT:
    *ty = TY_U32;
    put_u64(out, v->nat);
    put(out, "u");
    return v->nat <= 4294967295u ? 1 : refuse(k, "a Nat above 2^32 - 1 has no WGSL form");
  case VAL_OP:
    return constant(k, v, out, ty);
  case VAL_VAR:
    return variable(k, v, out, ty);
  case VAL_STUCK:
    return stuck(k, b, v, out, ty);
  case VAL_STR:
    return refuse(k, "a Str has no WGSL form");
  case VAL_TRAP:
    return refuse(k, "a trap has no WGSL form");
  case VAL_UNIV:
  case VAL_PI:
  case VAL_SIGMA:
    return refuse(k, "a type has no WGSL form");
  case VAL_LAM:
    return refuse(k, "a function has no WGSL form");
  case VAL_APP:
    return refuse(k, "an application of a variable has no WGSL form");
  }
  return refuse(k, "an unknown value (internal)");
}

static int shape_of(const Value *t, Shape *s) {
  const Value *e;
  memset(s, 0, sizeof *s);
  if (t == NULL || t->kind != VAL_OP)
    return 0;
  if (t->op == OP_U32)
    return 1;
  if (t->op != OP_TILE || t->argc != 3u)
    return 0;
  e = t->args[2];
  if (t->args[0]->kind != VAL_NAT || t->args[1]->kind != VAL_NAT || e->kind != VAL_OP || (e->op != OP_U32 && e->op != OP_FLAG))
    return 0;
  s->tile = 1;
  s->rows = t->args[0]->nat;
  s->cols = t->args[1]->nat;
  s->elem = e->op == OP_FLAG ? TY_BOOL : TY_U32;
  return 1;
}

static int small(const Shape *s) {
  return !s->tile || (s->rows <= WGSL_CELLS_MAX && s->cols <= WGSL_CELLS_MAX && s->rows * s->cols <= WGSL_CELLS_MAX);
}

static int wgsl_name(const char *name) {
  char pad[WGSL_NAME_MAX + 3u];
  size_t len = strlen(name);
  size_t i;
  if (len == 0 || len > WGSL_NAME_MAX || strcmp(name, "_") == 0 || strncmp(name, "__", 2) == 0)
    return 0;
  for (i = 0; i < len; i++) {
    char c = name[i];
    int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok)
      return 0;
  }
  snprintf(pad, sizeof pad, " %s ", name);
  return strstr(RESERVED, pad) == NULL;
}

/* 1 when definition D is an entry (D8), 0 when it is not, -1 after a refusal. */
static int entry_shape(Kernel *k, const DefInfo *d) {
  const Value *t = d->type;
  uint32_t n = 0;
  uint32_t p;
  Shape s;
  /* More than 7 parameters is a refusal only for an entry: every build makes
     the module (D14), so a refusal refuses the build for every target. */
  while (t != NULL && t->kind == VAL_PI && shape_of(t->dom, &s)) {
    if (n < WGSL_PARAMS_MAX)
      k->params[n] = s;
    t = closure_apply(k->m, t, val_var(k->m, n));
    n++;
  }
  if (t == NULL)
    return -1;
  if (n == 0 || !shape_of(t, &k->result) || !k->result.tile)
    return 0;
  if (n > WGSL_PARAMS_MAX)
    return refuse(k, "an entry with more than 7 parameters has no WGSL form (WebGPU gives a shader 8 storage buffers)") - 1;
  k->param_count = n;
  k->name = d->name;
  if (!wgsl_name(d->name))
    return refuse(k, "an entry name must be a WGSL identifier: ASCII letters, digits and '_', not a WGSL keyword or reserved word") - 1;
  for (p = 0; p < n; p++)
    if (!small(&k->params[p]))
      return refuse(k, "an entry tile with more than 1048576 cells has no WGSL form") - 1;
  return small(&k->result) ? 1 : refuse(k, "an entry tile with more than 1048576 cells has no WGSL form") - 1;
}

static void shape_text(Buf *doc, const Shape *s) {
  if (!s->tile) {
    put(doc, "U32");
    return;
  }
  put(doc, "Tile ");
  put_u64(doc, s->rows);
  put(doc, " ");
  put_u64(doc, s->cols);
  put(doc, s->elem == TY_BOOL ? " Flag" : " U32");
}

/* Each entry numbers its bindings from 0: parameter P has the binding P, and
   the result has the binding param_count. Thus a binding is at most 7, for
   each count of entries. Two entries can share a binding number, because no
   entry point uses a buffer of another entry (WGSL: the resource interface
   of a shader). */
static void binding_line(Buf *doc, const Kernel *k, uint32_t p, const char *access) {
  put(doc, "@group(0) @binding(");
  put_u64(doc, p);
  put(doc, ") var<storage, ");
  put(doc, access);
  put(doc, "> ");
  if (p < k->param_count)
    param_name(k, p, doc);
  else {
    put(doc, k->name);
    put(doc, "_out");
  }
  put(doc, ": array<u32>;\n");
}

static void write_kernel(const Kernel *k, const Block *body, const Buf *value, Buf *doc) {
  int flag = k->result.elem == TY_BOOL;
  uint32_t p;
  put(doc, "\n// ");
  put(doc, k->name);
  put(doc, ":");
  for (p = 0; p < k->param_count; p++) {
    put(doc, " ");
    shape_text(doc, &k->params[p]);
    put(doc, " ->");
  }
  put(doc, " ");
  shape_text(doc, &k->result);
  put(doc, "\n");
  for (p = 0; p < k->param_count; p++)
    binding_line(doc, k, p, "read");
  binding_line(doc, k, k->param_count, "read_write");
  put(doc, "\n@compute @workgroup_size(" WGSL_WORKGROUP ")\nfn ");
  put(doc, k->name);
  put(doc, "(@builtin(global_invocation_id) id: vec3<u32>) {\n  let idx = id.x;\n  if (idx >= ");
  put_u64(doc, k->result.rows * k->result.cols);
  put(doc, "u) {\n    return;\n  }\n");
  /* Each parameter is a binding of the pipeline, also when unused. */
  for (p = 0; p < k->param_count; p++) {
    if (k->used[p])
      continue;
    put(doc, "  _ = arrayLength(&");
    param_name(k, p, doc);
    put(doc, ");\n");
  }
  put(doc, "  let i = idx / ");
  put_u64(doc, k->result.cols);
  put(doc, "u;\n  let j = idx % ");
  put_u64(doc, k->result.cols);
  put(doc, "u;\n");
  put_buf(doc, &body->text);
  put(doc, "  ");
  put(doc, k->name);
  put(doc, flag ? "_out[idx] = select(0u, 1u, " : "_out[idx] = ");
  put_buf(doc, value);
  put(doc, flag ? ");\n}\n" : ";\n}\n");
}

static int emit_kernel(Kernel *k, uint32_t index, Buf *doc) {
  Machine *m = k->m;
  Block body;
  Buf value = {0};
  Ty ty = TY_U32;
  const Value *v;
  const Value *i;
  const Value *j;
  uint32_t p;
  int ok;
  memset(&body, 0, sizeof body);
  body.indent = 1u;
  m->symbolic = 1;
  v = def_value(m, index);
  for (p = 0; p < k->param_count; p++)
    v = apply_value(m, v, val_var(m, p));
  k->level = k->param_count;
  i = v == NULL ? NULL : bind(k, "i", TY_U32, upto(k->result.rows));
  j = i == NULL ? NULL : bind(k, "j", TY_U32, upto(k->result.cols));
  ok = j != NULL && cell(k, &body, v, i, j, &value, &ty);
  m->symbolic = 0;
  ok = ok && (ty == k->result.elem || refuse(k, "the result cell does not agree with the result type (internal)"));
  if (ok)
    write_kernel(k, &body, &value, doc);
  doc->failed |= body.text.failed | value.failed;
  free(body.text.bytes);
  free(value.bytes);
  return ok;
}

/* A parameter (P below the parameter count) or the result of the entry K in
   the manifest: its binding name and number, and the shape. A U32 parameter
   has no rows and cols. */
static void manifest_shape(const Kernel *k, uint32_t p, const Shape *s, Buf *man) {
  put(man, "{\"name\":\"");
  if (p < k->param_count)
    param_name(k, p, man);
  else {
    put(man, k->name);
    put(man, "_out");
  }
  put(man, "\",\"binding\":");
  put_u64(man, p);
  if (s->tile) {
    put(man, ",\"rows\":");
    put_u64(man, s->rows);
    put(man, ",\"cols\":");
    put_u64(man, s->cols);
  }
  put(man, s->tile && s->elem == TY_BOOL ? ",\"elem\":\"Flag\"}" : ",\"elem\":\"U32\"}");
}

/* The manifest object of the entry K (D12), after a ',' when it is not the
   first: the JSON text of the "kernels" member. */
static void write_manifest(const Kernel *k, Buf *man) {
  uint64_t cells = k->result.rows * k->result.cols;
  uint32_t p;
  put(man, man->len == 0 ? "[{\"name\":\"" : ",{\"name\":\"");
  put(man, k->name);
  put(man, "\",\"params\":[");
  for (p = 0; p < k->param_count; p++) {
    put(man, p == 0 ? "" : ",");
    manifest_shape(k, p, &k->params[p], man);
  }
  put(man, "],\"result\":");
  manifest_shape(k, k->param_count, &k->result, man);
  put(man, ",\"workgroup\":" WGSL_WORKGROUP ",\"dispatch\":");
  put_u64(man, (cells + WGSL_WORKGROUP_N - 1u) / WGSL_WORKGROUP_N);
  put(man, "}");
}

/* 1 when NAME is the name of a binding of the entry E with N parameters:
   E_out, or E_inP with P below N. */
static int binding_of(const char *name, const char *e, uint32_t n) {
  size_t len = strlen(e);
  const char *rest;
  if (strncmp(name, e, len) != 0)
    return 0;
  rest = name + len;
  return strcmp(rest, "_out") == 0 ||
         (rest[0] == '_' && rest[1] == 'i' && rest[2] == 'n' && rest[3] >= '0' && rest[3] < (char)('0' + n) && rest[4] == '\0');
}

/* The entries share the module scope: an entry name cannot be the name of a
   binding of an other entry. 1 after a refusal. */
static int collides(Kernel *k, const char *const *names, const uint32_t *counts, uint32_t entries) {
  uint32_t e;
  for (e = 0; e < entries; e++)
    if (binding_of(k->name, names[e], counts[e]) || binding_of(names[e], k->name, k->param_count)) {
      refuse(k, "an entry name cannot be the name of a binding of another entry (NAME_inP or NAME_out)");
      return 1;
    }
  return 0;
}

int wgsl_module(Machine *m, WgslModule *out) {
  Buf doc = {0};
  Buf man = {0};
  uint32_t entries = 0;
  uint32_t i;
  int ok = 1;
  char *copy;
  char *list;
  const char **names = arena_alloc(m->arena, (m->def_count + 1u) * sizeof *names);
  uint32_t *counts = arena_alloc(m->arena, (m->def_count + 1u) * sizeof *counts);
  if (names == NULL || counts == NULL)
    return diag_fail(m->diag, "OOM", NULL, "no memory for the WGSL module");
  put(&doc, HEADER);
  m->fuel = EVAL_FUEL_STEPS;
  for (i = 0; ok && i < m->def_count; i++) {
    Kernel k;
    int found;
    memset(&k, 0, sizeof k);
    k.m = m;
    m->def = m->defs[i].name;
    found = m->defs[i].origin == ORIGIN_PROGRAM ? entry_shape(&k, &m->defs[i]) : 0;
    ok = found >= 0 && (found == 0 || (!collides(&k, names, counts, entries) && emit_kernel(&k, i, &doc)));
    if (ok && found > 0)
      write_manifest(&k, &man);
    names[entries] = k.name;
    counts[entries] = k.param_count;
    entries += found > 0 ? 1u : 0u;
  }
  m->def = NULL;
  m->symbolic = 0;
  if (entries > 0)
    put(&man, "]");
  copy = ok && !doc.failed && !man.failed ? arena_alloc(m->arena, doc.len + 1u) : NULL;
  list = copy != NULL ? arena_alloc(m->arena, man.len + 1u) : NULL;
  if (list != NULL) {
    memcpy(copy, doc.bytes, doc.len + 1u);
    memcpy(list, man.len > 0 ? man.bytes : "", man.len + 1u);
  }
  free(doc.bytes);
  free(man.bytes);
  if (!ok)
    return 0;
  if (list == NULL)
    return diag_fail(m->diag, "OOM", NULL, "no memory for the WGSL module");
  out->text = copy;
  out->len = doc.len;
  out->manifest = list;
  out->manifest_len = man.len;
  out->count = entries;
  return 1;
}
