/* The langc checker (SPEC chunk 3): normalization by evaluation over
 * the AST of src/syntax.h. One Bind list is both the environment of
 * evaluation and the typing context; a name resolves to the locals first,
 * then to the globals declared so far. Evaluation is call by value and
 * unfolds every def. A def rec unfolds when it has all its arguments and
 * the decreasing one is a saturated constructor; natAdd, natSub, natEq and
 * natLt reduce on literals; the opaque core names of SPEC note P1 never
 * reduce. After the first error every function returns
 * at once. */
#include "check.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

enum {
  CHECK_FUEL = 1 << 24,  /* evaluation steps of one declaration or of the fork check */
  CHECK_DEPTH = 4096,    /* nested eval, apply, conv, quote, check and infer calls; tcc on an 8 MB stack crashed between 12288 and 16384 (10-07) */
  SINK_BYTES = 8192,     /* the allocations after a MEMORY error land here */
  TABLE_LIMIT = 4096,    /* tallies of one table (SPEC section 7); my choice, not ruled */
  AMEND_LIMIT = 8        /* constitutions of one program (SPEC section 7); my choice, not ruled */
};

typedef struct Value Value;
typedef struct Bind Bind;
typedef struct Frame Frame;
typedef struct Global Global;
typedef struct Names Names;

typedef enum {
  V_TYPE, V_NAT_TYPE, V_NAT, V_PI, V_SIGMA, V_LAM, V_PAIR, V_TUPLE, V_UNIT,
  V_PROD0, V_PROD, V_SUM, V_INJ, V_DATA, V_CON, V_NEU
} ValueKind;

typedef enum { G_DEF, G_REC, G_MU, G_CTOR, G_PRIM, G_OPAQUE } GlobalKind;
typedef enum { P_ADD, P_SUB, P_EQ, P_LT } Prim;
typedef enum { F_APP, F_PROJ, F_CASE, F_MATCH } FrameKind;

struct Bind {
  const char *name;  /* NULL for the binder of A -> B */
  Value *value;
  Value *type;       /* NULL in an environment of evaluation only */
  int erased;
  Bind *next;
};

struct Frame {
  FrameKind kind;
  Value *arg;      /* F_APP */
  unsigned index;  /* F_PROJ */
  Bind *env;       /* F_CASE, F_MATCH */
  const Ast *ast;  /* F_CASE, F_MATCH: the eliminator */
  Frame *next;     /* the older frames */
};

struct Value {
  ValueKind kind;
  unsigned long long nat;  /* V_TYPE level, V_NAT, V_INJ tag */
  const char *name;        /* V_PI, V_SIGMA binder; V_NEU variable */
  int erased;              /* V_PI, V_SIGMA */
  Value *left;             /* V_PI, V_SIGMA domain; V_PAIR, V_TUPLE, V_PROD, V_SUM; V_INJ value */
  Value *right;            /* V_PAIR, V_TUPLE, V_PROD, V_SUM; V_PI codomain of a builtin arrow */
  Bind *env;               /* V_PI, V_SIGMA, V_LAM closure */
  const Ast *body;         /* V_PI, V_SIGMA codomain (NULL for a builtin arrow); V_LAM node */
  Global *global;          /* V_DATA family, V_CON ctor, V_NEU global head */
  Value **args;            /* V_DATA, V_CON */
  size_t nargs;
  int level;               /* V_NEU variable head; -1 for a global head */
  Frame *spine;            /* V_NEU, newest first */
};

struct Global {
  const char *name;
  GlobalKind kind;
  Prim prim;          /* G_PRIM */
  int prelude;        /* a builtin or a prelude declaration */
  Value *type;
  Value *value;
  const Decl *decl;   /* G_DEF, G_REC, G_MU */
  size_t arity;       /* G_REC lambdas, G_MU indices, G_CTOR fields, G_PRIM 2 */
  size_t decreasing;  /* G_REC: the lambda that the top match takes apart */
  size_t index;       /* G_CTOR: the position in its mu */
  Global *family;     /* G_CTOR */
  Value *body;        /* G_REC: the lambda chain, once checked */
  Global *next;
};

/* The scope of the structural check of a def rec. */
struct Names {
  const char *name;
  int smaller;  /* a pattern variable of the top match */
  Names *next;
};

struct AnchorChecked {
  Arena *arena;
  Diag *diag;
  int failed;
  long fuel;
  int depth;
  const char *file;
  Span def;
  Loc loc;
  Global *globals;  /* newest first */
  unsigned members;
  const char *fork_def;   /* rule or amendments: the def of the symbolic fork check */
  size_t constitutions;   /* C: rule, then the constitutions of amendments (O3) */
  Value **amends;         /* constitutions 1 to C - 1 */
  Value bad;
  Ast bad_ast;
  union { long double ld; void *p; unsigned long long u; unsigned char bytes[SINK_BYTES]; } sink;
};

typedef AnchorChecked C;

static Value *eval(C *c, Bind *env, const Ast *t);
static Value *apply(C *c, Value *f, Value *a);
static int conv(C *c, Value *a, Value *b, int lvl);
static Ast *quote(C *c, Value *v, int lvl);
static Value *infer(C *c, Bind *ctx, int lvl, const Ast *t, int relevant);
static void check(C *c, Bind *ctx, int lvl, const Ast *t, Value *want, int relevant);

/* ---- errors, memory, names ---- */

static Value *fail(C *c, const char *code, Loc loc, const char *format, ...) {
  va_list args;
  va_start(args, format);
  diag_vat(c->diag, code, c->def, c->file, loc, format, args);
  va_end(args);
  c->failed = 1;
  return &c->bad;
}

static int refuse(C *c, const char *code, const char *name, const char *text) {
  diag_set(c->diag, code, span_of(name), "%s %s", name, text);
  c->failed = 1;
  return LANG_EXIT_REFUSED;
}

static void *alloc(C *c, size_t size) {
  void *p = c->failed || size > SINK_BYTES ? NULL : arena_alloc(c->arena, size);
  if (p != NULL)
    return p;
  if (!c->failed)
    fail(c, "MEMORY", c->loc, "the arena is full");
  memset(c->sink.bytes, 0, sizeof c->sink.bytes);
  return c->sink.bytes;
}

static int enter(C *c) {
  if (c->failed)
    return 0;
  if (c->depth >= CHECK_DEPTH || c->fuel <= 0) {
    fail(c, "TYPE_FUEL", c->loc, "%s", c->fuel <= 0 ? "evaluation ran out of fuel" : "evaluation nests too deep");
    return 0;
  }
  c->depth++;
  c->fuel--;
  return 1;
}

static int same(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}

static Bind *bind(C *c, Bind *next, const char *name, Value *value, Value *type, int erased) {
  Bind *b = alloc(c, sizeof *b);
  b->name = name;
  b->value = value;
  b->type = type;
  b->erased = erased;
  b->next = next;
  return b;
}

static Bind *find_local(Bind *env, const char *name) {
  while (env != NULL && !same(env->name, name))
    env = env->next;
  return env;
}

static Global *find_global(const C *c, const char *name) {
  Global *g = c->globals;
  while (g != NULL && !same(g->name, name))
    g = g->next;
  return g;
}

static const MatchArm *find_arm(const Ast *t, const char *ctor) {
  for (size_t i = 0; i < t->u.match.narms; i++)
    if (same(t->u.match.arms[i].ctor, ctor))
      return &t->u.match.arms[i];
  return NULL;
}

/* ---- values ---- */

static Value *mk(C *c, ValueKind kind) {
  Value *v = alloc(c, sizeof *v);
  v->kind = kind;
  v->level = -1;
  return v;
}

static Value *mk_nat(C *c, unsigned long long n) {
  Value *v = mk(c, V_NAT);
  v->nat = n;
  return v;
}

static Value *mk_type(C *c, unsigned long long level) {
  Value *v = mk(c, V_TYPE);
  v->nat = level;
  return v;
}

static Value *mk_two(C *c, ValueKind kind, Value *left, Value *right) {
  Value *v = mk(c, kind);
  v->left = left;
  v->right = right;
  return v;
}

static Value *mk_var(C *c, int level, const char *name) {
  Value *v = mk(c, V_NEU);
  v->level = level;
  v->name = name != NULL ? name : "_";
  return v;
}

static Value *mk_global(C *c, ValueKind kind, Global *g) {
  Value *v = mk(c, kind);
  v->global = g;
  return v;
}

static Value *mk_bool(C *c, int truth) {
  Value *v = mk_two(c, V_INJ, mk(c, V_UNIT), NULL);
  v->nat = truth ? 1 : 0;
  return v;
}

static Frame *frame(C *c, FrameKind kind, Bind *env, const Ast *ast) {
  Frame *f = alloc(c, sizeof *f);
  f->kind = kind;
  f->env = env;
  f->ast = ast;
  return f;
}

static Value *push(C *c, Value *neutral, Frame *f) {
  Value *v = mk(c, V_NEU);
  if (c->failed)
    return &c->bad;
  *v = *neutral;
  f->next = neutral->spine;
  v->spine = f;
  return v;
}

/* ---- evaluation ---- */

static Value *instantiate(C *c, Value *pi, Value *a) {
  if (c->failed)
    return &c->bad;
  if (pi->body == NULL)
    return pi->right;
  return eval(c, bind(c, pi->env, pi->name, a, NULL, 0), pi->body);
}

static Value *do_proj(C *c, Value *v, unsigned index) {
  if (c->failed)
    return &c->bad;
  if (v->kind == V_PAIR || v->kind == V_TUPLE)
    return index == 0 ? v->left : v->right;
  if (v->kind != V_NEU)
    return fail(c, "TYPE_INTERNAL", c->loc, "a projection of a value that is not a pair");
  Frame *f = frame(c, F_PROJ, NULL, NULL);
  f->index = index;
  return push(c, v, f);
}

static Value *do_case(C *c, Value *v, Bind *env, const Ast *t) {
  if (c->failed)
    return &c->bad;
  if (v->kind == V_NEU)
    return push(c, v, frame(c, F_CASE, env, t));
  if (v->kind != V_INJ)
    return fail(c, "TYPE_INTERNAL", t->loc, "a case of a value that is not an injection");
  const CaseArm *arm = &t->u.cases.arms[v->nat == 0 ? 0 : 1];
  return eval(c, bind(c, env, arm->binder.name, v->left, NULL, 0), arm->body);
}

static Bind *bind_args(C *c, Bind *env, const MatchArm *arm, Value **args) {
  for (size_t i = 0; i < arm->nvars; i++)
    env = bind(c, env, arm->vars[i].name, args[i], NULL, 0);
  return env;
}

static Value *do_match(C *c, Value *v, Bind *env, const Ast *t) {
  if (c->failed)
    return &c->bad;
  if (v->kind == V_NEU)
    return push(c, v, frame(c, F_MATCH, env, t));
  const MatchArm *arm = v->kind == V_CON ? find_arm(t, v->global->name) : NULL;
  if (arm == NULL || arm->nvars != v->nargs)
    return fail(c, "TYPE_INTERNAL", t->loc, "a match with no arm for its value");
  return eval(c, bind_args(c, env, arm, v->args), arm->body);
}

static Value *closure(C *c, ValueKind kind, Bind *env, const Ast *t) {
  Value *v = mk(c, kind);
  v->name = t->u.bind.binder.name;
  v->erased = t->u.bind.binder.erased;
  v->left = eval(c, env, t->u.bind.binder.type);
  v->env = env;
  v->body = t->u.bind.body;
  return v;
}

static Value *eval_var(C *c, Bind *env, const Ast *t) {
  Bind *b = find_local(env, t->u.name);
  Global *g = b == NULL ? find_global(c, t->u.name) : NULL;
  if (b != NULL)
    return b->value;
  if (g == NULL || g->value == NULL)
    return fail(c, "TYPE_SCOPE", t->loc, "%s is not in scope", t->u.name);
  return g->value;
}

static Value *eval_body(C *c, Bind *env, const Ast *t) {
  switch (t->kind) {
  case AST_VAR: return eval_var(c, env, t);
  case AST_NAT: return mk_nat(c, t->u.nat);
  case AST_TYPE: return mk_type(c, t->u.level);
  case AST_PI: return closure(c, V_PI, env, t);
  case AST_SIGMA: return closure(c, V_SIGMA, env, t);
  case AST_LAM: {
    Value *v = mk(c, V_LAM);
    v->env = env;
    v->body = t;
    return v;
  }
  case AST_APP: return apply(c, eval(c, env, t->u.app.fun), eval(c, env, t->u.app.arg));
  case AST_PAIR: return mk_two(c, V_PAIR, eval(c, env, t->u.pair.left), eval(c, env, t->u.pair.right));
  case AST_TUPLE0: return mk(c, V_UNIT);
  case AST_TUPLE: return mk_two(c, V_TUPLE, eval(c, env, t->u.pair.left), eval(c, env, t->u.pair.right));
  case AST_PROD0: return mk(c, V_PROD0);
  case AST_PROD: return mk_two(c, V_PROD, eval(c, env, t->u.pair.left), eval(c, env, t->u.pair.right));
  case AST_SUM: return mk_two(c, V_SUM, eval(c, env, t->u.pair.left), eval(c, env, t->u.pair.right));
  case AST_INJ: {
    Value *v = mk_two(c, V_INJ, eval(c, env, t->u.inj.value), NULL);
    v->nat = t->u.inj.tag;
    return v;
  }
  case AST_CASE: return do_case(c, eval(c, env, t->u.cases.subject), env, t);
  case AST_MATCH: return do_match(c, eval(c, env, t->u.match.subject), env, t);
  case AST_PROJ: return do_proj(c, eval(c, env, t->u.proj.term), t->u.proj.index);
  }
  return fail(c, "TYPE_INTERNAL", t->loc, "an unknown term");
}

static Value *eval(C *c, Bind *env, const Ast *t) {
  if (!enter(c))
    return &c->bad;
  Value *v = eval_body(c, env, t);
  c->depth--;
  return v;
}

static Value *prim(C *c, Prim p, Value *a, Value *b, Value *stuck) {
  if (a->kind != V_NAT || b->kind != V_NAT)
    return stuck;
  switch (p) {
  case P_ADD:
    if (a->nat > ULLONG_MAX - b->nat)
      return fail(c, "TYPE_NAT", c->loc, "natAdd %llu %llu overflows", a->nat, b->nat);
    return mk_nat(c, a->nat + b->nat);
  case P_SUB: return mk_nat(c, a->nat > b->nat ? a->nat - b->nat : 0);
  case P_EQ: return mk_bool(c, a->nat == b->nat);
  case P_LT: return mk_bool(c, a->nat < b->nat);
  }
  return stuck;
}

static Value *unfold(C *c, Global *g, Value **args, Value *stuck) {
  Value *d = args[g->decreasing];
  if (g->body == NULL || d->kind != V_CON || d->nargs != d->global->arity)
    return stuck;
  Value *v = g->body;
  for (size_t i = 0; i < g->arity; i++)
    v = apply(c, v, args[i]);
  return v;
}

/* A neutral with a global head reduces once it has all its arguments. */
static Value *reduce(C *c, Value *n) {
  if (c->failed || n->global == NULL)
    return n;
  Global *g = n->global;
  size_t count = 0;
  Frame *f = n->spine;
  for (; f != NULL && f->kind == F_APP; f = f->next)
    count++;
  if (f != NULL || count != g->arity || count == 0)
    return n;
  Value **args = alloc(c, count * sizeof *args);
  if (c->failed)
    return &c->bad;
  size_t i = count;
  for (f = n->spine; f != NULL; f = f->next)
    args[--i] = f->arg;
  switch (g->kind) {
  case G_PRIM: return prim(c, g->prim, args[0], args[1], n);
  case G_REC: return unfold(c, g, args, n);
  case G_DEF:
  case G_MU:
  case G_CTOR:
  case G_OPAQUE: break;
  }
  return n;
}

static Value *extend(C *c, Value *f, Value *a) {
  if (f->nargs >= f->global->arity)
    return fail(c, "TYPE_INTERNAL", c->loc, "%s has too many arguments", f->global->name);
  Value **args = alloc(c, (f->nargs + 1) * sizeof *args);
  Value *v = mk_global(c, f->kind, f->global);
  for (size_t i = 0; i < f->nargs; i++)
    args[i] = f->args[i];
  args[f->nargs] = a;
  v->args = args;
  v->nargs = f->nargs + 1;
  return v;
}

static Value *apply_body(C *c, Value *f, Value *a) {
  switch (f->kind) {
  case V_LAM: {
    const Ast *lam = f->body;
    return eval(c, bind(c, f->env, lam->u.bind.binder.name, a, NULL, 0), lam->u.bind.body);
  }
  case V_DATA:
  case V_CON: return extend(c, f, a);
  case V_NEU: {
    Frame *fr = frame(c, F_APP, NULL, NULL);
    fr->arg = a;
    return reduce(c, push(c, f, fr));
  }
  case V_TYPE: case V_NAT_TYPE: case V_NAT: case V_PI: case V_SIGMA: case V_PAIR: case V_TUPLE:
  case V_UNIT: case V_PROD0: case V_PROD: case V_SUM: case V_INJ: break;
  }
  return fail(c, "TYPE_INTERNAL", c->loc, "an application of a value that is not a function");
}

static Value *apply(C *c, Value *f, Value *a) {
  if (!enter(c))
    return &c->bad;
  Value *v = apply_body(c, f, a);
  c->depth--;
  return v;
}

/* ---- conversion ---- */

static int applicable(const Value *v) {
  int partial = (v->kind == V_DATA || v->kind == V_CON) && v->nargs < v->global->arity;
  return v->kind == V_LAM || v->kind == V_NEU || partial;
}

static int projectable(const Value *v) {
  return v->kind == V_PAIR || v->kind == V_TUPLE || v->kind == V_NEU;
}

static int conv_apply(C *c, Value *a, Value *b, int lvl) {
  const Value *lam = a->kind == V_LAM ? a : b;
  Value *x = mk_var(c, lvl, lam->body->u.bind.binder.name);
  return conv(c, apply(c, a, x), apply(c, b, x), lvl + 1);
}

static int conv_closure(C *c, Value *a, Value *b, int lvl) {
  Value *x = mk_var(c, lvl, a->name);
  return conv(c, instantiate(c, a, x), instantiate(c, b, x), lvl + 1);
}

static int conv_args(C *c, const Value *a, const Value *b, int lvl) {
  size_t i = 0;
  if (a->nargs != b->nargs)
    return 0;
  while (i < a->nargs && conv(c, a->args[i], b->args[i], lvl))
    i++;
  return i == a->nargs;
}

static int conv_case_arm(C *c, const Frame *fa, const Frame *fb, int i, int lvl) {
  const CaseArm *x = &fa->ast->u.cases.arms[i];
  const CaseArm *y = &fb->ast->u.cases.arms[i];
  Value *v = mk_var(c, lvl, x->binder.name);
  return conv(c, eval(c, bind(c, fa->env, x->binder.name, v, NULL, 0), x->body),
              eval(c, bind(c, fb->env, y->binder.name, v, NULL, 0), y->body), lvl + 1);
}

static int conv_match_arm(C *c, const Frame *fa, const Frame *fb, const MatchArm *x, int lvl) {
  const MatchArm *y = find_arm(fb->ast, x->ctor);
  if (y == NULL || y->nvars != x->nvars)
    return 0;
  Value **vars = alloc(c, (x->nvars + 1) * sizeof *vars);
  for (size_t i = 0; i < x->nvars; i++)
    vars[i] = mk_var(c, lvl + (int)i, x->vars[i].name);
  return conv(c, eval(c, bind_args(c, fa->env, x, vars), x->body),
              eval(c, bind_args(c, fb->env, y, vars), y->body), lvl + (int)x->nvars);
}

static int conv_match(C *c, const Frame *fa, const Frame *fb, int lvl) {
  const Ast *s = fa->ast;
  size_t i = 0;
  if (!same(s->u.match.family, fb->ast->u.match.family) || s->u.match.narms != fb->ast->u.match.narms)
    return 0;
  while (i < s->u.match.narms && conv_match_arm(c, fa, fb, &s->u.match.arms[i], lvl))
    i++;
  return i == s->u.match.narms;
}

static int conv_frame(C *c, const Frame *fa, const Frame *fb, int lvl) {
  if (fa->kind != fb->kind)
    return 0;
  switch (fa->kind) {
  case F_APP: return conv(c, fa->arg, fb->arg, lvl);
  case F_PROJ: return fa->index == fb->index;
  case F_CASE: return conv_case_arm(c, fa, fb, 0, lvl) && conv_case_arm(c, fa, fb, 1, lvl);
  case F_MATCH: return conv_match(c, fa, fb, lvl);
  }
  return 0;
}

static int conv_frames(C *c, const Frame *fa, const Frame *fb, int lvl) {
  while (fa != NULL && fb != NULL && conv_frame(c, fa, fb, lvl)) {
    fa = fa->next;
    fb = fb->next;
  }
  return fa == NULL && fb == NULL;
}

static int conv_body(C *c, Value *a, Value *b, int lvl) {
  if (a->kind == V_LAM || b->kind == V_LAM)
    return applicable(a) && applicable(b) && conv_apply(c, a, b, lvl);
  if (a->kind == V_PAIR || b->kind == V_PAIR || a->kind == V_TUPLE || b->kind == V_TUPLE)
    return projectable(a) && projectable(b) && conv(c, do_proj(c, a, 0), do_proj(c, b, 0), lvl) &&
           conv(c, do_proj(c, a, 1), do_proj(c, b, 1), lvl);
  if (a->kind != b->kind)
    return 0;
  switch (a->kind) {
  case V_TYPE:
  case V_NAT: return a->nat == b->nat;
  case V_NAT_TYPE:
  case V_UNIT:
  case V_PROD0: return 1;
  case V_PI:
  case V_SIGMA: return a->erased == b->erased && conv(c, a->left, b->left, lvl) && conv_closure(c, a, b, lvl);
  case V_PROD:
  case V_SUM: return conv(c, a->left, b->left, lvl) && conv(c, a->right, b->right, lvl);
  case V_INJ: return a->nat == b->nat && conv(c, a->left, b->left, lvl);
  case V_DATA:
  case V_CON: return a->global == b->global && conv_args(c, a, b, lvl);
  case V_NEU: return a->level == b->level && a->global == b->global && conv_frames(c, a->spine, b->spine, lvl);
  case V_LAM:
  case V_PAIR:
  case V_TUPLE: return 0;
  }
  return 0;
}

static int conv(C *c, Value *a, Value *b, int lvl) {
  if (a == b)
    return !c->failed;
  if (!enter(c))
    return 0;
  int equal = conv_body(c, a, b, lvl);
  c->depth--;
  return equal && !c->failed;
}

/* ---- quotation ---- */

static Ast *node(C *c, AstKind kind) {
  Ast *t = alloc(c, sizeof *t);
  t->kind = kind;
  t->depth = 1;
  return t;
}

static Ast *var_node(C *c, const char *name) {
  Ast *t = node(c, AST_VAR);
  t->u.name = name != NULL ? name : "_";
  return t;
}

static Ast *app_node(C *c, Ast *fun, Ast *arg) {
  Ast *t = node(c, AST_APP);
  t->u.app.fun = fun;
  t->u.app.arg = arg;
  return t;
}

static Ast *two_node(C *c, AstKind kind, Value *v, int lvl) {
  Ast *t = node(c, kind);
  t->u.pair.left = quote(c, v->left, lvl);
  t->u.pair.right = quote(c, v->right, lvl);
  return t;
}

/* Print variables by their semantic level, never by a closure's source
 * spelling. Each level has a distinct name, also distinct from globals. */
static const char *quote_name(C *c, int lvl) {
  char name[64];
  unsigned suffix = 0;
  do {
    snprintf(name, sizeof name, "anchorq%dx%u", lvl, suffix++);
  } while (find_global(c, name) != NULL);
  size_t size = strlen(name) + 1;
  char *out = alloc(c, size);
  memcpy(out, name, size);
  return out;
}

static Ast *quote_bind(C *c, Value *v, int lvl) {
  Ast *t = node(c, v->kind == V_PI ? AST_PI : AST_SIGMA);
  t->u.bind.binder.name = v->name == NULL ? NULL : quote_name(c, lvl);
  t->u.bind.binder.erased = v->erased;
  t->u.bind.binder.type = quote(c, v->left, lvl);
  t->u.bind.body = quote(c, instantiate(c, v, mk_var(c, lvl, v->name)), lvl + 1);
  return t;
}

static Ast *quote_lam(C *c, Value *v, int lvl) {
  const Ast *lam = v->body;
  Ast *t = node(c, AST_LAM);
  t->u.bind.binder = lam->u.bind.binder;
  t->u.bind.binder.name = quote_name(c, lvl);
  t->u.bind.binder.type = quote(c, eval(c, v->env, lam->u.bind.binder.type), lvl);
  t->u.bind.body = quote(c, apply(c, v, mk_var(c, lvl, lam->u.bind.binder.name)), lvl + 1);
  return t;
}

static Ast *quote_args(C *c, Value *v, int lvl) {
  Ast *t = var_node(c, v->global->name);
  for (size_t i = 0; i < v->nargs; i++)
    t = app_node(c, t, quote(c, v->args[i], lvl));
  return t;
}

static void quote_case_arm(C *c, CaseArm *arm, Bind *env, int lvl) {
  const char *name = arm->binder.name;
  arm->binder.name = quote_name(c, lvl);
  arm->binder.type = quote(c, eval(c, env, arm->binder.type), lvl);
  arm->body = quote(c, eval(c, bind(c, env, name, mk_var(c, lvl, name), NULL, 0), arm->body), lvl + 1);
}

static Ast *quote_match(C *c, Ast *subject, const Frame *f, int lvl) {
  Ast *t = node(c, AST_MATCH);
  size_t n = f->ast->u.match.narms;
  MatchArm *arms = alloc(c, (n + 1) * sizeof *arms);
  if (c->failed)
    return &c->bad_ast;
  *t = *f->ast;
  t->u.match.subject = subject;
  t->u.match.arms = arms;
  /* The motive is a closure too: instantiate its indices and subject in
   * the captured environment before printing it under fresh binders. */
  size_t ni = t->u.match.nindices;
  const char **indices = alloc(c, (ni + 1) * sizeof *indices);
  Bind *motive = f->env;
  for (size_t i = 0; i < ni; i++) {
    indices[i] = quote_name(c, lvl + (int)i);
    motive = bind(c, motive, f->ast->u.match.indices[i],
                  mk_var(c, lvl + (int)i, indices[i]), NULL, 0);
  }
  t->u.match.indices = indices;
  t->u.match.self = quote_name(c, lvl + (int)ni);
  motive = bind(c, motive, f->ast->u.match.self,
                mk_var(c, lvl + (int)ni, t->u.match.self), NULL, 0);
  t->u.match.motive = quote(c, eval(c, motive, f->ast->u.match.motive), lvl + (int)ni + 1);
  for (size_t i = 0; i < n; i++) {
    const MatchArm *arm = &f->ast->u.match.arms[i];
    Value **vars = alloc(c, (arm->nvars + 1) * sizeof *vars);
    for (size_t k = 0; k < arm->nvars; k++)
      vars[k] = mk_var(c, lvl + (int)k, arm->vars[k].name);
    arms[i] = *arm;
    arms[i].vars = alloc(c, (arm->nvars + 1) * sizeof *arms[i].vars);
    for (size_t k = 0; k < arm->nvars; k++) {
      arms[i].vars[k] = arm->vars[k];
      arms[i].vars[k].name = quote_name(c, lvl + (int)k);
    }
    arms[i].body = quote(c, eval(c, bind_args(c, f->env, arm, vars), arm->body), lvl + (int)arm->nvars);
  }
  return t;
}

static Ast *quote_frame(C *c, Ast *inner, const Frame *f, int lvl) {
  switch (f->kind) {
  case F_APP: return app_node(c, inner, quote(c, f->arg, lvl));
  case F_PROJ: {
    Ast *t = node(c, AST_PROJ);
    t->u.proj.term = inner;
    t->u.proj.index = f->index;
    return t;
  }
  case F_CASE: {
    Ast *t = node(c, AST_CASE);
    if (c->failed)
      return &c->bad_ast;
    *t = *f->ast;
    t->u.cases.subject = inner;
    quote_case_arm(c, &t->u.cases.arms[0], f->env, lvl);
    quote_case_arm(c, &t->u.cases.arms[1], f->env, lvl);
    return t;
  }
  case F_MATCH: return quote_match(c, inner, f, lvl);
  }
  return &c->bad_ast;
}

static Ast *quote_spine(C *c, const Value *v, const Frame *f, int lvl) {
  if (f == NULL)
    return var_node(c, v->global != NULL ? v->global->name : quote_name(c, v->level));
  return quote_frame(c, quote_spine(c, v, f->next, lvl), f, lvl);
}

static Ast *quote_body(C *c, Value *v, int lvl) {
  switch (v->kind) {
  case V_TYPE: {
    Ast *t = node(c, AST_TYPE);
    t->u.level = (unsigned)v->nat;
    return t;
  }
  case V_NAT_TYPE: return var_node(c, "Nat");
  case V_NAT: {
    Ast *t = node(c, AST_NAT);
    t->u.nat = v->nat;
    return t;
  }
  case V_PI:
  case V_SIGMA: return quote_bind(c, v, lvl);
  case V_LAM: return quote_lam(c, v, lvl);
  case V_PAIR: return two_node(c, AST_PAIR, v, lvl);
  case V_TUPLE: return two_node(c, AST_TUPLE, v, lvl);
  case V_UNIT: return node(c, AST_TUPLE0);
  case V_PROD0: return node(c, AST_PROD0);
  case V_PROD: return two_node(c, AST_PROD, v, lvl);
  case V_SUM: return two_node(c, AST_SUM, v, lvl);
  case V_INJ: {
    Ast *t = node(c, AST_INJ);
    t->u.inj.tag = (unsigned)v->nat;
    t->u.inj.arity = 2;
    t->u.inj.value = quote(c, v->left, lvl);
    return t;
  }
  case V_DATA:
  case V_CON: return quote_args(c, v, lvl);
  case V_NEU: return quote_spine(c, v, v->spine, lvl);
  }
  return &c->bad_ast;
}

static Ast *quote(C *c, Value *v, int lvl) {
  if (!enter(c))
    return &c->bad_ast;
  Ast *t = quote_body(c, v, lvl);
  c->depth--;
  return t;
}

/* ---- checking ---- */

static void mismatch(C *c, Loc loc, Value *want, Value *got, int lvl) {
  Ast *expected = quote(c, want, lvl);
  Ast *found = quote(c, got, lvl);
  if (c->failed)
    return;
  fail(c, "TYPE_MISMATCH", loc, "the types differ:");
  c->diag->expected = expected;
  c->diag->found = found;
}

static unsigned long long sort_of(C *c, Bind *ctx, int lvl, const Ast *t) {
  Value *s = infer(c, ctx, lvl, t, 0);
  if (!c->failed && s->kind != V_TYPE)
    fail(c, "TYPE_SHAPE", t->loc, "a type is expected here");
  return c->failed ? 0 : s->nat;
}

static Value *infer_var(C *c, Bind *ctx, const Ast *t, int relevant) {
  Bind *b = find_local(ctx, t->u.name);
  Global *g = find_global(c, t->u.name);
  if (b != NULL && relevant && b->erased)
    return fail(c, "TYPE_ERASED", t->loc, "%s is erased and is used at run time", t->u.name);
  if (b != NULL)
    return b->type;
  if (g == NULL)
    return fail(c, "TYPE_SCOPE", t->loc, "%s is not in scope", t->u.name);
  return g->type;
}

static Value *infer_bind(C *c, Bind *ctx, int lvl, const Ast *t) {
  const Binder *b = &t->u.bind.binder;
  unsigned long long domain = sort_of(c, ctx, lvl, b->type);
  Bind *inner = bind(c, ctx, b->name, mk_var(c, lvl, b->name), eval(c, ctx, b->type), b->erased);
  unsigned long long body = sort_of(c, inner, lvl + 1, t->u.bind.body);
  return mk_type(c, domain > body ? domain : body);
}

static Value *infer_app(C *c, Bind *ctx, int lvl, const Ast *t, int relevant) {
  Value *f = infer(c, ctx, lvl, t->u.app.fun, relevant);
  if (!c->failed && f->kind != V_PI)
    return fail(c, "TYPE_SHAPE", t->loc, "the head of an application is not a function");
  check(c, ctx, lvl, t->u.app.arg, f->left, relevant && !f->erased);
  return instantiate(c, f, eval(c, ctx, t->u.app.arg));
}

static Value *infer_proj(C *c, Bind *ctx, int lvl, const Ast *t, int relevant) {
  Value *ty = infer(c, ctx, lvl, t->u.proj.term, relevant);
  unsigned i = t->u.proj.index;
  if (c->failed)
    return &c->bad;
  if (i > 1 || (ty->kind != V_PROD && ty->kind != V_SIGMA))
    return fail(c, "TYPE_SHAPE", t->loc, "a projection .%u of a term that is not a pair", i);
  if (relevant && ty->kind == V_SIGMA && ty->erased && i == 0)
    return fail(c, "TYPE_ERASED", t->loc, "the first field of this pair is erased and is used at run time");
  if (ty->kind == V_PROD || i == 0)
    return i == 0 ? ty->left : ty->right;
  return instantiate(c, ty, do_proj(c, eval(c, ctx, t->u.proj.term), 0));
}

static Value *motive_at(C *c, Bind *env, const Ast *t, Value **indices, Value *self) {
  if (c->failed)
    return &c->bad;
  for (size_t i = 0; i < t->u.match.nindices; i++)
    env = bind(c, env, t->u.match.indices[i], indices[i], NULL, 0);
  return eval(c, bind(c, env, t->u.match.self, self, NULL, 0), t->u.match.motive);
}

static void check_motive(C *c, Bind *ctx, int lvl, const Ast *t, Global *fam) {
  Value *ty = fam->type;
  Value **indices = alloc(c, (fam->arity + 1) * sizeof *indices);
  for (size_t i = 0; i < fam->arity; i++) {
    const char *name = t->u.match.indices[i];
    indices[i] = mk_var(c, lvl, name);
    ctx = bind(c, ctx, name, indices[i], ty->left, 1);
    ty = instantiate(c, ty, indices[i]);
    lvl++;
  }
  Value *self = mk_global(c, V_DATA, fam);
  self->args = indices;
  self->nargs = fam->arity;
  ctx = bind(c, ctx, t->u.match.self, mk_var(c, lvl, t->u.match.self), self, 1);
  sort_of(c, ctx, lvl + 1, t->u.match.motive);
}

static void check_match_arm(C *c, Bind *ctx, int lvl, const Ast *t, const MatchArm *arm, Global *fam,
                            int relevant) {
  Global *ctor = find_global(c, arm->ctor);
  if (ctor == NULL || ctor->kind != G_CTOR || ctor->family != fam || find_arm(t, arm->ctor) != arm ||
      arm->nvars != ctor->arity) {
    fail(c, "TYPE_MATCH", arm->loc, "the arm %s is not one constructor of %s with its fields", arm->ctor,
         fam->name);
    return;
  }
  Value *ty = ctor->type;
  Value **args = alloc(c, (arm->nvars + 1) * sizeof *args);
  Bind *inner = ctx;
  int l = lvl;
  for (size_t k = 0; k < arm->nvars; k++) {
    const Pattern *p = &arm->vars[k];
    args[k] = mk_var(c, l++, p->name);
    inner = bind(c, inner, p->name, args[k], ty->left, p->erased || ty->erased);
    ty = instantiate(c, ty, args[k]);
  }
  if (c->failed)
    return;
  if (ty->kind != V_DATA || ty->nargs != fam->arity) {
    fail(c, "TYPE_INTERNAL", arm->loc, "%s does not end in %s", arm->ctor, fam->name);
    return;
  }
  Value *con = mk_global(c, V_CON, ctor);
  con->args = args;
  con->nargs = arm->nvars;
  check(c, inner, l, arm->body, motive_at(c, ctx, t, ty->args, con), relevant);
}

static Value *infer_match(C *c, Bind *ctx, int lvl, const Ast *t, int relevant) {
  Value *st = infer(c, ctx, lvl, t->u.match.subject, relevant);
  Global *fam = find_global(c, t->u.match.family);
  if (c->failed)
    return &c->bad;
  if (fam == NULL || fam->kind != G_MU || st->kind != V_DATA || st->global != fam ||
      t->u.match.nindices != fam->arity || st->nargs != fam->arity)
    return fail(c, "TYPE_MATCH", t->loc, "the subject is not of the family %s with %zu indices",
                t->u.match.family, t->u.match.nindices);
  if (t->u.match.narms != fam->decl->nctors)
    return fail(c, "TYPE_MATCH", t->loc, "the match has %zu arms and %s has %zu constructors",
                t->u.match.narms, fam->name, fam->decl->nctors);
  check_motive(c, ctx, lvl, t, fam);
  for (size_t i = 0; i < t->u.match.narms; i++)
    check_match_arm(c, ctx, lvl, t, &t->u.match.arms[i], fam, relevant);
  return motive_at(c, ctx, t, st->args, eval(c, ctx, t->u.match.subject));
}

static Value *infer_body(C *c, Bind *ctx, int lvl, const Ast *t, int relevant) {
  switch (t->kind) {
  case AST_VAR: return infer_var(c, ctx, t, relevant);
  case AST_NAT: return mk(c, V_NAT_TYPE);
  case AST_TYPE:
    return t->u.level == 0 ? mk_type(c, 1) : fail(c, "TYPE_UNIVERSE", t->loc, "Type 1 has no type");
  case AST_PI:
  case AST_SIGMA: return infer_bind(c, ctx, lvl, t);
  case AST_APP: return infer_app(c, ctx, lvl, t, relevant);
  case AST_TUPLE0: return mk(c, V_PROD0);
  case AST_TUPLE:
    return mk_two(c, V_PROD, infer(c, ctx, lvl, t->u.pair.left, relevant),
                  infer(c, ctx, lvl, t->u.pair.right, relevant));
  case AST_PROD0: return mk_type(c, 0);
  case AST_PROD:
  case AST_SUM: {
    unsigned long long a = sort_of(c, ctx, lvl, t->u.pair.left);
    unsigned long long b = sort_of(c, ctx, lvl, t->u.pair.right);
    return mk_type(c, a > b ? a : b);
  }
  case AST_MATCH: return infer_match(c, ctx, lvl, t, relevant);
  case AST_PROJ: return infer_proj(c, ctx, lvl, t, relevant);
  case AST_LAM:
  case AST_PAIR:
  case AST_INJ:
  case AST_CASE: break;
  }
  return fail(c, "TYPE_INFER", t->loc, "this term needs its type from the context");
}

static Value *infer(C *c, Bind *ctx, int lvl, const Ast *t, int relevant) {
  if (!enter(c))
    return &c->bad;
  Value *v = infer_body(c, ctx, lvl, t, relevant);
  c->depth--;
  return v;
}

/* The binder type of a fun or of a case arm must be the expected domain. */
static Value *binder_domain(C *c, Bind *ctx, int lvl, const Binder *b, Value *want) {
  sort_of(c, ctx, lvl, b->type);
  Value *domain = eval(c, ctx, b->type);
  if (!c->failed && !conv(c, domain, want, lvl))
    mismatch(c, b->loc, want, domain, lvl);
  return want;
}

static void check_lam(C *c, Bind *ctx, int lvl, const Ast *t, Value *want, int relevant) {
  const Binder *b = &t->u.bind.binder;
  if (want->kind != V_PI) {
    fail(c, "TYPE_SHAPE", t->loc, "a fun needs a function type");
    return;
  }
  if (b->erased != want->erased) {
    fail(c, "TYPE_ERASED", b->loc, "the erasure of %s differs from its type", b->name);
    return;
  }
  Value *domain = binder_domain(c, ctx, lvl, b, want->left);
  Value *x = mk_var(c, lvl, b->name);
  check(c, bind(c, ctx, b->name, x, domain, b->erased), lvl + 1, t->u.bind.body, instantiate(c, want, x),
        relevant);
}

static void check_case_arm(C *c, Bind *ctx, int lvl, const CaseArm *arm, Value *leg, Value *want, int relevant) {
  const Binder *b = &arm->binder;
  Value *domain = binder_domain(c, ctx, lvl, b, leg);
  check(c, bind(c, ctx, b->name, mk_var(c, lvl, b->name), domain, b->erased), lvl + 1, arm->body, want,
        relevant);
}

static int subtype(C *c, Value *got, Value *want, int lvl) {
  if (got->kind == V_TYPE && want->kind == V_TYPE)
    return got->nat <= want->nat;
  return conv(c, got, want, lvl);
}

static void check_body(C *c, Bind *ctx, int lvl, const Ast *t, Value *want, int relevant) {
  switch (t->kind) {
  case AST_LAM:
    check_lam(c, ctx, lvl, t, want, relevant);
    return;
  case AST_PAIR:
    if (want->kind != V_SIGMA) {
      fail(c, "TYPE_SHAPE", t->loc, "a pair needs a Sigma type");
      return;
    }
    check(c, ctx, lvl, t->u.pair.left, want->left, relevant && !want->erased);
    check(c, ctx, lvl, t->u.pair.right, instantiate(c, want, eval(c, ctx, t->u.pair.left)), relevant);
    return;
  case AST_TUPLE:
    if (want->kind != V_PROD) {
      fail(c, "TYPE_SHAPE", t->loc, "a tuple needs a prod type");
      return;
    }
    check(c, ctx, lvl, t->u.pair.left, want->left, relevant);
    check(c, ctx, lvl, t->u.pair.right, want->right, relevant);
    return;
  case AST_INJ:
    if (want->kind != V_SUM || t->u.inj.arity != 2) {
      fail(c, "TYPE_SHAPE", t->loc, "an inj needs a sum type of 2 legs");
      return;
    }
    check(c, ctx, lvl, t->u.inj.value, t->u.inj.tag == 0 ? want->left : want->right, relevant);
    return;
  case AST_CASE: {
    Value *s = infer(c, ctx, lvl, t->u.cases.subject, relevant);
    if (!c->failed && s->kind != V_SUM) {
      fail(c, "TYPE_SHAPE", t->loc, "a case needs a subject of a sum type");
      return;
    }
    check_case_arm(c, ctx, lvl, &t->u.cases.arms[0], s->left, want, relevant);
    check_case_arm(c, ctx, lvl, &t->u.cases.arms[1], s->right, want, relevant);
    return;
  }
  case AST_VAR: case AST_NAT: case AST_TYPE: case AST_PI: case AST_SIGMA: case AST_APP: case AST_TUPLE0:
  case AST_PROD0: case AST_PROD: case AST_SUM: case AST_MATCH: case AST_PROJ: break;
  }
  Value *got = infer(c, ctx, lvl, t, relevant);
  if (!c->failed && !subtype(c, got, want, lvl))
    mismatch(c, t->loc, want, got, lvl);
}

static void check(C *c, Bind *ctx, int lvl, const Ast *t, Value *want, int relevant) {
  if (!enter(c))
    return;
  /* A term checked against a universe constructs a type, so its inputs
   * have no run-time use even when it is the body of a definition. */
  check_body(c, ctx, lvl, t, want, relevant && want->kind != V_TYPE);
  c->depth--;
}

/* ---- the structural check of a def rec ---- */

static Names *push_name(C *c, Names *next, const char *name, int smaller) {
  Names *n = alloc(c, sizeof *n);
  n->name = name;
  n->smaller = smaller;
  n->next = next;
  return n;
}

static const Names *lookup(const Names *scope, const char *name) {
  while (scope != NULL && !same(scope->name, name))
    scope = scope->next;
  return scope;
}

static void walk(C *c, const Global *g, const Ast *t, Names *scope);

static void walk_arm(C *c, const Global *g, const MatchArm *arm, Names *scope, int smaller) {
  for (size_t i = 0; i < arm->nvars; i++)
    scope = push_name(c, scope, arm->vars[i].name, smaller);
  walk(c, g, arm->body, scope);
}

static void walk_match(C *c, const Global *g, const Ast *t, Names *scope, int smaller) {
  Names *motive = push_name(c, scope, t->u.match.self, 0);
  for (size_t i = 0; i < t->u.match.nindices; i++)
    motive = push_name(c, motive, t->u.match.indices[i], 0);
  walk(c, g, t->u.match.subject, scope);
  walk(c, g, t->u.match.motive, motive);
  for (size_t i = 0; i < t->u.match.narms; i++)
    walk_arm(c, g, &t->u.match.arms[i], scope, smaller);
}

/* A call of the def rec has its arity in arguments, and the decreasing one
 * is a pattern variable of the top match. */
static void walk_app(C *c, const Global *g, const Ast *t, Names *scope) {
  size_t n = 0;
  const Ast *head = t;
  for (; head->kind == AST_APP; head = head->u.app.fun)
    n++;
  int call = head->kind == AST_VAR && same(head->u.name, g->name) && lookup(scope, g->name) == NULL;
  const Ast *arg = t;
  for (size_t k = 0; call && n >= g->arity && k + 1 + g->decreasing < n; k++)
    arg = arg->u.app.fun;
  const Names *var = arg->u.app.arg->kind == AST_VAR ? lookup(scope, arg->u.app.arg->u.name) : NULL;
  if (call && (n < g->arity || var == NULL || !var->smaller))
    fail(c, "TYPE_REC", t->loc, "%s recurs without %zu arguments and a smaller one", g->name, g->arity);
  for (const Ast *a = t; a->kind == AST_APP; a = a->u.app.fun)
    walk(c, g, a->u.app.arg, scope);
  if (!call)
    walk(c, g, head, scope);
}

static void walk(C *c, const Global *g, const Ast *t, Names *scope) {
  if (c->failed)
    return;
  switch (t->kind) {
  case AST_VAR:
    if (same(t->u.name, g->name) && lookup(scope, g->name) == NULL)
      fail(c, "TYPE_REC", t->loc, "%s recurs without its arguments", g->name);
    return;
  case AST_NAT: case AST_TYPE: case AST_TUPLE0: case AST_PROD0: return;
  case AST_PI:
  case AST_SIGMA:
  case AST_LAM:
    walk(c, g, t->u.bind.binder.type, scope);
    walk(c, g, t->u.bind.body, push_name(c, scope, t->u.bind.binder.name, 0));
    return;
  case AST_APP: walk_app(c, g, t, scope); return;
  case AST_PAIR:
  case AST_TUPLE:
  case AST_PROD:
  case AST_SUM:
    walk(c, g, t->u.pair.left, scope);
    walk(c, g, t->u.pair.right, scope);
    return;
  case AST_INJ: walk(c, g, t->u.inj.value, scope); return;
  case AST_PROJ: walk(c, g, t->u.proj.term, scope); return;
  case AST_CASE:
    walk(c, g, t->u.cases.subject, scope);
    for (int i = 0; i < 2; i++)
      walk(c, g, t->u.cases.arms[i].binder.type, scope);
    for (int i = 0; i < 2; i++)
      walk(c, g, t->u.cases.arms[i].body, push_name(c, scope, t->u.cases.arms[i].binder.name, 0));
    return;
  case AST_MATCH: walk_match(c, g, t, scope, 0); return;
  }
}

static void structural(C *c, Global *g, const Decl *d) {
  const Ast *body = d->body;
  for (; body->kind == AST_LAM; body = body->u.bind.body)
    ;
  int top = body->kind == AST_MATCH && body->u.match.subject->kind == AST_VAR;
  const char *subject = top ? body->u.match.subject->u.name : NULL;
  Names *scope = NULL;
  size_t n = 0;
  int found = 0;
  for (const Ast *t = d->body; t->kind == AST_LAM; t = t->u.bind.body, n++) {
    int hit = same(t->u.bind.binder.name, subject);
    g->decreasing = hit ? n : g->decreasing;
    found = found || hit;
    scope = push_name(c, scope, t->u.bind.binder.name, 0);
  }
  g->arity = n;
  if (!found) {
    fail(c, "TYPE_REC", d->loc, "def rec %s is not fun ... => match on one of its parameters", d->name);
    return;
  }
  walk_match(c, g, body, scope, 1);
}

/* ---- declarations ---- */

static Global *add_global(C *c, const char *name, GlobalKind kind, Value *type, int prelude) {
  Global *g = alloc(c, sizeof *g);
  g->name = name;
  g->kind = kind;
  g->type = type;
  g->prelude = prelude;
  g->next = c->globals;
  if (!c->failed)
    c->globals = g;
  return g;
}

static Value *closed_type(C *c, const Ast *t) {
  sort_of(c, NULL, 0, t);
  return eval(c, NULL, t);
}

static int occurs(const char *name, const Ast *t);

static int occurs_match(const char *name, const Ast *t) {
  int found = occurs(name, t->u.match.subject) || occurs(name, t->u.match.motive);
  for (size_t i = 0; i < t->u.match.narms; i++)
    found = found || occurs(name, t->u.match.arms[i].body);
  return found;
}

static int occurs(const char *name, const Ast *t) {
  switch (t->kind) {
  case AST_VAR: return same(t->u.name, name);
  case AST_NAT: case AST_TYPE: case AST_TUPLE0: case AST_PROD0: return 0;
  case AST_PI:
  case AST_SIGMA:
  case AST_LAM: return occurs(name, t->u.bind.binder.type) || occurs(name, t->u.bind.body);
  case AST_APP: return occurs(name, t->u.app.fun) || occurs(name, t->u.app.arg);
  case AST_PAIR:
  case AST_TUPLE:
  case AST_PROD:
  case AST_SUM: return occurs(name, t->u.pair.left) || occurs(name, t->u.pair.right);
  case AST_INJ: return occurs(name, t->u.inj.value);
  case AST_PROJ: return occurs(name, t->u.proj.term);
  case AST_CASE:
    return occurs(name, t->u.cases.subject) || occurs(name, t->u.cases.arms[0].binder.type) ||
           occurs(name, t->u.cases.arms[0].body) || occurs(name, t->u.cases.arms[1].binder.type) ||
           occurs(name, t->u.cases.arms[1].body);
  case AST_MATCH: return occurs_match(name, t);
  }
  return 1;
}

/* The number of arguments of NAME a b ..., when no argument mentions NAME;
 * SIZE_MAX otherwise. */
static size_t spine_of(const char *name, const Ast *t) {
  size_t n = 0;
  int clean = 1;
  for (; t->kind == AST_APP; t = t->u.app.fun, n++)
    clean = clean && !occurs(name, t->u.app.arg);
  return clean && t->kind == AST_VAR && same(t->u.name, name) ? n : SIZE_MAX;
}

static Bind *check_field(C *c, const Global *fam, Bind *ctx, int lvl, const Binder *b, unsigned level) {
  unsigned long long sort = sort_of(c, ctx, lvl, b->type);
  if (!c->failed && occurs(fam->name, b->type) && spine_of(fam->name, b->type) == SIZE_MAX)
    fail(c, "TYPE_MU", b->loc, "%s occurs in a field in a position that is not strictly positive", fam->name);
  if (!c->failed && sort > level)
    fail(c, "TYPE_MU", b->loc, "a field of %s lives in Type %llu, above its family", fam->name, sort);
  return bind(c, ctx, b->name, mk_var(c, lvl, b->name), eval(c, ctx, b->type), b->erased);
}

static void check_ctor(C *c, Global *fam, const Ctor *k, size_t index, unsigned level) {
  if (find_global(c, k->name) != NULL) {
    fail(c, "TYPE_DUPLICATE", k->loc, "%s is declared already", k->name);
    return;
  }
  Bind *ctx = NULL;
  int lvl = 0;
  const Ast *t = k->type;
  for (; t->kind == AST_PI && !c->failed; t = t->u.bind.body, lvl++)
    ctx = check_field(c, fam, ctx, lvl, &t->u.bind.binder, level);
  if (!c->failed && spine_of(fam->name, t) != fam->arity) {
    fail(c, "TYPE_MU", k->loc, "%s does not end in %s with %zu indices", k->name, fam->name, fam->arity);
    return;
  }
  sort_of(c, ctx, lvl, t);
  Global *g = add_global(c, k->name, G_CTOR, eval(c, NULL, k->type), 1);
  g->family = fam;
  g->index = index;
  g->arity = (size_t)lvl;
  g->value = mk_global(c, V_CON, g);
}

static void check_mu(C *c, const Decl *d) {
  const Ast *t = d->type;
  size_t arity = 0;
  for (; t->kind == AST_PI; t = t->u.bind.body)
    arity++;
  if (t->kind != AST_TYPE) {
    fail(c, "TYPE_MU", d->loc, "the type of mu %s does not end in Type", d->name);
    return;
  }
  Global *fam = add_global(c, d->name, G_MU, closed_type(c, d->type), 1);
  fam->decl = d;
  fam->arity = arity;
  fam->value = mk_global(c, V_DATA, fam);
  for (size_t i = 0; i < d->nctors && !c->failed; i++)
    check_ctor(c, fam, &d->ctors[i], i, t->u.level);
}

static void check_def(C *c, const Decl *d, int prelude) {
  Value *type = closed_type(c, d->type);
  check(c, NULL, 0, d->body, type, 1);
  Global *g = add_global(c, d->name, G_DEF, type, prelude);
  g->decl = d;
  g->value = eval(c, NULL, d->body);
}

static void check_rec(C *c, const Decl *d) {
  Global *g = add_global(c, d->name, G_REC, closed_type(c, d->type), 1);
  g->decl = d;
  g->value = mk_global(c, V_NEU, g);
  structural(c, g, d);
  check(c, NULL, 0, d->body, g->type, 1);
  g->body = eval(c, NULL, d->body);
}

static void check_decl(C *c, const Program *p, size_t i, int prelude) {
  const Decl *d = &p->decls[i];
  if (c->failed)
    return;
  c->file = p->file;
  c->def = span_of(d->name);
  c->loc = d->loc;
  c->fuel = CHECK_FUEL;
  if (!prelude && d->kind == DECL_MU) {
    fail(c, "REFUSE_DATA", d->loc, "a program may not declare mu %s; mu lives in the prelude", d->name);
    return;
  }
  if (!prelude && d->kind == DECL_REC) {
    fail(c, "REFUSE_REC", d->loc, "a program may not declare def rec %s; recursion lives in the prelude",
         d->name);
    return;
  }
  Global *old = find_global(c, d->name);
  if (old != NULL) {
    fail(c, old->prelude && !prelude ? "REFUSE_NAME" : "TYPE_DUPLICATE", d->loc,
         "%s is declared already", d->name);
    return;
  }
  switch (d->kind) {
  case DECL_DEF: check_def(c, d, prelude); return;
  case DECL_REC: check_rec(c, d); return;
  case DECL_MU: check_mu(c, d); return;
  }
}

static void prim_global(C *c, const char *name, Prim p, Value *type) {
  Global *g = add_global(c, name, G_PRIM, type, 1);
  g->prim = p;
  g->arity = 2;
  g->value = mk_global(c, V_NEU, g);
}

/* SPEC prelude note P1: an opaque core name. It has no literal, no
 * projection, no eliminator and no match, and it never reduces. */
static Value *opaque_global(C *c, const char *name, Value *type) {
  Global *g = add_global(c, name, G_OPAQUE, type, 1);
  g->value = mk_global(c, V_NEU, g);
  return g->value;
}

static void builtins(C *c) {
  Value *nat = mk(c, V_NAT_TYPE);
  Value *unit = mk(c, V_PROD0);
  Value *truth = mk_two(c, V_SUM, unit, unit);
  Global *g = add_global(c, "Nat", G_DEF, mk_type(c, 0), 1);
  g->value = nat;
  prim_global(c, "natAdd", P_ADD, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, nat)));
  prim_global(c, "natSub", P_SUB, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, nat)));
  prim_global(c, "natEq", P_EQ, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, truth)));
  prim_global(c, "natLt", P_LT, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, truth)));
  Value *hash = opaque_global(c, "Hash", mk_type(c, 0));
  Value *time = opaque_global(c, "Time", mk_type(c, 0));
  Value *log = opaque_global(c, "AnchorLog", mk_type(c, 0));
  opaque_global(c, "logMember", mk_two(c, V_PI, hash, mk_two(c, V_PI, time, mk_two(c, V_PI, log, truth))));
  opaque_global(c, "logInsert", mk_two(c, V_PI, hash, mk_two(c, V_PI, time, mk_two(c, V_PI, log, log))));
  opaque_global(c, "hashNonZero", mk_two(c, V_PI, hash, truth));
}

static int members_ok(C *c, const Program *p) {
  const Decl *d = p->ndecls > 0 ? &p->decls[0] : NULL;
  int ok = d != NULL && d->kind == DECL_DEF && same(d->name, "members") && d->type->kind == AST_VAR &&
           same(d->type->u.name, "Nat") && d->body->kind == AST_NAT && d->body->u.nat >= 1 &&
           d->body->u.nat <= UINT_MAX;
  Loc start = {1, 1};
  if (!ok)
    fail(c, "REFUSE_MEMBERS", d != NULL ? d->loc : start,
         "the first declaration must be def members : Nat := N with N >= 1");
  return ok;
}

/* The required program definition NAME of type WANT, or NULL after an
 * error: TYPE_SCOPE if the program does not define it, TYPE_MISMATCH with
 * both normal forms if its type differs. */
static Global *required(C *c, const char *name, Value *want) {
  Global *g = find_global(c, name);
  if (c->failed)
    return NULL;
  if (g == NULL || g->prelude) {
    refuse(c, "TYPE_SCOPE", name, "is not declared; a program defines members, candidates and rule");
    return NULL;
  }
  c->def = span_of(g->name);
  c->loc = g->decl->loc;
  if (!conv(c, want, g->type, 0))
    mismatch(c, g->decl->loc, want, g->type, 0);
  return c->failed ? NULL : g;
}

/* V with the frames from NEWEST down to STOP (not STOP) applied, oldest
 * first. */
static Value *replay(C *c, Value *v, const Frame *newest, const Frame *stop) {
  if (c->failed || newest == stop)
    return v;
  Value *inner = replay(c, v, newest->next, stop);
  switch (newest->kind) {
  case F_APP: return apply(c, inner, newest->arg);
  case F_PROJ: return do_proj(c, inner, newest->index);
  case F_CASE: return do_case(c, inner, newest->env, newest->ast);
  case F_MATCH: return do_match(c, inner, newest->env, newest->ast);
  }
  return inner;
}

static void forks(C *c, Value *v, int lvl, const char *which);

/* Each arm of the stuck case or match F of V, under fresh variables and
 * with the newer frames of V applied. */
static void fork_arms(C *c, Value *v, const Frame *f, int lvl, const char *which) {
  const Ast *t = f->ast;
  if (f->kind == F_CASE) {
    for (size_t i = 0; i < 2 && !c->failed; i++) {
      const CaseArm *arm = &t->u.cases.arms[i];
      const char *name = arm->binder.name;
      Value *r = eval(c, bind(c, f->env, name, mk_var(c, lvl, name), NULL, 0), arm->body);
      forks(c, replay(c, r, v->spine, f), lvl + 1, which);
    }
    return;
  }
  for (size_t i = 0; i < t->u.match.narms && !c->failed; i++) {
    const MatchArm *arm = &t->u.match.arms[i];
    Value **vars = alloc(c, (arm->nvars + 1) * sizeof *vars);
    for (size_t k = 0; k < arm->nvars && !c->failed; k++)
      vars[k] = mk_var(c, lvl + (int)k, arm->vars[k].name);
    Value *r = eval(c, bind_args(c, f->env, arm, vars), arm->body);
    forks(c, replay(c, r, v->spine, f), lvl + (int)arm->nvars, which);
  }
}

/* V, an outcome of rule under its binder t (or of a constitution of
 * amendments under its binder x; c->fork_def names it), has no two p q with a side
 * that is not frozen. When WHICH names a policy side, V is its freeze
 * flag instead. Explore stuck case/match arms in both outcomes and flags,
 * including the frames after each branch. An unresolved leaf is refused. */
static void forks(C *c, Value *v, int lvl, const char *which) {
  if (!enter(c))
    return;
  const Frame *branch = NULL;
  for (const Frame *f = v->kind == V_NEU ? v->spine : NULL; f != NULL; f = f->next)
    branch = f->kind == F_CASE || f->kind == F_MATCH ? f : branch;
  if (which != NULL) {
    if (v->kind == V_INJ && v->nat == 0)
      fail(c, "REFUSE_FORK", c->loc, "policyForkFreeze %s of a two p q outcome is flagNo", which);
    else if (v->kind != V_INJ && branch == NULL)
      fail(c, "REFUSE_FORK", c->loc,
           "policyForkFreeze %s of a two p q outcome does not normalize to a closed flag", which);
  } else {
    if (v->kind == V_CON && v->global == find_global(c, "two") && v->nargs == 2) {
      Value *freeze = find_global(c, "policyForkFreeze")->value;
      forks(c, apply(c, freeze, v->args[0]), lvl, "p");
      forks(c, apply(c, freeze, v->args[1]), lvl, "q");
    }
    if (v->kind == V_NEU && branch == NULL)
      fail(c, "REFUSE_FORK", c->loc, "an outcome of %s is stuck with no case or match, so its fork is not visible",
           c->fork_def);
  }
  if (branch != NULL)
    fork_arms(c, v, branch, lvl, which);
  c->depth--;
}

/* SPEC section 2: the required definitions, then REFUSE_FORK on the normal
 * form of the rule body under its binder t. */
static void program_ok(C *c) {
  Global *candidates = find_global(c, "Candidates");
  Global *tally = find_global(c, "Tally");
  Global *outcome = find_global(c, "Outcome");
  if (candidates == NULL || tally == NULL || outcome == NULL || find_global(c, "two") == NULL ||
      find_global(c, "policyForkFreeze") == NULL) {
    fail(c, "TYPE_INTERNAL", c->loc, "the prelude lacks Candidates, Tally, Outcome, two or policyForkFreeze");
    return;
  }
  required(c, "candidates", candidates->value);
  Global *rule = required(c, "rule", mk_two(c, V_PI, tally->value, outcome->value));
  if (rule == NULL)
    return;
  c->fuel = CHECK_FUEL;
  c->fork_def = "rule";
  forks(c, apply(c, rule->value, mk_var(c, 0, "t")), 1, NULL);
}

static int is_ctor(const Value *v, const Global *g, size_t nargs);

/* The program def NAME, or NULL: a prelude name does not count. */
static Global *program_def(const C *c, const char *name) {
  Global *g = find_global(c, name);
  return g != NULL && !g->prelude ? g : NULL;
}

/* SPEC section 2 (O3; my choices 1 to 3): amendments and amendTo come
 * together or not at all, typed Constitutions and Policy -> Nat -> Flag.
 * amendments holds constitutions 1 to C - 1, C <= AMEND_LIMIT, and each
 * one has the fork check of rule under its binder x. */
static void amend_ok(C *c) {
  Global *list = program_def(c, "amendments");
  Global *to = program_def(c, "amendTo");
  c->constitutions = 1;
  if (list == NULL && to == NULL)
    return;
  if (list == NULL || to == NULL) {
    Global *g = list != NULL ? list : to;
    c->def = span_of(g->name);
    fail(c, "REFUSE_AMEND", g->decl->loc, "%s", list != NULL ? "amendments has no amendTo" : "amendTo has no amendments");
    return;
  }
  Global *family = find_global(c, "Constitutions");
  Global *policy = find_global(c, "Policy");
  Global *flag = find_global(c, "Flag");
  Global *cons = find_global(c, "consConstitution");
  Global *last = find_global(c, "lastConstitution");
  if (family == NULL || policy == NULL || flag == NULL || cons == NULL || last == NULL) {
    fail(c, "TYPE_INTERNAL", c->loc, "the prelude lacks Constitutions, Policy or Flag");
    return;
  }
  required(c, "amendments", family->value);
  required(c, "amendTo", mk_two(c, V_PI, policy->value, mk_two(c, V_PI, mk(c, V_NAT_TYPE), flag->value)));
  if (c->failed)
    return;
  c->def = span_of("amendments");
  c->loc = list->decl->loc;
  size_t n = 0;  /* the constitutions before the last one */
  const Value *v = list->value;
  for (; is_ctor(v, cons, 2) && n + 1 < AMEND_LIMIT; v = v->args[1])
    n++;
  if (n + 1 == AMEND_LIMIT) {
    fail(c, "AMEND_LIMIT", c->loc, "rule and amendments give more than %d constitutions", AMEND_LIMIT);
    return;
  }
  if (!is_ctor(v, last, 1)) {
    fail(c, "TABLE_STUCK", c->loc, "amendments does not reduce to a list of constitutions");
    return;
  }
  Value **fs = alloc(c, (n + 1) * sizeof *fs);
  v = list->value;
  for (size_t i = 0; i < n; i++, v = v->args[1])
    fs[i] = v->args[0];
  fs[n] = v->args[0];
  c->fork_def = "amendments";
  for (size_t i = 0; i <= n && !c->failed; i++) {
    c->fuel = CHECK_FUEL;
    forks(c, apply(c, fs[i], mk_var(c, 0, "x")), 1, NULL);
  }
  c->amends = fs;
  c->constitutions = n + 2;
}

int lang_check(Arena *arena, const Program *prelude, const Program *program, AnchorChecked **checked,
                 Diag *diag) {
  C *c = arena_alloc(arena, sizeof *c);
  if (c == NULL) {
    diag_set(diag, "MEMORY", span_of("-"), "the arena is full");
    return LANG_EXIT_REFUSED;
  }
  c->arena = arena;
  c->diag = diag;
  c->fuel = CHECK_FUEL;
  c->file = program->file;
  c->def = span_of("-");
  c->bad.kind = V_UNIT;
  c->bad.level = -1;
  c->bad_ast.kind = AST_TUPLE0;
  c->bad_ast.depth = 1;
  *checked = c;
  if (!members_ok(c, program))
    return LANG_EXIT_REFUSED;
  builtins(c);
  check_decl(c, program, 0, 0);
  c->members = (unsigned)program->decls[0].body->u.nat;
  for (size_t i = 0; i < prelude->ndecls; i++)
    check_decl(c, prelude, i, 1);
  for (size_t i = 1; i < program->ndecls; i++)
    check_decl(c, program, i, 0);
  if (!c->failed)
    program_ok(c);
  if (!c->failed)
    amend_ok(c);
  return c->failed ? LANG_EXIT_REFUSED : LANG_EXIT_OK;
}

unsigned lang_members(const AnchorChecked *checked) { return checked->members; }

/* ---- tabulation (SPEC section 7) ---- */

/* COUNT items of SIZE bytes in one piece, or NULL after MEMORY. alloc caps
 * a piece at SINK_BYTES, so the table arrays come from the arena here. */
static void *table_alloc(C *c, size_t count, size_t size) {
  void *p = c->failed ? NULL : arena_alloc(c->arena, count * size);
  if (p == NULL && !c->failed)
    fail(c, "MEMORY", c->loc, "the table does not fit in the arena");
  return p;
}

static int is_ctor(const Value *v, const Global *g, size_t nargs) {
  return v->kind == V_CON && v->global == g && v->nargs == nargs;
}

/* V is closed and first order: numbers, injections, tuples and
 * constructors all the way down. */
static int closed(const Value *v) {
  if (v->kind == V_NAT || v->kind == V_UNIT)
    return 1;
  if (v->kind == V_INJ)
    return closed(v->left);
  if (v->kind == V_PAIR || v->kind == V_TUPLE)
    return closed(v->left) && closed(v->right);
  int ok = v->kind == V_CON;
  for (size_t i = 0; ok && i < v->nargs; i++)
    ok = closed(v->args[i]);
  return ok;
}

static Ast *nat_node(C *c, unsigned long long n) {
  Ast *t = node(c, AST_NAT);
  t->u.nat = n;
  return t;
}

/* natAdd (case natEq i J with | 0 (u : prod ()) => 0 | 1 (u : prod ()) => N) REST */
static Ast *count_term(C *c, size_t j, unsigned n, Ast *rest) {
  Ast *t = node(c, AST_CASE);
  t->u.cases.subject = app_node(c, app_node(c, var_node(c, "natEq"), var_node(c, "i")), nat_node(c, j));
  for (size_t k = 0; k < 2; k++) {
    t->u.cases.arms[k].binder.name = "u";
    t->u.cases.arms[k].binder.type = node(c, AST_PROD0);
    t->u.cases.arms[k].body = nat_node(c, k == 0 ? 0 : n);
  }
  return app_node(c, app_node(c, var_node(c, "natAdd"), t), rest);
}

/* The count function of a tally (prelude note P5): COUNTS at candidates 0
 * to K-1 and 0 at the other numbers, with one term for each count that is
 * not 0. */
static Value *count_fn(C *c, const unsigned *counts, size_t k) {
  Ast *body = nat_node(c, 0);
  for (size_t j = k; j > 0; j--)
    body = counts[j - 1] == 0 ? body : count_term(c, j - 1, counts[j - 1], body);
  Ast *lam = node(c, AST_LAM);
  lam->u.bind.binder.name = "i";
  lam->u.bind.binder.type = var_node(c, "Nat");
  lam->u.bind.body = body;
  return eval(c, NULL, lam);
}

/* The number of tallies, C(members + K - 1, K - 1), or TABLE_LIMIT + 1
 * once it is larger. */
static size_t tally_total(unsigned members, size_t k) {
  unsigned long long r = 1;
  for (size_t i = 1; i < k && r <= TABLE_LIMIT; i++)
    r = r * ((unsigned long long)members + i) / i;
  return r > TABLE_LIMIT ? TABLE_LIMIT + 1 : (size_t)r;
}

/* COUNTS becomes the next tally in reverse lexicographic order. The
 * caller stops at the last tally, (0, ..., 0, members). */
static void next_tally(unsigned *counts, size_t k) {
  size_t j = k - 1;  /* one past the candidate that gives up a ballot */
  while (j > 0 && counts[j - 1] == 0)
    j--;
  if (j == 0)
    return;
  unsigned tail = counts[k - 1];
  counts[j - 1]--;
  counts[k - 1] = 0;
  counts[j] = tail + 1;
}

/* "(c0, c1, ...)", cut after 16 counts. */
static void tally_text(char *buf, size_t size, const unsigned *counts, size_t k) {
  size_t shown = k < 16 ? k : 16;
  size_t at = 0;
  for (size_t j = 0; j < shown && at < size; j++)
    at += (size_t)snprintf(buf + at, size - at, "%s%u", j == 0 ? "(" : ", ", counts[j]);
  if (at < size)
    snprintf(buf + at, size - at, "%s", k > 16 ? ", ...)" : ")");
}

/* The policies of candidates, candidate 0 first, and their number in *K;
 * NULL after TABLE_LIMIT, TABLE_STUCK or MEMORY. */
static Value **candidate_policies(C *c, size_t *k) {
  Global *cons = find_global(c, "consPolicy");
  Global *last = find_global(c, "lastPolicy");
  Value *list = find_global(c, "candidates")->value;
  size_t n = 0;
  const Value *v = list;
  for (; is_ctor(v, cons, 2) && n < TABLE_LIMIT; v = v->args[1])
    n++;
  if (n == TABLE_LIMIT) {
    fail(c, "TABLE_LIMIT", c->loc, "candidates has more than %d policies", TABLE_LIMIT);
    return NULL;
  }
  if (!is_ctor(v, last, 1)) {
    fail(c, "TABLE_STUCK", c->loc, "candidates does not reduce to a list of policies");
    return NULL;
  }
  Value **ps = table_alloc(c, n + 1, sizeof *ps);
  if (ps == NULL)
    return NULL;
  v = list;
  for (size_t i = 0; i < n; i++, v = v->args[1])
    ps[i] = v->args[0];
  ps[n] = v->args[0];
  for (size_t i = 0; i <= n && !c->failed; i++)
    if (!closed(ps[i]))
      fail(c, "TABLE_STUCK", c->loc, "candidate %lu does not reduce to a closed policy", (unsigned long)i);
  *k = n + 1;
  return c->failed ? NULL : ps;
}

/* The number of the closed policy P: the first candidate or earlier extra
 * policy with the same normal form, else a new extra policy at *N. */
static size_t policy_number(C *c, Value **policies, size_t *n, Value *p) {
  for (size_t i = 0; i < *n; i++)
    if (conv(c, policies[i], p, 0))
      return i;
  policies[*n] = p;
  return (*n)++;
}

/* The full fork check at one tally (SPEC section 2): each side of the
 * closed outcome two p q O is frozen. */
static void fork_sides(C *c, Value *o, const char *at) {
  Value *freeze = find_global(c, "policyForkFreeze")->value;
  for (size_t i = 0; i < 2 && !c->failed; i++) {
    Value *flag = apply(c, freeze, o->args[i]);
    if (flag->kind == V_INJ && flag->nat == 0)
      fail(c, "REFUSE_FORK", c->loc, "policyForkFreeze %s of the two p q outcome at tally %s is flagNo",
           i == 0 ? "p" : "q", at);
  }
}

/* The row of the outcome O of WHAT at the tally AT, normalized to none,
 * one p or two p q with closed policies. */
static AnchorRow outcome_row(C *c, Value *o, const char *what, const char *at, Value **policies, size_t *n) {
  static const char *const fates[3] = {"none", "one", "two"};
  AnchorRow row = {LANG_FATE_NONE, 0, 0};
  if (c->failed)
    return row;
  size_t fate = 0;  /* also the number of policies of the outcome */
  while (fate < 3 && !is_ctor(o, find_global(c, fates[fate]), fate))
    fate++;
  if (fate == 3 || !closed(o)) {
    fail(c, "TABLE_STUCK", c->loc, "the outcome of %s at tally %s is not none, one p or two p q of closed policies",
         what, at);
    return row;
  }
  if (fate == 2)
    fork_sides(c, o, at);
  row.fate = (AnchorFate)fate;
  row.p = fate >= 1 ? policy_number(c, policies, n, o->args[0]) : 0;
  row.q = fate == 2 ? policy_number(c, policies, n, o->args[1]) : 0;
  return row;
}

/* The tally COUNTS as a value: mkTally of the count function. */
static Value *tally_value(C *c, const unsigned *counts, size_t k) {
  return apply(c, find_global(c, "mkTally")->value, count_fn(c, counts, k));
}

/* The row of the tally COUNTS: rule at mkTally of the count function. */
static AnchorRow tally_row(C *c, const unsigned *counts, size_t k, Value **policies, size_t *n) {
  char at[256];
  tally_text(at, sizeof at, counts, k);
  c->fuel = CHECK_FUEL;
  Value *tally = tally_value(c, counts, k);
  return outcome_row(c, apply(c, find_global(c, "rule")->value, tally), "rule", at, policies, n);
}

/* The sorted profile of the tally COUNTS (O3, my choice 4): counts[0]
 * ballots of 0 first, then the ballots of 1, and so on. */
static Value *sorted_profile(C *c, const unsigned *counts, size_t k) {
  Value *with = find_global(c, "withBallot")->value;
  Value *x = find_global(c, "noBallots")->value;
  for (size_t j = k; j > 0; j--)
    for (unsigned b = 0; b < counts[j - 1] && !c->failed; b++)
      x = apply(c, apply(c, with, mk_nat(c, j - 1)), x);
  return x;
}

/* R when the constitution F is constitutionOf R: a closure of the inner
 * lambda of the prelude constitutionOf (the same body node), with R in its
 * environment. Else NULL. The test is on the closure, not on the meaning. */
static Value *constitution_rule(const C *c, const Value *f) {
  const Global *of = find_global(c, "constitutionOf");
  const Value *v = of != NULL && of->prelude ? of->value : NULL;
  const Ast *outer = v != NULL && v->kind == V_LAM ? v->body : NULL;
  int same = outer != NULL && outer->kind == AST_LAM && f->kind == V_LAM && f->body == outer->u.bind.body;
  return same && f->env != NULL ? f->env->value : NULL;
}

/* The row of constitution B >= 1 at the tally COUNTS: its outcome at the
 * sorted profile of COUNTS. For constitutionOf r, the row is r at the tally
 * COUNTS, and the checker builds no profile (SPEC section 7, a1b): at the
 * sorted profile, orbitProjection gives that tally, so the row is the same. */
static AnchorRow profile_row(C *c, size_t b, const unsigned *counts, size_t k, Value **policies, size_t *n) {
  char at[256];
  char what[64];
  tally_text(at, sizeof at, counts, k);
  snprintf(what, sizeof what, "constitution %lu", (unsigned long)b);
  c->fuel = CHECK_FUEL;
  Value *f = c->amends[b - 1];
  Value *r = constitution_rule(c, f);
  Value *o = r != NULL ? apply(c, r, tally_value(c, counts, k)) : apply(c, f, sorted_profile(c, counts, k));
  return outcome_row(c, o, what, at, policies, n);
}

/* One mask for each of the N policies (O3, my choice 5): bit B is set when
 * amendTo p B is flagYes. NULL after TABLE_STUCK, TYPE_FUEL or MEMORY. */
static unsigned char *amend_masks(C *c, Value **policies, size_t n) {
  Global *to = find_global(c, "amendTo");
  c->def = span_of("amendTo");
  c->loc = to->decl->loc;
  unsigned char *masks = table_alloc(c, n, sizeof *masks);
  for (size_t i = 0; i < n && !c->failed; i++) {
    masks[i] = 0;
    for (size_t b = 0; b < c->constitutions && !c->failed; b++) {
      c->fuel = CHECK_FUEL;
      Value *flag = apply(c, apply(c, to->value, policies[i]), mk_nat(c, b));
      int yes = flag->kind == V_INJ && flag->nat == 1;
      if (!c->failed && (flag->kind != V_INJ || !closed(flag)))
        fail(c, "TABLE_STUCK", c->loc, "amendTo of policy %lu at constitution %lu does not normalize to a closed flag",
             (unsigned long)i, (unsigned long)b);
      masks[i] = (unsigned char)(masks[i] | (yes ? 1u << b : 0u));
    }
  }
  return c->failed ? NULL : masks;
}

int lang_table(AnchorChecked *c, AnchorTable *t) {
  memset(t, 0, sizeof *t);
  if (c->failed)
    return LANG_EXIT_REFUSED;
  c->def = span_of("candidates");
  c->loc = find_global(c, "candidates")->decl->loc;
  size_t k = 0;
  Value **cands = candidate_policies(c, &k);
  size_t total = cands == NULL ? 0 : tally_total(c->members, k);
  size_t blocks = c->constitutions;  /* C: one block of R rows for each constitution */
  if (total > TABLE_LIMIT)
    fail(c, "TABLE_LIMIT", c->loc, "%u members and %lu candidates give more than %d tallies", c->members,
         (unsigned long)k, TABLE_LIMIT);
  else if (total * blocks > TABLE_LIMIT)
    fail(c, "TABLE_LIMIT", c->loc, "%u members, %lu candidates and %lu constitutions give more than %d rows",
         c->members, (unsigned long)k, (unsigned long)blocks, TABLE_LIMIT);
  if (c->failed)
    return LANG_EXIT_REFUSED;
  c->def = span_of("rule");
  c->loc = find_global(c, "rule")->decl->loc;
  unsigned *counts = table_alloc(c, total * k, sizeof *counts);
  AnchorRow *rows = table_alloc(c, total * blocks, sizeof *rows);
  Value **policies = table_alloc(c, k + 2 * total * blocks, sizeof *policies);
  if (c->failed)
    return LANG_EXIT_REFUSED;
  memcpy(policies, cands, k * sizeof *policies);
  size_t n = k;
  memset(counts, 0, k * sizeof *counts);
  counts[0] = c->members;
  for (size_t r = 0; r < total && !c->failed; r++) {
    unsigned *row = counts + r * k;
    if (r > 0) {
      memcpy(row, row - k, k * sizeof *row);
      next_tally(row, k);
    }
    rows[r] = tally_row(c, row, k, policies, &n);
  }
  if (blocks > 1) {
    c->def = span_of("amendments");
    c->loc = find_global(c, "amendments")->decl->loc;
  }
  for (size_t b = 1; b < blocks && !c->failed; b++)
    for (size_t r = 0; r < total && !c->failed; r++)
      rows[b * total + r] = profile_row(c, b, counts + r * k, k, policies, &n);
  const unsigned char *masks = blocks > 1 ? amend_masks(c, policies, n) : NULL;
  const Ast **forms = table_alloc(c, n, sizeof *forms);
  for (size_t i = 0; i < n && !c->failed; i++)
    forms[i] = quote(c, policies[i], 0);
  if (c->failed)
    return LANG_EXIT_REFUSED;
  t->members = c->members;
  t->candidates = k;
  t->npolicies = n;
  t->policies = forms;
  t->constitutions = blocks;
  t->amend = masks;
  t->nrows = total;
  t->counts = counts;
  t->rows = rows;
  return LANG_EXIT_OK;
}

static int policy_app(const Ast *a) { return a != NULL && a->kind == AST_APP; }

static int policy_var(const Ast *a, const char *name) {
  int var = a != NULL && a->kind == AST_VAR;
  return var && strcmp(a->u.name, name) == 0;
}

int lang_policy_fields(const AnchorTable *t, size_t i, int *allow, unsigned long long *schema,
                         unsigned long long *window) {
  const Ast *args[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
  const Ast *head = i < t->npolicies ? t->policies[i] : NULL;
  size_t n = 6;
  while (n > 0 && policy_app(head)) {
    n--;
    args[n] = head->u.app.arg;
    head = head->u.app.fun;
  }
  int nat = args[4] != NULL && args[4]->kind == AST_NAT;
  int window_nat = args[3] != NULL && args[3]->kind == AST_NAT;
  *allow = policy_var(args[0], "allow");
  *schema = nat ? args[4]->u.nat : 0;
  *window = window_nat ? args[3]->u.nat : 0;
  int fields = nat && window_nat && (*allow || policy_var(args[0], "deny"));
  return n == 0 && policy_var(head, "mkPolicy") && fields ? 0 : 1;
}

static const char *const FATES[3] = {"none", "one", "two"};

/* "tally c0 ... cK-1" of row R, with no newline. */
static void print_tally(FILE *out, const AnchorTable *t, size_t r) {
  fputs("tally", out);
  for (size_t j = 0; j < t->candidates; j++)
    fprintf(out, " %u", t->counts[r * t->candidates + j]);
}

/* "members N", "candidates K", then "constitutions C" when C > 1. */
static void print_head(FILE *out, const AnchorTable *t) {
  fprintf(out, "members %u\ncandidates %lu\n", t->members, (unsigned long)t->candidates);
  if (t->constitutions > 1)
    fprintf(out, "constitutions %lu\n", (unsigned long)t->constitutions);
}

/* "constitution B" before block B when C > 1. */
static void print_block(FILE *out, const AnchorTable *t, size_t b) {
  if (t->constitutions > 1)
    fprintf(out, "constitution %lu\n", (unsigned long)b);
}

void lang_print_table(FILE *out, const AnchorTable *t) {
  print_head(out, t);
  for (size_t i = 0; i < t->npolicies; i++) {
    fprintf(out, "policy %lu ", (unsigned long)i);
    lang_print_term(out, t->policies[i]);
    fputc('\n', out);
  }
  for (size_t i = 0; t->amend != NULL && i < t->npolicies; i++) {
    fprintf(out, "amendTo %lu ", (unsigned long)i);
    for (size_t b = 0; b < t->constitutions; b++)
      fputc(((t->amend[i] >> b) & 1u) != 0 ? '1' : '0', out);
    fputc('\n', out);
  }
  for (size_t b = 0; b < t->constitutions; b++) {
    print_block(out, t, b);
    for (size_t r = 0; r < t->nrows; r++) {
      const AnchorRow *row = &t->rows[b * t->nrows + r];
      print_tally(out, t, r);
      fprintf(out, " : %s", FATES[row->fate]);
      if (row->fate != LANG_FATE_NONE)
        fprintf(out, " %lu", (unsigned long)row->p);
      if (row->fate == LANG_FATE_TWO)
        fprintf(out, " %lu", (unsigned long)row->q);
      fputc('\n', out);
    }
  }
}

/* ---- the fate report and eval (chunk 4b) ---- */

void lang_print_report(FILE *out, const AnchorTable *t) {
  print_head(out, t);
  for (size_t b = 0; b < t->constitutions; b++) {
    const AnchorRow *rows = t->rows + b * t->nrows;
    print_block(out, t, b);
    for (size_t f = 0; f < 3; f++) {
      size_t n = 0;
      for (size_t r = 0; r < t->nrows; r++)
        n += rows[r].fate == (AnchorFate)f;
      fprintf(out, "fate %s %lu\n", FATES[f], (unsigned long)n);
      for (size_t r = 0; r < t->nrows; r++)
        if (rows[r].fate == (AnchorFate)f) {
          print_tally(out, t, r);
          fputc('\n', out);
        }
    }
  }
}

int lang_eval(AnchorChecked *c, const char *name, FILE *out) {
  if (c->failed)
    return LANG_EXIT_REFUSED;
  Global *g = find_global(c, name);
  if (g == NULL)
    return refuse(c, "EVAL_NAME", name, "is not declared");
  if ((g->kind != G_DEF && g->kind != G_REC) || g->decl == NULL)
    return refuse(c, "EVAL_NAME", name, "is not a def");
  c->def = span_of(g->name);
  c->loc = g->decl->loc;
  c->fuel = CHECK_FUEL;
  Ast *t = quote(c, g->kind == G_REC ? g->body : g->value, 0);
  if (c->failed)
    return LANG_EXIT_REFUSED;
  lang_print_term(out, t);
  fputc('\n', out);
  return LANG_EXIT_OK;
}
