/* The type checker (see front/check.h). SHARED by the tcc kits.

   Checking is bidirectional. A term is checked against an expected type (the
   hint) or its type is inferred. Two types are equal when their normal forms
   are equal (front/eval.c). The checker writes core terms (front/core.h). */
#include "front/check.h"

#include <string.h>

#define CHECK_DEPTH_LIMIT 2000u
#define SPINE_MAX 64u
#define SHOW_MAX 512u
#define NAMES_MAX 64u
#define UNIVERSE_MAX 2u
#define PRINT_MAX 65536u
#define NO_FAMILY UINT32_MAX
#define NO_DEF UINT32_MAX

#define FAIL(c, code, ...) diag_fail((c)->m->diag, (code), (c)->m->def, __VA_ARGS__)

typedef struct Scope Scope;
struct Scope {
  const char *name; /* NULL for the binder of `A -> B` */
  const Value *type;
  uint32_t level;
  const Scope *next;
};

typedef struct {
  Machine *m;
  const DeclList *decls;
  size_t decl_index;
  const Scope *scope; /* the innermost binder first */
  const Env *env;     /* a variable for each binder */
  uint32_t level;     /* the number of binders */
  unsigned depth;
  uint32_t eta_count;
  uint32_t self;      /* the family whose fields are checked, or NO_FAMILY */
  uint32_t self_uses; /* the uses of that family in the current field */
} Checker;

typedef enum {
  RULE_TYPE0,
  RULE_TYPE1,
  RULE_TYPE2,
  RULE_EQ,
  RULE_VALUE0,
  RULE_PAIR,
  RULE_INJ,
  RULE_EMPTY,
  RULE_SOME,
  RULE_CONS,
  RULE_PACK,
  RULE_REFL,
  RULE_PART,
  RULE_SIGMA_PART,
  RULE_EITHER,
  RULE_OPTION,
  RULE_PURE,
  RULE_MAP,
  RULE_BIND,
  RULE_FILTER,
  RULE_FOLD,
  RULE_UNFOLD,
  RULE_SYMM,
  RULE_TRANS,
  RULE_CONG,
  RULE_TRANSPORT,
  RULE_NAT2,
  RULE_U256_2,
  RULE_ADDR2,
  RULE_TO_U256,
  RULE_FLAG_IF,
  RULE_KMAP,
  RULE_KMAP_GET,
  RULE_KMAP_SET
} Rule;

typedef struct {
  const char *name;
  uint32_t arity; /* fold: the least number of arguments */
  Rule rule;
  Op op;
  Op result;     /* VALUE0, NAT2, U256_2, ADDR2, TO_U256: the result type. EMPTY, INJ: the type former. */
  uint32_t part; /* INJ, PART, SIGMA_PART: 0 or 1 */
} Builtin;

static const Builtin BUILTINS[] = {
  {"Nat", 0, RULE_TYPE0, OP_NAT, OP_NAT, 0},
  {"Flag", 0, RULE_TYPE0, OP_FLAG, OP_NAT, 0},
  {"U256", 0, RULE_TYPE0, OP_U256, OP_NAT, 0},
  {"Addr", 0, RULE_TYPE0, OP_ADDR, OP_NAT, 0},
  {"Unit", 0, RULE_TYPE0, OP_UNIT, OP_NAT, 0},
  {"Prod", 2, RULE_TYPE2, OP_PROD, OP_NAT, 0},
  {"Sum", 2, RULE_TYPE2, OP_SUM, OP_NAT, 0},
  {"Option", 1, RULE_TYPE1, OP_OPTION, OP_NAT, 0},
  {"List", 1, RULE_TYPE1, OP_LIST, OP_NAT, 0},
  {"Eq", 3, RULE_EQ, OP_EQ, OP_NAT, 0},
  {"unit", 0, RULE_VALUE0, OP_UNIT_VAL, OP_UNIT, 0},
  {"flagYes", 0, RULE_VALUE0, OP_FLAG_YES, OP_FLAG, 0},
  {"flagNo", 0, RULE_VALUE0, OP_FLAG_NO, OP_FLAG, 0},
  {"pair", 2, RULE_PAIR, OP_PAIR, OP_PROD, 0},
  {"inl", 1, RULE_INJ, OP_INL, OP_SUM, 0},
  {"inr", 1, RULE_INJ, OP_INR, OP_SUM, 1},
  {"none", 0, RULE_EMPTY, OP_NONE, OP_OPTION, 0},
  {"nil", 0, RULE_EMPTY, OP_NIL, OP_LIST, 0},
  {"some", 1, RULE_SOME, OP_SOME, OP_OPTION, 0},
  {"cons", 2, RULE_CONS, OP_CONS, OP_LIST, 0},
  {"pack", 2, RULE_PACK, OP_PACK, OP_NAT, 0},
  {"refl", 0, RULE_REFL, OP_REFL, OP_EQ, 0},
  {"first", 1, RULE_PART, OP_FIRST, OP_PROD, 0},
  {"second", 1, RULE_PART, OP_SECOND, OP_PROD, 1},
  {"witness", 1, RULE_SIGMA_PART, OP_WITNESS, OP_NAT, 0},
  {"payload", 1, RULE_SIGMA_PART, OP_PAYLOAD, OP_NAT, 1},
  {"either", 3, RULE_EITHER, OP_EITHER, OP_SUM, 0},
  {"option", 3, RULE_OPTION, OP_OPTION_ELIM, OP_OPTION, 0},
  {"pure", 1, RULE_PURE, OP_PURE, OP_NAT, 0},
  {"map", 2, RULE_MAP, OP_MAP, OP_NAT, 0},
  {"bind", 2, RULE_BIND, OP_BIND, OP_NAT, 0},
  {"filter", 2, RULE_FILTER, OP_FILTER, OP_NAT, 0},
  {"fold", 2, RULE_FOLD, OP_FOLD_NAT, OP_NAT, 0},
  {"unfold", 3, RULE_UNFOLD, OP_UNFOLD, OP_LIST, 0},
  {"symm", 1, RULE_SYMM, OP_SYMM, OP_EQ, 0},
  {"trans", 2, RULE_TRANS, OP_TRANS, OP_EQ, 0},
  {"cong", 2, RULE_CONG, OP_CONG, OP_EQ, 0},
  {"transport", 3, RULE_TRANSPORT, OP_TRANSPORT, OP_EQ, 0},
  {"natAdd", 2, RULE_NAT2, OP_NAT_ADD, OP_NAT, 0},
  {"natSub", 2, RULE_NAT2, OP_NAT_SUB, OP_NAT, 0},
  {"natMul", 2, RULE_NAT2, OP_NAT_MUL, OP_NAT, 0},
  {"natEq", 2, RULE_NAT2, OP_NAT_EQ, OP_FLAG, 0},
  {"natLe", 2, RULE_NAT2, OP_NAT_LE, OP_FLAG, 0},
  {"u256Add", 2, RULE_U256_2, OP_U256_ADD, OP_U256, 0},
  {"u256Sub", 2, RULE_U256_2, OP_U256_SUB, OP_U256, 0},
  {"u256Mul", 2, RULE_U256_2, OP_U256_MUL, OP_U256, 0},
  {"u256Div", 2, RULE_U256_2, OP_U256_DIV, OP_U256, 0},
  {"u256Le", 2, RULE_U256_2, OP_U256_LE, OP_FLAG, 0},
  {"u256Eq", 2, RULE_U256_2, OP_U256_EQ, OP_FLAG, 0},
  {"u256Min", 2, RULE_U256_2, OP_U256_MIN, OP_U256, 0},
  {"toU256", 1, RULE_TO_U256, OP_TO_U256, OP_U256, 0},
  {"addrEq", 2, RULE_ADDR2, OP_ADDR_EQ, OP_FLAG, 0},
  {"flagIf", 3, RULE_FLAG_IF, OP_FLAG_IF, OP_FLAG, 0},
  {"Map", 2, RULE_KMAP, OP_KMAP, OP_NAT, 0},
  {"mapEmpty", 0, RULE_EMPTY, OP_KMAP_OF, OP_KMAP, 0},
  {"mapGet", 2, RULE_KMAP_GET, OP_KMAP_GET, OP_KMAP, 0},
  {"mapSet", 3, RULE_KMAP_SET, OP_KMAP_SET, OP_KMAP, 0}
};

#define BUILTIN_COUNT ((uint32_t)(sizeof BUILTINS / sizeof BUILTINS[0]))

/* Names that no declaration can take (rule R4). */
static const char *const RESERVED[] = {"Bool", "Text"};

typedef enum {
  HEAD_TERM, /* a local, a definition or another term: plain application */
  HEAD_FAMILY,
  HEAD_CTOR,
  HEAD_PROJ,
  HEAD_BUILTIN
} HeadKind;

typedef struct {
  HeadKind kind;
  uint32_t index; /* FAMILY: the family. CTOR, PROJ: the constructor. */
  uint32_t field; /* PROJ: the field number */
  const Builtin *builtin;
} Head;

/* One application of a core name to the arguments of a spine. */
typedef struct {
  Checker *c;
  const Builtin *b;
  const Term *const *args;
  uint32_t argc;      /* the arguments in the spine */
  const Value *hint;  /* the expected type of the result, or NULL */
  const Value *outer; /* the expected type of the whole spine, or NULL */
  uint32_t used;      /* the arguments that the core name takes */
  const Value *type;  /* the type of the result */
} Call;

static const Core *nul(int failed) {
  (void)failed;
  return NULL;
}

static Core *new_core(Checker *c, CoreKind kind) {
  Core *k = arena_alloc(c->m->arena, sizeof *k);
  if (k == NULL) {
    FAIL(c, "OOM", "out of memory");
    return NULL;
  }
  k->kind = kind;
  return k;
}

static const Core *core_index(Checker *c, CoreKind kind, uint32_t index) {
  Core *k = new_core(c, kind);
  if (k != NULL)
    k->index = index;
  return k;
}

static const Core *core_leaf(Checker *c, CoreKind kind, uint64_t nat) {
  Core *k = new_core(c, kind);
  if (k != NULL)
    k->nat = nat;
  return k;
}

static const Core *core_word(Checker *c, uint64_t sort, const U256 *word) {
  Core *k = new_core(c, CORE_WORD);
  if (k != NULL) {
    k->nat = sort;
    k->word = word;
  }
  return k;
}

static const Core *core_bind(Checker *c, CoreKind kind, const char *name, const Core *a, const Core *b) {
  Core *k;
  if (b == NULL || (a == NULL && kind != CORE_LAM))
    return NULL;
  k = new_core(c, kind);
  if (k == NULL)
    return NULL;
  k->name = name;
  k->a = a;
  k->b = b;
  return k;
}

static const Core *core_app(Checker *c, const Core *f, const Core *x) {
  Core *k;
  if (f == NULL || x == NULL)
    return NULL;
  k = new_core(c, CORE_APP);
  if (k == NULL)
    return NULL;
  k->a = f;
  k->b = x;
  return k;
}

static const Core *core_op(Checker *c, Op op, uint32_t inst, uint32_t field, const Core *const *args, uint32_t n) {
  const Core **copy = arena_alloc(c->m->arena, ((size_t)n + 1u) * sizeof *copy);
  Core *k;
  uint32_t i;
  if (copy == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  for (i = 0; i < n; i++) {
    if (args[i] == NULL)
      return NULL;
    copy[i] = args[i];
  }
  k = new_core(c, CORE_OP);
  if (k == NULL)
    return NULL;
  k->op = op;
  k->inst = inst;
  k->field = field;
  k->args = copy;
  k->argc = n;
  return k;
}

static const Core *op3(Checker *c, Op op, uint32_t inst, uint32_t n, const Core *a, const Core *b, const Core *x) {
  const Core *args[3];
  args[0] = a;
  args[1] = b;
  args[2] = x;
  return core_op(c, op, inst, 0, args, n);
}

static const Value *tyop(Checker *c, Op op, uint32_t n, const Value *a, const Value *b, const Value *x) {
  return val_make(c->m, op, 0, n, a, b, x);
}

static const Value *nat_type(Checker *c) {
  return tyop(c, OP_NAT, 0, NULL, NULL, NULL);
}

static const Value *flag_type(Checker *c) {
  return tyop(c, OP_FLAG, 0, NULL, NULL, NULL);
}

/* The value of the core term K in the current context. */
static const Value *here(Checker *c, const Core *k) {
  return k == NULL ? NULL : eval_core(c->m, c->env, k);
}

/* V in the surface syntax, for a diagnostic. */
static const char *show(Checker *c, const Value *v) {
  const char *names[NAMES_MAX];
  const Scope *s;
  char *buf = arena_alloc(c->m->arena, SHOW_MAX);
  uint32_t count = c->level < NAMES_MAX ? c->level : NAMES_MAX;
  for (s = c->scope; s != NULL; s = s->next) {
    if (s->level < NAMES_MAX)
      names[s->level] = s->name != NULL ? s->name : "_";
  }
  if (buf == NULL || v == NULL)
    return "?";
  value_print(c->m, names, count, v, buf, SHOW_MAX);
  return buf;
}

static int push(Checker *c, const char *name, const Value *type) {
  Scope *s = arena_alloc(c->m->arena, sizeof *s);
  const Env *e = env_push(c->m, c->env, val_var(c->m, c->level));
  if (type == NULL || s == NULL || e == NULL)
    return c->m->diag->set ? 0 : FAIL(c, "OOM", "out of memory");
  s->name = name;
  s->type = type;
  s->level = c->level;
  s->next = c->scope;
  c->scope = s;
  c->env = e;
  c->level++;
  return 1;
}

static void pop(Checker *c) {
  c->scope = c->scope->next;
  c->env = c->env->next;
  c->level--;
}

/* 1 when a term of type FOUND can stand where WANT is expected.
   Universes are not cumulative (former F14). */
static int expect(Checker *c, const Value *found, const Value *want) {
  if (found == NULL || want == NULL)
    return 0;
  if (conv_values(c->m, c->level, found, want))
    return 1;
  return FAIL(c, "TYPE_MISMATCH", "expected a term of type %s, found a term of type %s", show(c, want), show(c, found));
}

static const Scope *find_local(const Checker *c, const char *name) {
  const Scope *s;
  for (s = c->scope; s != NULL; s = s->next) {
    if (s->name != NULL && strcmp(s->name, name) == 0)
      return s;
  }
  return NULL;
}

static uint32_t find_def(const Machine *m, const char *name) {
  uint32_t i;
  for (i = 0; i < m->def_count; i++) {
    if (strcmp(m->defs[i].name, name) == 0)
      return i;
  }
  return NO_DEF;
}

/* An environment that binds VALS in order: the last one is index 0. */
static const Env *env_of(Checker *c, const Value *const *vals, uint32_t n) {
  const Env *e = NULL;
  uint32_t i;
  for (i = 0; i < n; i++)
    e = env_push(c->m, e, vals[i]);
  return e;
}

static const Core *elab(Checker *c, const Term *t, const Value *hint, const Value **type);
static const Core *check_lam(Checker *c, const Term *t, const Value *hint, const Value **type);

static const Core *check(Checker *c, const Term *t, const Value *want) {
  const Value *type = NULL;
  return want == NULL ? NULL : elab(c, t, want, &type);
}

static const Core *infer(Checker *c, const Term *t, const Value **type) {
  *type = NULL;
  return elab(c, t, NULL, type);
}

/* infer, but a failure leaves no diagnostic. */
static const Core *try_infer(Checker *c, const Term *t, const Value **type) {
  Diag saved = *c->m->diag;
  const Core *k = infer(c, t, type);
  if (k == NULL)
    *c->m->diag = saved;
  return k;
}

static const Core *check_type(Checker *c, const Term *t, uint64_t *level) {
  const Value *type = NULL;
  const Core *k = infer(c, t, &type);
  if (k == NULL)
    return NULL;
  if (type == NULL || type->kind != VAL_UNIV)
    return nul(FAIL(c, "TYPE_MISMATCH", "expected a type, found a term of type %s", show(c, type)));
  *level = type->nat;
  return k;
}

/* Compares the inferred *TYPE with HINT. */
static const Core *finish(Checker *c, const Core *k, const Value *hint, const Value **type) {
  if (k == NULL || *type == NULL)
    return NULL;
  if (hint != NULL && !expect(c, *type, hint))
    return NULL;
  *type = hint != NULL ? hint : *type;
  return k;
}

/* 1 when T is Option A, List A or Sum E A. *ELEM is A. */
static int carrier_of(const Value *t, Carrier *carrier, const Value **elem) {
  if (val_is(t, OP_OPTION) || val_is(t, OP_LIST)) {
    *carrier = val_is(t, OP_OPTION) ? CARRIER_OPTION : CARRIER_LIST;
    *elem = t->args[0];
    return 1;
  }
  if (!val_is(t, OP_SUM))
    return 0;
  *carrier = CARRIER_SUM;
  *elem = t->args[1];
  return 1;
}

/* SHAPE (Option, List or Sum E) with the element ELEM. */
static const Value *carried(Checker *c, const Value *shape, const Value *elem) {
  if (shape == NULL)
    return NULL;
  return val_is(shape, OP_SUM) ? tyop(c, OP_SUM, 2, shape->args[0], elem, NULL) : tyop(c, shape->op, 1, elem, NULL, NULL);
}

/* Checks T as a function from DOM. *RESULT is WANT, or the result type that
   T infers when WANT is NULL. */
static const Core *fn_into(Checker *c, const Term *t, const Value *dom, const Value *want, const Value **result) {
  const Value *ft = NULL;
  const Core *f;
  if (want != NULL) {
    *result = want;
    return check(c, t, val_arrow(c->m, dom, want));
  }
  f = infer(c, t, &ft);
  if (f == NULL)
    return NULL;
  if (ft == NULL || ft->kind != VAL_PI)
    return nul(FAIL(c, "TYPE_NOT_FUNCTION", "expected a function, found a term of type %s", show(c, ft)));
  *result = closure_apply(c->m, ft, val_var(c->m, c->level));
  /* Check dependence before the result leaves its binder. */
  if (!conv_values(c->m, c->level + 2u, *result,
                   closure_apply(c->m, ft, val_var(c->m, c->level + 1u))))
    return nul(FAIL(c, "TYPE_MISMATCH", "this operation needs a function with a non-dependent result type"));
  return expect(c, ft, val_arrow(c->m, dom, *result)) ? f : NULL;
}

/* The argument type of the function T, from its binder or its type. */
static const Value *lam_domain(Checker *c, const Term *t) {
  uint64_t level = 0;
  const Value *ft = NULL;
  if (t->kind == TERM_LAM && t->left != NULL)
    return here(c, check_type(c, t->left, &level));
  return try_infer(c, t, &ft) != NULL && ft != NULL && ft->kind == VAL_PI ? ft->dom : NULL;
}

/* The carried argument ARG of map, bind or filter. When ARG does not infer
   (`inr 3`), its type comes from the expected type and from the binder of
   the function FN. */
static const Core *carried_arg(Call *k, const Term *arg, const Term *fn, const Value **type) {
  Checker *c = k->c;
  Carrier carrier = CARRIER_OPTION;
  const Value *elem = NULL;
  const Value *dom;
  const Core *x = try_infer(c, arg, type);
  if (x != NULL)
    return x;
  if (!carrier_of(k->hint, &carrier, &elem))
    return infer(c, arg, type);
  dom = lam_domain(c, fn);
  if (dom == NULL)
    return infer(c, arg, type);
  *type = carried(c, k->hint, dom);
  return check(c, arg, *type);
}

static const Core *need_type(Call *k, const char *what) {
  return nul(FAIL(k->c, "TYPE_INFER", "%s needs a declared type %s here", k->b->name, what));
}

static const Core *not_carrier(Call *k, const Value *type) {
  return nul(FAIL(k->c, "TYPE_MISMATCH", "%s needs a value of type Option A, List A or Sum E A, found a term of type %s", k->b->name, show(k->c, type)));
}

static const Core *infer_eq(Call *k, uint32_t i, const Value **type) {
  const Core *e = infer(k->c, k->args[i], type);
  if (e == NULL)
    return NULL;
  return val_is(*type, OP_EQ) ? e : nul(FAIL(k->c, "TYPE_MISMATCH", "%s needs a proof of type Eq A a b, found a term of type %s", k->b->name, show(k->c, *type)));
}

static const Core *rule_type1(Call *k) {
  uint64_t level = 0;
  const Core *a = check_type(k->c, k->args[0], &level);
  k->type = val_univ(k->c->m, level);
  return op3(k->c, k->b->op, 0, 1, a, NULL, NULL);
}

static const Core *rule_type2(Call *k) {
  uint64_t la = 0;
  uint64_t lb = 0;
  const Core *a = check_type(k->c, k->args[0], &la);
  const Core *b = a == NULL ? NULL : check_type(k->c, k->args[1], &lb);
  k->type = val_univ(k->c->m, la > lb ? la : lb);
  return op3(k->c, k->b->op, 0, 2, a, b, NULL);
}

static const Core *rule_eq(Call *k) {
  Checker *c = k->c;
  uint64_t level = 0;
  const Core *t = check_type(c, k->args[0], &level);
  const Value *tv = here(c, t);
  const Core *a = check(c, k->args[1], tv);
  const Core *b = a == NULL ? NULL : check(c, k->args[2], tv);
  k->type = val_univ(c->m, level);
  return op3(c, OP_EQ, 0, 3, t, a, b);
}

static const Core *rule_pair(Call *k) {
  Checker *c = k->c;
  const Value *ta = val_is(k->hint, OP_PROD) ? k->hint->args[0] : NULL;
  const Value *tb = val_is(k->hint, OP_PROD) ? k->hint->args[1] : NULL;
  const Core *a = ta != NULL ? check(c, k->args[0], ta) : infer(c, k->args[0], &ta);
  const Core *b = a == NULL ? NULL : (tb != NULL ? check(c, k->args[1], tb) : infer(c, k->args[1], &tb));
  k->type = tyop(c, OP_PROD, 2, ta, tb, NULL);
  return op3(c, OP_PAIR, 0, 2, a, b, NULL);
}

static const Core *rule_inj(Call *k) {
  if (!val_is(k->hint, OP_SUM))
    return need_type(k, "Sum A B");
  k->type = k->hint;
  return op3(k->c, k->b->op, 0, 1, check(k->c, k->args[0], k->hint->args[k->b->part]), NULL, NULL);
}

static const Core *rule_empty(Call *k) {
  if (!val_is(k->hint, k->b->result))
    return need_type(k, k->b->result == OP_OPTION ? "Option A" : k->b->result == OP_KMAP ? "Map K V" : "List A");
  k->type = k->hint;
  return op3(k->c, k->b->op, 0, 0, NULL, NULL, NULL);
}

static const Core *rule_some(Call *k) {
  Checker *c = k->c;
  const Value *a = val_is(k->hint, OP_OPTION) ? k->hint->args[0] : NULL;
  const Core *x = a != NULL ? check(c, k->args[0], a) : infer(c, k->args[0], &a);
  k->type = tyop(c, OP_OPTION, 1, a, NULL, NULL);
  return op3(c, OP_SOME, 0, 1, x, NULL, NULL);
}

static const Core *rule_cons(Call *k) {
  Checker *c = k->c;
  const Value *a = val_is(k->hint, OP_LIST) ? k->hint->args[0] : NULL;
  const Core *x = a != NULL ? check(c, k->args[0], a) : infer(c, k->args[0], &a);
  const Value *list = tyop(c, OP_LIST, 1, a, NULL, NULL);
  const Core *xs = x == NULL ? NULL : check(c, k->args[1], list);
  k->type = list;
  return op3(c, OP_CONS, 0, 2, x, xs, NULL);
}

static const Core *rule_pack(Call *k) {
  Checker *c = k->c;
  const Value *s = k->hint;
  const Core *w;
  const Core *p;
  if (s == NULL || s->kind != VAL_SIGMA)
    return need_type(k, "Sigma (x : A) B");
  w = check(c, k->args[0], s->dom);
  p = w == NULL ? NULL : check(c, k->args[1], closure_apply(c->m, s, here(c, w)));
  k->type = s;
  return op3(c, OP_PACK, 0, 2, w, p, NULL);
}

static const Core *rule_refl(Call *k) {
  Checker *c = k->c;
  const Value *e = k->hint;
  if (!val_is(e, OP_EQ))
    return need_type(k, "Eq A a b");
  if (!conv_values(c->m, c->level, e->args[1], e->args[2]))
    return nul(FAIL(c, "TYPE_REFL", "refl cannot prove the equality: the left side normalizes to %s, the right side normalizes to %s", show(c, e->args[1]), show(c, e->args[2])));
  k->type = e;
  return op3(c, OP_REFL, 0, 0, NULL, NULL, NULL);
}

static const Core *rule_part(Call *k) {
  Checker *c = k->c;
  const Value *pt = NULL;
  const Core *p = infer(c, k->args[0], &pt);
  if (p == NULL)
    return NULL;
  if (!val_is(pt, OP_PROD))
    return nul(FAIL(c, "TYPE_MISMATCH", "%s needs a pair, found a term of type %s", k->b->name, show(c, pt)));
  k->type = pt->args[k->b->part];
  return op3(c, k->b->op, 0, 1, p, NULL, NULL);
}

static const Core *rule_sigma_part(Call *k) {
  Checker *c = k->c;
  const Value *st = NULL;
  const Core *s = infer(c, k->args[0], &st);
  const Core *w;
  if (s == NULL)
    return NULL;
  if (st == NULL || st->kind != VAL_SIGMA)
    return nul(FAIL(c, "TYPE_MISMATCH", "%s needs a Sigma value, found a term of type %s", k->b->name, show(c, st)));
  w = op3(c, OP_WITNESS, 0, 1, s, NULL, NULL);
  k->type = k->b->part == 0u ? st->dom : closure_apply(c->m, st, here(c, w));
  return k->b->part == 0u ? w : op3(c, OP_PAYLOAD, 0, 1, s, NULL, NULL);
}

/* either f g s */
static const Core *rule_either(Call *k) {
  Checker *c = k->c;
  const Value *st = NULL;
  const Value *res = NULL;
  const Core *s = infer(c, k->args[2], &st);
  const Core *f;
  const Core *g;
  if (s == NULL)
    return NULL;
  if (!val_is(st, OP_SUM))
    return nul(FAIL(c, "TYPE_MISMATCH", "either needs a value of type Sum A B, found a term of type %s", show(c, st)));
  f = fn_into(c, k->args[0], st->args[0], k->hint, &res);
  g = f == NULL ? NULL : check(c, k->args[1], val_arrow(c->m, st->args[1], res));
  k->type = res;
  return op3(c, OP_EITHER, 0, 3, f, g, s);
}

/* option z f o */
static const Core *rule_option(Call *k) {
  Checker *c = k->c;
  const Value *ot = NULL;
  const Value *res = k->hint;
  const Core *o = infer(c, k->args[2], &ot);
  const Core *z;
  const Core *f;
  if (o == NULL)
    return NULL;
  if (!val_is(ot, OP_OPTION))
    return nul(FAIL(c, "TYPE_MISMATCH", "option needs a value of type Option A, found a term of type %s", show(c, ot)));
  z = res != NULL ? check(c, k->args[0], res) : infer(c, k->args[0], &res);
  f = z == NULL ? NULL : check(c, k->args[1], val_arrow(c->m, ot->args[0], res));
  k->type = res;
  return op3(c, OP_OPTION_ELIM, 0, 3, z, f, o);
}

static const Core *rule_pure(Call *k) {
  Carrier carrier = CARRIER_OPTION;
  const Value *a = NULL;
  if (!carrier_of(k->hint, &carrier, &a))
    return need_type(k, "Option A, List A or Sum E A");
  k->type = k->hint;
  return op3(k->c, OP_PURE, carrier, 1, check(k->c, k->args[0], a), NULL, NULL);
}

/* map f m */
static const Core *rule_map(Call *k) {
  Checker *c = k->c;
  Carrier carrier = CARRIER_OPTION;
  Carrier hint_carrier = CARRIER_OPTION;
  const Value *mt = NULL;
  const Value *a = NULL;
  const Value *b = NULL;
  const Value *hint_elem = NULL;
  const Core *x = carried_arg(k, k->args[1], k->args[0], &mt);
  const Core *f;
  if (x == NULL)
    return NULL;
  if (!carrier_of(mt, &carrier, &a))
    return not_carrier(k, mt);
  f = fn_into(c, k->args[0], a, carrier_of(k->hint, &hint_carrier, &hint_elem) ? hint_elem : NULL, &b);
  k->type = carried(c, mt, b);
  return op3(c, OP_MAP, carrier, 2, f, x, NULL);
}

/* bind m f */
static const Core *rule_bind(Call *k) {
  Checker *c = k->c;
  Carrier carrier = CARRIER_OPTION;
  Carrier result_carrier = CARRIER_OPTION;
  const Value *mt = NULL;
  const Value *a = NULL;
  const Value *b = NULL;
  const Value *res = NULL;
  const Core *x = carried_arg(k, k->args[0], k->args[1], &mt);
  const Core *f;
  if (x == NULL)
    return NULL;
  if (!carrier_of(mt, &carrier, &a))
    return not_carrier(k, mt);
  f = fn_into(c, k->args[1], a, k->hint, &res);
  if (f == NULL)
    return NULL;
  if (!carrier_of(res, &result_carrier, &b) || result_carrier != carrier)
    return nul(FAIL(c, "TYPE_MISMATCH", "bind needs a function into the carrier of %s, found one into %s", show(c, mt), show(c, res)));
  if (carrier == CARRIER_SUM && !conv_values(c->m, c->level, mt->args[0], res->args[0]))
    return nul(FAIL(c, "TYPE_MISMATCH", "bind must preserve the Sum error type %s, found %s", show(c, mt->args[0]), show(c, res->args[0])));
  k->type = res;
  return op3(c, OP_BIND, carrier, 2, x, f, NULL);
}

/* filter p m */
static const Core *rule_filter(Call *k) {
  Checker *c = k->c;
  Carrier carrier = CARRIER_OPTION;
  const Value *mt = NULL;
  const Value *a = NULL;
  const Core *x = carried_arg(k, k->args[1], k->args[0], &mt);
  const Core *p;
  if (x == NULL)
    return NULL;
  if (!carrier_of(mt, &carrier, &a) || carrier == CARRIER_SUM)
    return nul(FAIL(c, "TYPE_MISMATCH", "filter needs a value of type Option A or List A, found a term of type %s", show(c, mt)));
  p = check(c, k->args[0], val_arrow(c->m, a, flag_type(c)));
  k->type = mt;
  return op3(c, OP_FILTER, carrier, 2, p, x, NULL);
}

/* fold step z n (Nat) or fold f z xs (List A). */
static const Core *fold_simple(Call *k, const Core *s, const Value *st) {
  Checker *c = k->c;
  const Value *res = k->hint;
  const Core *z = res != NULL ? check(c, k->args[1], res) : infer(c, k->args[1], &res);
  int list = val_is(st, OP_LIST);
  const Value *step = list ? val_arrow(c->m, st->args[0], val_arrow(c->m, res, res)) : val_arrow(c->m, res, res);
  const Core *f = z == NULL ? NULL : check(c, k->args[0], step);
  k->used = 3u;
  k->type = res;
  return op3(c, list ? OP_FOLD_LIST : OP_FOLD_NAT, 0, 3, f, z, s);
}

/* The type of the fold case of CTOR: one argument for each field (a
   recursive field arrives folded, so its type is RES), then RES. */
static const Value *case_type(Checker *c, const CtorInfo *ctor, const Env *env, const Value *res) {
  const Value *t = res;
  uint32_t i = ctor->field_count;
  while (i > 0u && t != NULL) {
    const FieldInfo *field = &ctor->fields[--i];
    t = val_arrow(c->m, field->recursive ? res : eval_core(c->m, env, field->type), t);
  }
  return t;
}

/* fold case... x over a domain family: one case for each constructor. */
static const Core *fold_family(Call *k, const Core *s, const Value *st) {
  Checker *c = k->c;
  Machine *m = c->m;
  const FamilyInfo *f = &m->families[st->inst];
  const Env *env = env_of(c, st->args, st->argc);
  const Value *res = k->hint;
  const Core **cores = arena_alloc(m->arena, ((size_t)f->ctor_count + 2u) * sizeof *cores);
  uint32_t j;
  if (cores == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  if (res == NULL && m->ctors[f->first_ctor].field_count == 0u)
    cores[0] = infer(c, k->args[0], &res);
  if (res == NULL)
    return nul(FAIL(c, "TYPE_INFER", "fold over %s needs a declared result type here", f->name));
  for (j = 0; j < f->ctor_count; j++) {
    cores[j] = cores[j] != NULL ? cores[j] : check(c, k->args[j], case_type(c, &m->ctors[f->first_ctor + j], env, res));
    if (cores[j] == NULL)
      return NULL;
  }
  cores[f->ctor_count] = s;
  k->used = f->ctor_count + 1u;
  k->type = res;
  return core_op(c, OP_FOLD_FAMILY, st->inst, 0, cores, f->ctor_count + 1u);
}

static int simple_scrutinee(const Value *t) {
  return val_is(t, OP_NAT) || val_is(t, OP_LIST);
}

/* fold takes its scrutinee last, so the scrutinee type sets the arity. */
static const Core *rule_fold(Call *k) {
  Checker *c = k->c;
  const Value *st = NULL;
  const Core *s = try_infer(c, k->args[k->argc - 1u], &st);
  uint32_t fam = val_is(st, OP_FAMILY) ? st->inst : NO_FAMILY;
  if (s != NULL && fam != NO_FAMILY && c->m->families[fam].ctor_count + 1u == k->argc) {
    k->hint = k->outer;
    return fold_family(k, s, st);
  }
  if (s != NULL && k->argc == 3u && simple_scrutinee(st)) {
    k->hint = k->outer;
    return fold_simple(k, s, st);
  }
  s = k->argc > 3u ? try_infer(c, k->args[2], &st) : NULL;
  if (s != NULL && simple_scrutinee(st)) {
    k->hint = NULL;
    return fold_simple(k, s, st);
  }
  s = infer(c, k->args[k->argc - 1u], &st);
  return s == NULL ? NULL : nul(FAIL(c, "TYPE_MISMATCH", "fold needs a last argument of type Nat, List A or a domain family, and one case for each constructor; found %u arguments and a last argument of type %s", k->argc, show(c, st)));
}

/* unfold g limit seed, with g : S -> Option (Prod A S). */
static const Core *rule_unfold(Call *k) {
  Checker *c = k->c;
  const Value *st = NULL;
  const Value *a;
  const Core *seed;
  const Core *limit;
  const Core *g;
  if (!val_is(k->hint, OP_LIST))
    return need_type(k, "List A");
  a = k->hint->args[0];
  seed = infer(c, k->args[2], &st);
  limit = seed == NULL ? NULL : check(c, k->args[1], nat_type(c));
  g = limit == NULL ? NULL : check(c, k->args[0], val_arrow(c->m, st, tyop(c, OP_OPTION, 1, tyop(c, OP_PROD, 2, a, st, NULL), NULL, NULL)));
  k->type = k->hint;
  return op3(c, OP_UNFOLD, 0, 3, g, limit, seed);
}

static const Core *rule_symm(Call *k) {
  const Value *et = NULL;
  const Core *e = infer_eq(k, 0, &et);
  if (e == NULL)
    return NULL;
  k->type = tyop(k->c, OP_EQ, 3, et->args[0], et->args[2], et->args[1]);
  return op3(k->c, OP_SYMM, 0, 1, e, NULL, NULL);
}

static const Core *rule_trans(Call *k) {
  Checker *c = k->c;
  const Value *t1 = NULL;
  const Value *t2 = NULL;
  const Core *e1 = infer_eq(k, 0, &t1);
  const Core *e2 = e1 == NULL ? NULL : infer_eq(k, 1, &t2);
  if (e2 == NULL)
    return NULL;
  if (!conv_values(c->m, c->level, t1->args[0], t2->args[0]) || !conv_values(c->m, c->level, t1->args[2], t2->args[1]))
    return nul(FAIL(c, "TYPE_MISMATCH", "trans needs the right side of the first proof, %s, to equal the left side of the second, %s", show(c, t1->args[2]), show(c, t2->args[1])));
  k->type = tyop(c, OP_EQ, 3, t1->args[0], t1->args[1], t2->args[2]);
  return op3(c, OP_TRANS, 0, 2, e1, e2, NULL);
}

/* cong f e */
static const Core *rule_cong(Call *k) {
  Checker *c = k->c;
  const Value *et = NULL;
  const Value *bt = NULL;
  const Value *fv;
  const Value *hint_type = val_is(k->hint, OP_EQ) ? k->hint->args[0] : NULL;
  const Core *e = infer_eq(k, 1, &et);
  const Core *f = e == NULL ? NULL : fn_into(c, k->args[0], et->args[0], hint_type, &bt);
  if (f == NULL)
    return NULL;
  fv = here(c, f);
  k->type = tyop(c, OP_EQ, 3, bt, apply_value(c->m, fv, et->args[1]), apply_value(c->m, fv, et->args[2]));
  return op3(c, OP_CONG, 0, 2, f, e, NULL);
}

/* transport P e u */
static const Core *rule_transport(Call *k) {
  Checker *c = k->c;
  const Value *et = NULL;
  const Value *pt = NULL;
  const Value *cod;
  const Value *pv;
  const Core *e = infer_eq(k, 1, &et);
  const Core *p = e == NULL ? NULL : infer(c, k->args[0], &pt);
  const Core *u;
  if (p == NULL)
    return NULL;
  cod = pt != NULL && pt->kind == VAL_PI ? closure_apply(c->m, pt, val_var(c->m, c->level)) : NULL;
  if (cod == NULL || cod->kind != VAL_UNIV || !conv_values(c->m, c->level, pt->dom, et->args[0]))
    return nul(FAIL(c, "TYPE_MISMATCH", "transport needs a family of types over %s, found a term of type %s", show(c, et->args[0]), show(c, pt)));
  pv = here(c, p);
  u = check(c, k->args[2], apply_value(c->m, pv, et->args[1]));
  k->type = apply_value(c->m, pv, et->args[2]);
  return op3(c, OP_TRANSPORT, 0, 3, p, e, u);
}

/* natAdd, u256Add, addrEq, toU256 and the like: each argument has the type
   former ARG. */
static const Core *rule_prim(Call *k, Op arg) {
  Checker *c = k->c;
  const Value *t = tyop(c, arg, 0, NULL, NULL, NULL);
  const Core *x = check(c, k->args[0], t);
  const Core *y = x == NULL || k->b->arity < 2u ? NULL : check(c, k->args[1], t);
  k->type = tyop(c, k->b->result, 0, NULL, NULL, NULL);
  return k->b->arity < 2u ? op3(c, k->b->op, 0, 1, x, NULL, NULL) : op3(c, k->b->op, 0, 2, x, y, NULL);
}

/* flagIf b yes no */
static const Core *rule_flag_if(Call *k) {
  Checker *c = k->c;
  const Value *res = k->hint;
  const Core *b = check(c, k->args[0], flag_type(c));
  const Core *yes = b == NULL ? NULL : (res != NULL ? check(c, k->args[1], res) : infer(c, k->args[1], &res));
  const Core *no = yes == NULL ? NULL : check(c, k->args[2], res);
  k->type = res;
  return op3(c, OP_FLAG_IF, 0, 3, b, yes, no);
}

static int kmap_word(const Value *t, int flag_ok) {
  return val_is(t, OP_NAT) || val_is(t, OP_U256) || val_is(t, OP_ADDR) || (flag_ok && val_is(t, OP_FLAG));
}

/* Map K V (slice K4a0): K is Nat, U256 or Addr. V is Nat, Flag, U256 or Addr. */
static const Core *rule_kmap(Call *k) {
  const Core *t = rule_type2(k);
  const Value *v = here(k->c, t);
  if (v == NULL)
    return NULL;
  if (!kmap_word(v->args[0], 0))
    return nul(FAIL(k->c, "REFUSE_MAP", "the key type of a Map must be Nat, U256 or Addr, found %s", show(k->c, v->args[0])));
  if (!kmap_word(v->args[1], 1))
    return nul(FAIL(k->c, "REFUSE_MAP", "the value type of a Map must be Nat, Flag, U256 or Addr, found %s", show(k->c, v->args[1])));
  return t;
}

/* mapGet m k and mapSet m k v (slice K4a0). The core puts V first, so the
   evaluator can make the zero value of V. */
static const Core *kmap_access(Call *k, int set) {
  Checker *c = k->c;
  const Value *t = set && val_is(k->hint, OP_KMAP) ? k->hint : NULL;
  const Core *map = t != NULL ? check(c, k->args[0], t) : infer(c, k->args[0], &t);
  const Core *key;
  const Core *args[4];
  if (map == NULL)
    return NULL;
  if (!val_is(t, OP_KMAP))
    return nul(FAIL(c, "TYPE_MISMATCH", "%s needs a Map K V, found %s", k->b->name, show(c, t)));
  key = check(c, k->args[1], t->args[0]);
  args[0] = op3(c, t->args[1]->op, 0, 0, NULL, NULL, NULL);
  args[1] = map;
  args[2] = key;
  args[3] = set && key != NULL ? check(c, k->args[2], t->args[1]) : NULL;
  k->type = set ? t : t->args[1];
  return core_op(c, k->b->op, 0, 0, args, set ? 4u : 3u);
}

static const Core *rule(Call *k) {
  switch (k->b->rule) {
  case RULE_TYPE0:
    k->type = val_univ(k->c->m, 0);
    return op3(k->c, k->b->op, 0, 0, NULL, NULL, NULL);
  case RULE_VALUE0:
    k->type = tyop(k->c, k->b->result, 0, NULL, NULL, NULL);
    return op3(k->c, k->b->op, 0, 0, NULL, NULL, NULL);
  case RULE_TYPE1:
    return rule_type1(k);
  case RULE_TYPE2:
    return rule_type2(k);
  case RULE_EQ:
    return rule_eq(k);
  case RULE_PAIR:
    return rule_pair(k);
  case RULE_INJ:
    return rule_inj(k);
  case RULE_EMPTY:
    return rule_empty(k);
  case RULE_SOME:
    return rule_some(k);
  case RULE_CONS:
    return rule_cons(k);
  case RULE_PACK:
    return rule_pack(k);
  case RULE_REFL:
    return rule_refl(k);
  case RULE_PART:
    return rule_part(k);
  case RULE_SIGMA_PART:
    return rule_sigma_part(k);
  case RULE_EITHER:
    return rule_either(k);
  case RULE_OPTION:
    return rule_option(k);
  case RULE_PURE:
    return rule_pure(k);
  case RULE_MAP:
    return rule_map(k);
  case RULE_BIND:
    return rule_bind(k);
  case RULE_FILTER:
    return rule_filter(k);
  case RULE_FOLD:
    return rule_fold(k);
  case RULE_UNFOLD:
    return rule_unfold(k);
  case RULE_SYMM:
    return rule_symm(k);
  case RULE_TRANS:
    return rule_trans(k);
  case RULE_CONG:
    return rule_cong(k);
  case RULE_TRANSPORT:
    return rule_transport(k);
  case RULE_NAT2:
  case RULE_TO_U256:
    return rule_prim(k, OP_NAT);
  case RULE_U256_2:
    return rule_prim(k, OP_U256);
  case RULE_ADDR2:
    return rule_prim(k, OP_ADDR);
  case RULE_FLAG_IF:
    return rule_flag_if(k);
  case RULE_KMAP:
    return rule_kmap(k);
  case RULE_KMAP_GET:
    return kmap_access(k, 0);
  case RULE_KMAP_SET:
    return kmap_access(k, 1);
  }
  return NULL;
}

/* Checks the family parameters ARGS. VALS gets their values. */
static int check_params(Checker *c, const FamilyInfo *f, const Term *const *args, const Core **cores, const Value **vals) {
  uint32_t i;
  for (i = 0; i < f->param_count; i++) {
    cores[i] = check(c, args[i], eval_core(c->m, env_of(c, vals, i), f->param_types[i]));
    vals[i] = here(c, cores[i]);
    if (vals[i] == NULL)
      return 0;
  }
  return 1;
}

static const Core *family_app(Call *k, uint32_t fam) {
  Checker *c = k->c;
  const FamilyInfo *f = &c->m->families[fam];
  const Core **cores = arena_alloc(c->m->arena, ((size_t)f->param_count + 1u) * sizeof *cores);
  const Value **vals = arena_alloc(c->m->arena, ((size_t)f->param_count + 1u) * sizeof *vals);
  if (cores == NULL || vals == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  if (!check_params(c, f, k->args, cores, vals))
    return NULL;
  k->type = val_univ(c->m, f->universe);
  return core_op(c, OP_FAMILY, fam, 0, cores, f->param_count);
}

/* A constructor takes the family parameters (the indices), then its fields. */
static const Core *ctor_app(Call *k, uint32_t ctor) {
  Checker *c = k->c;
  const CtorInfo *ci = &c->m->ctors[ctor];
  const FamilyInfo *f = &c->m->families[ci->family];
  uint32_t p = f->param_count;
  uint32_t n = p + ci->field_count;
  const Core **cores = arena_alloc(c->m->arena, ((size_t)n + 1u) * sizeof *cores);
  const Value **vals = arena_alloc(c->m->arena, ((size_t)p + 1u) * sizeof *vals);
  const Env *env;
  uint32_t j;
  if (cores == NULL || vals == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  if (!check_params(c, f, k->args, cores, vals))
    return NULL;
  env = env_of(c, vals, p);
  for (j = 0; j < ci->field_count; j++) {
    cores[p + j] = check(c, k->args[p + j], eval_core(c->m, env, ci->fields[j].type));
    if (cores[p + j] == NULL)
      return NULL;
  }
  k->type = val_op(c->m, VAL_OP, OP_FAMILY, ci->family, 0, vals, p);
  return core_op(c, OP_CTOR, ctor, 0, cores, n);
}

/* A field of a one-constructor family is a projection. */
static const Core *proj_app(Call *k, uint32_t ctor, uint32_t field) {
  Checker *c = k->c;
  const CtorInfo *ci = &c->m->ctors[ctor];
  const Value *xt = NULL;
  const Core *x = infer(c, k->args[0], &xt);
  if (x == NULL)
    return NULL;
  if (!val_is(xt, OP_FAMILY) || xt->inst != ci->family)
    return nul(FAIL(c, "TYPE_MISMATCH", "%s needs a value of the family %s, found a term of type %s", ci->fields[field].name, c->m->families[ci->family].name, show(c, xt)));
  k->type = eval_core(c->m, env_of(c, xt->args, xt->argc), ci->fields[field].type);
  return core_op(c, OP_PROJ, ctor, field, &x, 1);
}

static int find_known(Checker *c, const char *name, Head *h) {
  const Machine *m = c->m;
  uint32_t i;
  uint32_t j;
  for (i = 0; i < m->family_count; i++) {
    if (strcmp(m->families[i].name, name) == 0) {
      h->kind = HEAD_FAMILY;
      h->index = i;
      c->self_uses += i == c->self;
      return 1;
    }
  }
  for (i = 0; i < m->ctor_count; i++) {
    if (strcmp(m->ctors[i].name, name) == 0) {
      h->kind = HEAD_CTOR;
      h->index = i;
      return 1;
    }
  }
  for (i = 0; i < m->ctor_count; i++) {
    for (j = 0; m->families[m->ctors[i].family].ctor_count == 1u && j < m->ctors[i].field_count; j++) {
      if (strcmp(m->ctors[i].fields[j].name, name) == 0) {
        h->kind = HEAD_PROJ;
        h->index = i;
        h->field = j;
        return 1;
      }
    }
  }
  for (i = 0; i < BUILTIN_COUNT; i++) {
    if (strcmp(BUILTINS[i].name, name) == 0) {
      h->kind = HEAD_BUILTIN;
      h->builtin = &BUILTINS[i];
      return 1;
    }
  }
  return 0;
}

/* An unknown name. When it names this declaration or a later one, the use
   is recursion (rule R3). */
static int unknown_name(Checker *c, const char *name) {
  size_t i;
  for (i = c->decl_index; i < c->decls->count; i++) {
    if (strcmp(c->decls->items[i].name, name) == 0)
      return FAIL(c, "REFUSE_REC", "%s is this definition or a later one; a definition can use only earlier ones (rule R3)", name);
  }
  return FAIL(c, "NAME_UNKNOWN", "unknown name %s", name);
}

/* Name resolution order: locals (the innermost first), definitions,
   families, constructors, projections, core names. */
static int resolve_head(Checker *c, const Term *head, Head *h) {
  memset(h, 0, sizeof *h);
  h->kind = HEAD_TERM;
  if (head->kind != TERM_VAR || find_local(c, head->name) != NULL || find_def(c->m, head->name) != NO_DEF)
    return 1;
  return find_known(c, head->name, h) ? 1 : unknown_name(c, head->name);
}

static uint32_t known_arity(const Checker *c, const Head *h) {
  const CtorInfo *ci = h->kind == HEAD_CTOR ? &c->m->ctors[h->index] : NULL;
  switch (h->kind) {
  case HEAD_TERM:
    return 0u;
  case HEAD_FAMILY:
    return c->m->families[h->index].param_count;
  case HEAD_CTOR:
    return ci == NULL ? 0u : c->m->families[ci->family].param_count + ci->field_count;
  case HEAD_PROJ:
    return 1u;
  case HEAD_BUILTIN:
    return h->builtin->arity;
  }
  return 0u;
}

static const char *known_name(const Checker *c, const Head *h) {
  switch (h->kind) {
  case HEAD_TERM:
    return "?";
  case HEAD_FAMILY:
    return c->m->families[h->index].name;
  case HEAD_CTOR:
    return c->m->ctors[h->index].name;
  case HEAD_PROJ:
    return c->m->ctors[h->index].fields[h->field].name;
  case HEAD_BUILTIN:
    return h->builtin->name;
  }
  return "?";
}

static const Core *known_core(Call *k, const Head *h) {
  switch (h->kind) {
  case HEAD_TERM:
    return nul(FAIL(k->c, "INTERNAL", "a plain term reached the core name rules"));
  case HEAD_FAMILY:
    return family_app(k, h->index);
  case HEAD_CTOR:
    return ctor_app(k, h->index);
  case HEAD_PROJ:
    return proj_app(k, h->index, h->field);
  case HEAD_BUILTIN:
    return rule(k);
  }
  return NULL;
}

/* An under-applied core name checked against a function type: WHOLE becomes
   `fun x => WHOLE x`, with a name that the lexer cannot make. */
static const Core *eta(Checker *c, const Term *whole, const Value *hint, const Value **type) {
  char *name = arena_alloc(c->m->arena, 24);
  Term *x = arena_alloc(c->m->arena, sizeof *x);
  Term *app = arena_alloc(c->m->arena, sizeof *app);
  Term *lam = arena_alloc(c->m->arena, sizeof *lam);
  if (name == NULL || x == NULL || app == NULL || lam == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  c->eta_count++;
  snprintf(name, 24, "%%eta%u", c->eta_count);
  x->kind = TERM_VAR;
  x->name = name;
  app->kind = TERM_APP;
  app->left = whole;
  app->right = x;
  lam->kind = TERM_LAM;
  lam->name = name;
  lam->right = app;
  return check_lam(c, lam, hint, type);
}

/* Applies K of type TY to ARGS, one by one. */
static const Core *apply_rest(Checker *c, const Core *k, const Value *ty, const Term *const *args, uint32_t argc, const Value *hint, const Value **type) {
  uint32_t i;
  for (i = 0; i < argc && k != NULL; i++) {
    const Core *a;
    if (ty == NULL || ty->kind != VAL_PI)
      return nul(FAIL(c, "TYPE_NOT_FUNCTION", "a term of type %s is applied to an argument", show(c, ty)));
    a = check(c, args[i], ty->dom);
    ty = closure_apply(c->m, ty, here(c, a));
    k = core_app(c, k, a);
  }
  *type = ty;
  return finish(c, k, hint, type);
}

static const Core *var_core(Checker *c, const char *name, const Value **type) {
  const Scope *s = find_local(c, name);
  uint32_t d = find_def(c->m, name);
  if (s != NULL) {
    *type = s->type;
    return core_index(c, CORE_VAR, c->level - 1u - s->level);
  }
  if (d != NO_DEF) {
    *type = c->m->defs[d].type;
    return core_index(c, CORE_GLOBAL, d);
  }
  return nul(FAIL(c, "NAME_UNKNOWN", "unknown name %s", name));
}

static const Core *apply_term(Checker *c, const Term *head, const Term *const *args, uint32_t argc, const Value *hint, const Value **type) {
  const Value *ty = NULL;
  const Core *k = head->kind == TERM_VAR ? var_core(c, head->name, &ty) : infer(c, head, &ty);
  return apply_rest(c, k, ty, args, argc, hint, type);
}

static const Core *apply_known(Checker *c, const Term *whole, const Head *h, const Term *const *args, uint32_t argc, const Value *hint, const Value **type) {
  uint32_t arity = known_arity(c, h);
  Call k;
  const Core *core;
  if (argc < arity && hint != NULL && hint->kind == VAL_PI)
    return eta(c, whole, hint, type);
  if (argc < arity)
    return nul(FAIL(c, "TYPE_ARITY", "%s needs %u arguments, found %u", known_name(c, h), arity, argc));
  memset(&k, 0, sizeof k);
  k.c = c;
  k.b = h->builtin;
  k.args = args;
  k.argc = argc;
  k.hint = argc == arity ? hint : NULL;
  k.outer = hint;
  k.used = arity;
  core = known_core(&k, h);
  return apply_rest(c, core, k.type, args + k.used, argc - k.used, hint, type);
}

static const Core *elab_spine(Checker *c, const Term *t, const Value *hint, const Value **type) {
  const Term **args;
  const Term *head = t;
  uint32_t argc = 0;
  uint32_t i;
  Head h;
  while (head->kind == TERM_APP) {
    argc++;
    head = head->left;
  }
  if (argc > SPINE_MAX)
    return nul(FAIL(c, "TYPE_ARITY", "an application has more than %u arguments", SPINE_MAX));
  args = arena_alloc(c->m->arena, ((size_t)argc + 1u) * sizeof *args);
  if (args == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  for (head = t, i = argc; head->kind == TERM_APP; head = head->left)
    args[--i] = head->right;
  if (!resolve_head(c, head, &h))
    return NULL;
  return h.kind == HEAD_TERM ? apply_term(c, head, args, argc, hint, type) : apply_known(c, t, &h, args, argc, hint, type);
}

static int annotation_ok(Checker *c, const Term *ann, const Value *dom) {
  uint64_t level = 0;
  const Value *v = here(c, check_type(c, ann, &level));
  if (v == NULL)
    return 0;
  return conv_values(c->m, c->level, v, dom) ? 1 : FAIL(c, "TYPE_MISMATCH", "the binder type %s differs from the expected type %s", show(c, v), show(c, dom));
}

static const Core *check_lam(Checker *c, const Term *t, const Value *hint, const Value **type) {
  const Core *body;
  if (hint->kind != VAL_PI)
    return nul(FAIL(c, "TYPE_MISMATCH", "a function is not a term of type %s", show(c, hint)));
  if (t->left != NULL && !annotation_ok(c, t->left, hint->dom))
    return NULL;
  if (!push(c, t->name, hint->dom))
    return NULL;
  body = check(c, t->right, closure_apply(c->m, hint, c->env->value));
  pop(c);
  *type = hint;
  return core_bind(c, CORE_LAM, t->name, NULL, body);
}

static const Core *infer_lam(Checker *c, const Term *t, const Value **type) {
  uint64_t level = 0;
  const Value *dom;
  const Value *bt = NULL;
  const Core *body;
  const Core *quoted;
  if (t->left == NULL)
    return nul(FAIL(c, "TYPE_INFER", "fun %s => ... needs a binder type here", t->name));
  dom = here(c, check_type(c, t->left, &level));
  if (!push(c, t->name, dom))
    return NULL;
  body = infer(c, t->right, &bt);
  quoted = body == NULL ? NULL : quote_value(c->m, c->level, bt);
  pop(c);
  *type = val_closure(c->m, VAL_PI, t->name, dom, c->env, quoted);
  return core_bind(c, CORE_LAM, t->name, NULL, body);
}

/* (x : A) -> B and Sigma (x : A) B. */
static const Core *elab_binder(Checker *c, const Term *t, CoreKind kind, const Value *hint, const Value **type) {
  uint64_t la = 0;
  uint64_t lb = 0;
  const Core *a = check_type(c, t->left, &la);
  const Core *b;
  if (a == NULL || !push(c, t->name, here(c, a)))
    return NULL;
  b = check_type(c, t->right, &lb);
  pop(c);
  *type = val_univ(c->m, la > lb ? la : lb);
  return finish(c, core_bind(c, kind, t->name, a, b), hint, type);
}

static const Core *elab_inner(Checker *c, const Term *t, const Value *hint, const Value **type) {
  switch (t->kind) {
  case TERM_VAR:
  case TERM_APP:
    return elab_spine(c, t, hint, type);
  case TERM_NAT:
    *type = nat_type(c);
    return finish(c, core_leaf(c, CORE_NAT, t->nat), hint, type);
  case TERM_WORD:
    *type = tyop(c, t->nat == WORD_ADDR ? OP_ADDR : OP_U256, 0, NULL, NULL, NULL);
    return finish(c, core_word(c, t->nat, t->word), hint, type);
  case TERM_LAM:
    return hint != NULL ? check_lam(c, t, hint, type) : infer_lam(c, t, type);
  case TERM_PI:
    return elab_binder(c, t, CORE_PI, hint, type);
  case TERM_SIGMA:
    return elab_binder(c, t, CORE_SIGMA, hint, type);
  case TERM_UNIV:
    if (t->nat >= UNIVERSE_MAX)
      return nul(FAIL(c, "TYPE_UNIVERSE", "Type %llu is above the largest universe Type %u", (unsigned long long)t->nat, UNIVERSE_MAX - 1u));
    *type = val_univ(c->m, t->nat + 1u);
    return finish(c, core_leaf(c, CORE_UNIV, t->nat), hint, type);
  }
  return NULL;
}

static const Core *elab(Checker *c, const Term *t, const Value *hint, const Value **type) {
  const Core *k;
  if (c->depth >= CHECK_DEPTH_LIMIT)
    return nul(FAIL(c, "CHECK_DEPTH", "the term is nested deeper than %u levels", CHECK_DEPTH_LIMIT));
  c->depth++;
  k = elab_inner(c, t, hint, type);
  c->depth--;
  return k;
}

/* NULL when NAME is free, else the reason it is taken (rule R4). */
static const char *name_owner(const Checker *c, const char *name) {
  const Machine *m = c->m;
  uint32_t i;
  uint32_t j;
  for (i = 0; i < sizeof RESERVED / sizeof RESERVED[0]; i++) {
    if (strcmp(RESERVED[i], name) == 0)
      return "a reserved name";
  }
  for (i = 0; i < BUILTIN_COUNT; i++) {
    if (strcmp(BUILTINS[i].name, name) == 0)
      return "a core name";
  }
  for (i = 0; i < m->family_count; i++) {
    if (strcmp(m->families[i].name, name) == 0)
      return "a domain family";
  }
  for (i = 0; i < m->ctor_count; i++) {
    for (j = 0; j < m->ctors[i].field_count; j++) {
      if (strcmp(m->ctors[i].fields[j].name, name) == 0)
        return "a domain field";
    }
    if (strcmp(m->ctors[i].name, name) == 0)
      return "a domain constructor";
  }
  i = find_def(m, name);
  if (i == NO_DEF)
    return NULL;
  return m->defs[i].origin == ORIGIN_DOMAIN ? "a domain definition" : "already defined";
}

static int name_free(Checker *c, const char *name) {
  const char *why = name_owner(c, name);
  return why == NULL ? 1 : FAIL(c, "REFUSE_NAME", "%s is %s (rule R4)", name, why);
}

/* A recursive field must be the whole field type, in a family with no
   parameters. */
static int check_field(Checker *c, uint32_t fam, const Field *field, FieldInfo *out) {
  uint64_t level = 0;
  const Core *t;
  int whole;
  if (!name_free(c, field->name))
    return 0;
  c->self_uses = 0;
  t = check_type(c, field->type, &level);
  if (t == NULL)
    return 0;
  if (level > c->m->families[fam].universe)
    c->m->families[fam].universe = level;
  whole = t->kind == CORE_OP && t->op == OP_FAMILY && t->inst == fam && c->m->families[fam].param_count == 0u;
  out->name = field->name;
  out->type = t;
  out->recursive = c->self_uses > 0u;
  out->history = field->history;
  if (out->recursive && !whole)
    return FAIL(c, "DOMAIN_FAMILY", "the recursive field %s must have the type %s itself, in a family with no parameters", field->name, c->m->families[fam].name);
  return 1;
}

static int check_ctor(Checker *c, uint32_t fam, const Ctor *ctor) {
  Machine *m = c->m;
  FieldInfo *fields = arena_alloc(m->arena, (ctor->field_count + 1u) * sizeof *fields);
  CtorInfo *ci = &m->ctors[m->ctor_count];
  size_t j;
  if (fields == NULL)
    return FAIL(c, "OOM", "out of memory");
  if (!name_free(c, ctor->name))
    return 0;
  for (j = 0; j < ctor->field_count; j++) {
    if (strcmp(ctor->fields[j].name, ctor->name) == 0)
      return FAIL(c, "REFUSE_NAME", "%s is this domain constructor (rule R4)", ctor->name);
    for (size_t k = 0; k < j; k++)
      if (strcmp(ctor->fields[j].name, ctor->fields[k].name) == 0)
        return FAIL(c, "REFUSE_NAME", "%s is already a field of this domain constructor (rule R4)", ctor->fields[j].name);
    if (!check_field(c, fam, &ctor->fields[j], &fields[j]))
      return 0;
  }
  ci->name = ctor->name;
  ci->family = fam;
  ci->fields = fields;
  ci->field_count = (uint32_t)ctor->field_count;
  m->ctor_count++;
  return 1;
}

/* A family is in scope in its own fields, so a field can be recursive. */
static int check_family(Checker *c, const Decl *d) {
  Machine *m = c->m;
  uint32_t fam = m->family_count;
  FamilyInfo *f = &m->families[fam];
  const Core **params = arena_alloc(m->arena, (d->param_count + 1u) * sizeof *params);
  size_t i;
  if (params == NULL)
    return FAIL(c, "OOM", "out of memory");
  for (i = 0; i < d->param_count; i++) {
    uint64_t level = 0;
    params[i] = check_type(c, d->params[i].type, &level);
    if (!push(c, d->params[i].name, here(c, params[i])))
      return 0;
  }
  f->name = d->name;
  f->param_types = params;
  f->param_count = (uint32_t)d->param_count;
  f->first_ctor = m->ctor_count;
  f->ctor_count = (uint32_t)d->ctor_count;
  m->family_count++;
  c->self = fam;
  for (i = 0; i < d->ctor_count; i++) {
    if (!check_ctor(c, fam, &d->ctors[i]))
      return 0;
  }
  return 1;
}

/* Rule D-9 (slice K3c). The argument A of the history field K of the state
   constructor CTOR is `F t` or `ADD (F t) e`, with F the projection of the
   field. ADD is natAdd or u256Add. The type of the field selects one of
   the two, so this function does not compare them. */
static int history_arg_ok(uint32_t ctor, uint32_t k, const Core *a) {
  const Core *x = a->kind == CORE_OP && (a->op == OP_U256_ADD || a->op == OP_NAT_ADD) && a->argc == 2u ? a->args[0] : a;
  return x->kind == CORE_OP && x->op == OP_PROJ && x->inst == ctor && x->field == k;
}

static int is_family(const Value *t, uint32_t family);

/* Rule D-9 (USER ruling A). The rule does not check the body of init or of
   a definition with the type State after normalization. */
static int history_root(const Machine *m, uint32_t j) {
  return strcmp(m->defs[j].name, "init") == 0 || is_family(m->defs[j].type, m->state_family);
}

static int has_history(const Machine *m) {
  const CtorInfo *ci;
  uint32_t i;
  if (!m->has_state)
    return 0;
  ci = &m->ctors[m->families[m->state_family].first_ctor];
  for (i = 0; i < ci->field_count; i++) {
    if (ci->fields[i].history)
      return 1;
  }
  return 0;
}

/* 1 if the family State occurs in the type value V (USER ruling A). The
   codomain of a Pi or a Sigma opens at a fresh level, as conv_values does.
   A variable can stand for State, including the payload type of an
   existential package. A Lam, a stuck operation or a value that does not
   open also gives 1. */
static int mentions_state(Machine *m, uint32_t level, const Value *v) {
  uint32_t i;
  if (v == NULL)
    return 1;
  switch (v->kind) {
  case VAL_NAT:
  case VAL_WORD:
  case VAL_TRAP:
  case VAL_UNIV:
    return 0;
  case VAL_PI:
  case VAL_SIGMA:
    return mentions_state(m, level, v->dom) || mentions_state(m, level + 1u, closure_apply(m, v, val_var(m, level)));
  case VAL_APP:
    return mentions_state(m, level, v->dom) || mentions_state(m, level, v->arg);
  case VAL_LAM:
  case VAL_STUCK:
  case VAL_VAR:
    return 1;
  case VAL_OP:
    break;
  }
  if (is_family(v, m->state_family))
    return 1;
  for (i = 0; i < v->argc; i++) {
    if (mentions_state(m, level, v->args[i]))
      return 1;
  }
  return 0;
}

/* Rule D-9 (slice K3c) on the core term T of a definition. The names are
   resolved, so a binder with the name of a field is a local and not the
   field. Each use of the state constructor has all of its arguments and
   keeps or adds to each history field. TYPED is 1 when State occurs in the
   type of the definition: then T cannot refer to a definition that the rule
   does not check (history_root). */
static int history_ok(Checker *c, const Core *t, int typed) {
  const Machine *m = c->m;
  uint32_t i;
  if (t == NULL || !m->has_state)
    return 1;
  switch (t->kind) {
  case CORE_VAR:
  case CORE_NAT:
  case CORE_WORD:
  case CORE_TRAP:
  case CORE_UNIV:
    return 1;
  case CORE_GLOBAL:
    if (typed && history_root(m, t->index))
      return FAIL(c, "REFUSE_HISTORY_WRITE", "a definition with State in its type cannot refer to %s, because the history rule does not check %s (rule D-9)", m->defs[t->index].name, m->defs[t->index].name);
    return 1;
  case CORE_LAM:
  case CORE_PI:
  case CORE_SIGMA:
  case CORE_APP:
    return history_ok(c, t->a, typed) && history_ok(c, t->b, typed);
  case CORE_OP:
    break;
  }
  if (t->op == OP_CTOR && m->ctors[t->inst].family == m->state_family) {
    const CtorInfo *ci = &m->ctors[t->inst];
    if (t->argc < ci->field_count)
      return FAIL(c, "REFUSE_HISTORY_WRITE", "%s must have all of its arguments (rule D-9)", ci->name);
    for (i = 0; i < ci->field_count; i++) {
      if (ci->fields[i].history && !history_arg_ok(t->inst, i, t->args[t->argc - ci->field_count + i]))
        return FAIL(c, "REFUSE_HISTORY_WRITE", "the history field %s must be (%s t) or ADD (%s t) e, with ADD u256Add or natAdd (rule D-9)", ci->fields[i].name, ci->fields[i].name, ci->fields[i].name);
    }
  }
  for (i = 0; i < t->argc; i++) {
    if (!history_ok(c, t->args[i], typed))
      return 0;
  }
  return 1;
}

static int check_def(Checker *c, const Decl *d) {
  Machine *m = c->m;
  uint64_t level = 0;
  const Core *t = check_type(c, d->type, &level);
  const Value *tv = here(c, t);
  const Core *body = check(c, d->body, tv);
  DefInfo *info = &m->defs[m->def_count];
  if (body == NULL)
    return 0;
  if (has_history(m) && strcmp(d->name, "init") != 0 && !is_family(tv, m->state_family) && !history_ok(c, body, mentions_state(m, c->level, tv)))
    return 0;
  info->name = d->name;
  info->origin = d->origin;
  info->body = body;
  info->type = tv;
  info->value = NULL;
  m->def_count++;
  return 1;
}

/* First order (slice K3a): Nat, Flag, U256, Addr, and Option, Prod and List
   of first-order types, after normalization. */
static int first_order(const Value *t) {
  uint32_t i;
  if (t == NULL || t->kind != VAL_OP)
    return 0;
  if (t->op == OP_NAT || t->op == OP_FLAG || t->op == OP_U256 || t->op == OP_ADDR)
    return 1;
  if (t->op != OP_OPTION && t->op != OP_PROD && t->op != OP_LIST)
    return 0;
  for (i = 0; i < t->argc; i++) {
    if (!first_order(t->args[i]))
      return 0;
  }
  return 1;
}

/* The state form (slice K3a): one `state State` with no parameters, one
   constructor and first-order fields. A history field is a Nat or a U256. */
static int check_state(Checker *c, const Decl *d) {
  Machine *m = c->m;
  const CtorInfo *ci;
  uint32_t j;
  if (m->has_state)
    return FAIL(c, "REFUSE_STATE", "a program can declare one state only");
  if (strcmp(d->name, "State") != 0)
    return FAIL(c, "REFUSE_STATE", "the state type must have the name State, found %s", d->name);
  if (d->param_count != 0u || d->ctor_count != 1u)
    return FAIL(c, "REFUSE_STATE", "the state must have no parameters and one constructor");
  if (!name_free(c, d->name) || !check_family(c, d))
    return 0;
  ci = &m->ctors[m->families[m->family_count - 1u].first_ctor];
  for (j = 0; j < ci->field_count; j++) {
    const Value *t = here(c, ci->fields[j].type);
    if (t == NULL)
      return 0;
    if (!first_order(t) && !val_is(t, OP_KMAP))
      return FAIL(c, "REFUSE_STATE", "the state field %s must have a first-order type: Nat, Flag, U256, Addr, Option, Prod or List of them, or a Map K V", ci->fields[j].name);
    if (ci->fields[j].history && t->op != OP_NAT && t->op != OP_U256)
      return FAIL(c, "REFUSE_STATE", "the history field %s must have the type Nat or U256", ci->fields[j].name);
  }
  m->has_state = 1;
  m->state_family = m->family_count - 1u;
  return 1;
}

static int check_decl(Checker *c, const Decl *d) {
  if (d->is_state)
    return check_state(c, d);
  if (d->kind == DECL_FAMILY && d->origin == ORIGIN_PROGRAM)
    return FAIL(c, "REFUSE_DATA", "a program cannot declare a family (rule R1); only domain/domain.lang can");
  if (d->kind == DECL_AXIOM)
    return FAIL(c, "REFUSE_AXIOM", "an axiom has no proof (rule R2)");
  if (d->is_rec)
    return FAIL(c, "REFUSE_REC", "def rec is refused (rule R3); use fold or unfold");
  if (!name_free(c, d->name))
    return 0;
  return d->kind == DECL_FAMILY ? check_family(c, d) : check_def(c, d);
}

int check_program(Arena *arena, const DeclList *decls, Machine *m, Diag *diag) {
  Checker c;
  size_t ctors = 0;
  size_t i;
  memset(m, 0, sizeof *m);
  m->arena = arena;
  m->diag = diag;
  m->fuel = EVAL_FUEL_STEPS;
  for (i = 0; i < decls->count; i++)
    ctors += decls->items[i].ctor_count;
  m->defs = arena_alloc(arena, (decls->count + 1u) * sizeof *m->defs);
  m->families = arena_alloc(arena, (decls->count + 1u) * sizeof *m->families);
  m->ctors = arena_alloc(arena, (ctors + 1u) * sizeof *m->ctors);
  if (m->defs == NULL || m->families == NULL || m->ctors == NULL)
    return diag_fail(diag, "OOM", NULL, "out of memory");
  m->def_cap = (uint32_t)decls->count;
  m->family_cap = (uint32_t)decls->count;
  m->ctor_cap = (uint32_t)ctors;
  memset(&c, 0, sizeof c);
  c.m = m;
  c.decls = decls;
  for (i = 0; i < decls->count; i++) {
    c.decl_index = i;
    c.scope = NULL;
    c.env = NULL;
    c.level = 0;
    c.depth = 0;
    c.self = NO_FAMILY;
    m->def = decls->items[i].name;
    if (!check_decl(&c, &decls->items[i]))
      return 0;
  }
  m->def = NULL;
  return 1;
}

int entry_of(Machine *m, const Value *type, Entry *out) {
  const Value *t = type;
  memset(out, 0, sizeof *out);
  while (t != NULL && t->kind == VAL_PI && out->param_count < ENTRY_PARAMS_MAX) {
    if (!val_is(t->dom, OP_NAT) && !val_is(t->dom, OP_FLAG))
      return 0;
    out->param_flag[out->param_count] = val_is(t->dom, OP_FLAG);
    t = closure_apply(m, t, val_var(m, out->param_count));
    out->param_count++;
  }
  out->result_flag = val_is(t, OP_FLAG);
  return val_is(t, OP_NAT) || val_is(t, OP_FLAG);
}

static int parse_u64(const char *s, uint64_t *out) {
  uint64_t n = 0;
  size_t i;
  if (s[0] == '\0')
    return 0;
  for (i = 0; s[i] != '\0'; i++) {
    unsigned d = (unsigned)(unsigned char)s[i] - (unsigned)'0';
    if (d > 9u || n > (UINT64_MAX - d) / 10u)
      return 0;
    n = n * 10u + d;
  }
  *out = n;
  return 1;
}

/* The type of an entry argument (slice K3b adds U256 and Addr). */
typedef enum { ARG_TYPE_NAT, ARG_TYPE_FLAG, ARG_TYPE_U256, ARG_TYPE_ADDR } ArgType;

static const char *const ARG_TEXT[] = {
  "Nat (0 to 2^64-1)", "Flag (0 or 1)", "U256 (5u or 0x and hex digits, then u)", "Addr (0x and 40 hex digits)",
};

static int hex_digit(char ch, int hex) {
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (hex && ch >= 'a' && ch <= 'f')
    return ch - 'a' + 10;
  if (hex && ch >= 'A' && ch <= 'F')
    return ch - 'A' + 10;
  return -1;
}

/* A word literal: an Addr is 0x and 40 hex digits; a U256 is decimal digits,
   or 0x and hex digits, then u. Returns 1 when TEXT is one. */
static int parse_word(const char *text, uint64_t sort, U256 *out) {
  size_t n = strlen(text);
  int hex = n > 2u && text[0] == '0' && text[1] == 'x';
  size_t start = hex ? 2u : 0u;
  size_t end = sort == WORD_U256 && n > 0u ? n - 1u : n;
  size_t i;
  if (end <= start || (sort == WORD_U256 && text[end] != 'u') || (sort == WORD_ADDR && (!hex || n != 42u)))
    return 0;
  u256_from_u64(out, 0);
  for (i = start; i < end; i++) {
    int d = hex_digit(text[i], hex);
    if (d < 0 || !u256_scale_add(out, hex ? 16u : 10u, (uint32_t)d))
      return 0;
  }
  return 1;
}

static const Value *word_arg(Machine *m, const char *text, uint64_t sort) {
  U256 *word = arena_alloc(m->arena, sizeof *word);
  if (word == NULL) {
    diag_fail(m->diag, "OOM", m->def, "out of memory");
    return NULL;
  }
  return parse_word(text, sort, word) ? val_word(m, sort, word) : NULL;
}

/* An entry argument: a Nat in decimal, a Flag as 0 or 1, a U256 or an Addr
   in the literal form of the language. */
static const Value *entry_arg(Machine *m, const char *text, ArgType type) {
  uint64_t n = 0;
  const Value *v = NULL;
  switch (type) {
  case ARG_TYPE_NAT:
    v = parse_u64(text, &n) ? val_nat(m, n) : NULL;
    break;
  case ARG_TYPE_FLAG:
    v = parse_u64(text, &n) && n <= 1u ? val_make(m, n == 1u ? OP_FLAG_YES : OP_FLAG_NO, 0, 0, NULL, NULL, NULL) : NULL;
    break;
  case ARG_TYPE_U256:
    v = word_arg(m, text, WORD_U256);
    break;
  case ARG_TYPE_ADDR:
    v = word_arg(m, text, WORD_ADDR);
    break;
  }
  if (v == NULL && m->diag->code == NULL)
    diag_fail(m->diag, "EVAL_ARGS", m->def, "'%s' is not a %s", text, ARG_TEXT[type]);
  return v;
}

static int print_result(Machine *m, const char *name, const Value *v, FILE *out, FILE *err) {
  switch (v->kind) {
  case VAL_NAT:
    fprintf(out, "%llu\n", (unsigned long long)v->nat);
    return 0;
  case VAL_TRAP:
    fputs("trap\n", out);
    fprintf(err, "langc: EVAL_OVERFLOW: %s: an operation trapped: a Nat or U256 overflow, or a U256 division by 0\n", name);
    return 0;
  case VAL_OP:
    if (val_is(v, OP_FLAG_YES) || val_is(v, OP_FLAG_NO)) {
      fprintf(out, "%d\n", val_is(v, OP_FLAG_YES));
      return 0;
    }
    break;
  case VAL_WORD:
  case VAL_UNIV:
  case VAL_LAM:
  case VAL_PI:
  case VAL_SIGMA:
  case VAL_VAR:
  case VAL_APP:
  case VAL_STUCK:
    break;
  }
  diag_fail(m->diag, "EVAL_ENTRY", name, "the entry did not compute a number");
  return 1;
}

int eval_command(Machine *m, const char *name, char *const *args, int arg_count, FILE *out, FILE *err) {
  uint32_t d = find_def(m, name);
  Entry entry;
  const Value *v;
  char *buf;
  uint32_t i;
  m->def = name;
  if (d == NO_DEF) {
    diag_fail(m->diag, "EVAL_ENTRY", name, "no definition has this name");
    return 1;
  }
  v = def_value(m, d);
  if (v == NULL)
    return 1;
  if (!entry_of(m, m->defs[d].type, &entry) && arg_count != 0) {
    diag_fail(m->diag, "EVAL_ARGS", name, "the definition is not an entry, so it takes no arguments");
    return 2;
  }
  if (!entry_of(m, m->defs[d].type, &entry)) {
    buf = arena_alloc(m->arena, PRINT_MAX);
    if (buf == NULL)
      return diag_fail(m->diag, "OOM", name, "out of memory") + 1;
    if (!value_print(m, NULL, 0, v, buf, PRINT_MAX)) {
      diag_fail(m->diag, "EVAL_PRINT", name, "the normal form exceeds the printer limits");
      return 1;
    }
    fprintf(out, "%s\n", buf);
    return 0;
  }
  if ((uint32_t)arg_count != entry.param_count) {
    diag_fail(m->diag, "EVAL_ARGS", name, "the entry takes %u arguments, found %d", entry.param_count, arg_count);
    return 2;
  }
  for (i = 0; i < entry.param_count && v != NULL; i++) {
    const Value *arg = entry_arg(m, args[i], entry.param_flag[i] ? ARG_TYPE_FLAG : ARG_TYPE_NAT);
    v = arg == NULL ? NULL : apply_value(m, v, arg);
  }
  if (v == NULL)
    return strcmp(m->diag->code != NULL ? m->diag->code : "", "EVAL_ARGS") == 0 ? 2 : 1;
  return print_result(m, name, v, out, err);
}

/* The role of a definition in a run (slice K3b, D-3), found by its type. */
typedef enum { ROLE_HELPER, ROLE_ENTRY, ROLE_VIEW } Role;

typedef struct {
  uint32_t param_count;
  ArgType params[ENTRY_PARAMS_MAX];
} RunCall;

/* A run: the script path, the prelude families, the calls so far, the
   current state and a print buffer of PRINT_MAX bytes. */
typedef struct {
  const char *path;
  uint32_t env;
  uint32_t out;
  uint32_t calls;
  const Value *state;
  char *buf;
} Run;

static uint32_t find_family(const Machine *m, const char *name) {
  uint32_t i;
  for (i = 0; i < m->family_count; i++) {
    if (strcmp(m->families[i].name, name) == 0)
      return i;
  }
  return NO_FAMILY;
}

static int is_family(const Value *t, uint32_t family) {
  return t != NULL && val_is(t, OP_FAMILY) && t->inst == family;
}

static int arg_type(const Value *t, ArgType *out) {
  *out = val_is(t, OP_FLAG) ? ARG_TYPE_FLAG : val_is(t, OP_U256) ? ARG_TYPE_U256 : val_is(t, OP_ADDR) ? ARG_TYPE_ADDR : ARG_TYPE_NAT;
  return t != NULL && (val_is(t, OP_NAT) || val_is(t, OP_FLAG) || val_is(t, OP_U256) || val_is(t, OP_ADDR));
}

/* Entry: (env : Env) -> (s : State) -> ARGS -> Option (Prod State (List Out)).
   View: the same parameters, with a Nat, Flag, U256 or Addr result. Each
   argument is a Nat, Flag, U256 or Addr. All other definitions are helpers. */
static Role role_of(Machine *m, const Run *r, const Value *type, RunCall *call) {
  const Value *t = type;
  const Value *p;
  ArgType kind;
  memset(call, 0, sizeof *call);
  if (t == NULL || t->kind != VAL_PI || !is_family(t->dom, r->env))
    return ROLE_HELPER;
  t = closure_apply(m, t, val_var(m, 0));
  if (t == NULL || t->kind != VAL_PI || !is_family(t->dom, m->state_family))
    return ROLE_HELPER;
  t = closure_apply(m, t, val_var(m, 1));
  while (t != NULL && t->kind == VAL_PI && call->param_count < ENTRY_PARAMS_MAX && arg_type(t->dom, &kind)) {
    call->params[call->param_count] = kind;
    t = closure_apply(m, t, val_var(m, call->param_count + 2u));
    call->param_count++;
  }
  if (arg_type(t, &kind))
    return ROLE_VIEW;
  p = val_is(t, OP_OPTION) ? t->args[0] : NULL;
  if (val_is(p, OP_PROD) && is_family(p->args[0], m->state_family) && val_is(p->args[1], OP_LIST) && is_family(p->args[1]->args[0], r->out))
    return ROLE_ENTRY;
  return ROLE_HELPER;
}

static int has_trap(const Value *v) {
  uint32_t i;
  if (v != NULL && v->kind == VAL_TRAP)
    return 1;
  for (i = 0; v != NULL && v->kind == VAL_OP && i < v->argc; i++) {
    if (has_trap(v->args[i]))
      return 1;
  }
  return 0;
}

static int print_value(Machine *m, const Value *v, char *buf) {
  return value_print(m, NULL, 0, v, buf, PRINT_MAX) ? 1 : diag_fail(m->diag, "EVAL_PRINT", m->def, "the normal form exceeds the printer limits");
}

static const Value *make_env(Machine *m, const Run *r, uint64_t now, const U256 *caller) {
  const Value **args = arena_alloc(m->arena, 2u * sizeof *args);
  if (args == NULL) {
    diag_fail(m->diag, "OOM", m->def, "out of memory");
    return NULL;
  }
  args[0] = val_nat(m, now);
  args[1] = val_word(m, WORD_ADDR, caller);
  return args[0] == NULL || args[1] == NULL ? NULL : val_op(m, VAL_OP, OP_CTOR, m->families[r->env].first_ctor, 0, args, 2u);
}

/* One call `NOW CALLER NAME ARGS...` (D-6 to D-8). A trap is a revert: the
   state does not change. Returns 0, 1, or 2 after EVAL_ARGS. */
static int run_call(Machine *m, Run *r, uint32_t line, char *const *word, uint32_t count, FILE *out) {
  const Value *args[ENTRY_PARAMS_MAX];
  U256 *caller = arena_alloc(m->arena, sizeof *caller);
  const char *name = count > 2u ? word[2] : "";
  uint64_t now = 0;
  uint32_t d = count > 2u ? find_def(m, name) : NO_DEF;
  RunCall call;
  Role role;
  const Value *v;
  uint32_t i;
  if (caller == NULL)
    return diag_fail(m->diag, "OOM", NULL, "out of memory") + 1;
  if (count < 3u)
    return diag_fail(m->diag, "RUN_SCRIPT", NULL, "%s:%u: a call is NOW CALLER NAME ARGS...", r->path, line) + 1;
  if (!parse_u64(word[0], &now))
    return diag_fail(m->diag, "RUN_SCRIPT", NULL, "%s:%u: NOW '%s' is not a Nat", r->path, line, word[0]) + 1;
  if (!parse_word(word[1], WORD_ADDR, caller))
    return diag_fail(m->diag, "RUN_SCRIPT", NULL, "%s:%u: CALLER '%s' is not an Addr (0x and 40 hex digits)", r->path, line, word[1]) + 1;
  m->fuel = EVAL_FUEL_STEPS;
  m->def = name;
  if (d == NO_DEF)
    return diag_fail(m->diag, "EVAL_ENTRY", name, "%s:%u: no definition has this name", r->path, line) + 1;
  role = role_of(m, r, m->defs[d].type, &call);
  if (role == ROLE_HELPER)
    return diag_fail(m->diag, "EVAL_ENTRY", name, "%s:%u: a helper is not an entry or a view", r->path, line) + 1;
  if (count - 3u != call.param_count)
    return diag_fail(m->diag, "EVAL_ARGS", name, "%s:%u: the call takes %u arguments, found %u", r->path, line, call.param_count, count - 3u) + 2;
  for (i = 0; i < call.param_count; i++) {
    args[i] = entry_arg(m, word[i + 3u], call.params[i]);
    if (args[i] == NULL)
      return strcmp(m->diag->code != NULL ? m->diag->code : "", "EVAL_ARGS") == 0 ? 2 : 1;
  }
  v = apply_value(m, apply_value(m, def_value(m, d), make_env(m, r, now, caller)), r->state);
  for (i = 0; i < call.param_count; i++)
    v = apply_value(m, v, args[i]);
  if (v == NULL)
    return 1;
  r->calls++;
  if (has_trap(v)) {
    fprintf(out, "%u %s trap\n", r->calls, name);
    return 0;
  }
  if (role == ROLE_VIEW) {
    if (!print_value(m, v, r->buf))
      return 1;
    fprintf(out, "%u %s = %s\n", r->calls, name, r->buf);
    return 0;
  }
  if (val_is(v, OP_NONE)) {
    fprintf(out, "%u %s revert\n", r->calls, name);
    return 0;
  }
  if (!val_is(v, OP_SOME) || !val_is(v->args[0], OP_PAIR))
    return diag_fail(m->diag, "EVAL_ENTRY", name, "%s:%u: the entry did not compute some or none", r->path, line) + 1;
  r->state = v->args[0]->args[0];
  fprintf(out, "%u %s ok\n", r->calls, name);
  for (v = v->args[0]->args[1]; val_is(v, OP_CONS); v = v->args[1]) {
    if (!print_value(m, v->args[0], r->buf))
      return 1;
    fprintf(out, "  %s\n", r->buf);
  }
  return 0;
}

/* Splits LINE at spaces and tabs. Returns the word count; keeps CAP words. */
static uint32_t split_words(char *line, char **word, uint32_t cap) {
  uint32_t count = 0;
  char *p = line;
  while (*p != '\0') {
    if (*p == ' ' || *p == '\t' || *p == '\r') {
      *p++ = '\0';
      continue;
    }
    if (count < cap)
      word[count] = p;
    count++;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r')
      p++;
  }
  return count;
}

int run_command(Machine *m, const char *path, const char *text, size_t len, FILE *out) {
  char *word[ENTRY_PARAMS_MAX + 3u];
  uint32_t init = find_def(m, "init");
  uint32_t line = 0;
  size_t pos = 0;
  int status = 0;
  Run r;
  memset(&r, 0, sizeof r);
  r.path = path;
  r.env = find_family(m, "Env");
  r.out = find_family(m, "Out");
  r.buf = arena_alloc(m->arena, PRINT_MAX);
  m->def = "init";
  if (!m->has_state || init == NO_DEF || !is_family(m->defs[init].type, m->state_family) || r.env == NO_FAMILY)
    return diag_fail(m->diag, "RUN_INIT", NULL, "a run needs a state and def init : State") + 1;
  if (r.buf == NULL)
    return diag_fail(m->diag, "OOM", NULL, "out of memory") + 1;
  m->fuel = EVAL_FUEL_STEPS;
  r.state = def_value(m, init);
  if (r.state == NULL)
    return 1;
  if (has_trap(r.state))
    return diag_fail(m->diag, "RUN_INIT", "init", "the start state traps") + 1;
  while (status == 0 && pos < len) {
    size_t end = pos;
    char *copy;
    uint32_t count;
    while (end < len && text[end] != '\n')
      end++;
    if (memchr(text + pos, '\0', end - pos) != NULL)
      return diag_fail(m->diag, "RUN_SCRIPT", NULL, "%s:%u: a script line contains a NUL byte", r.path, line + 1u) + 1;
    copy = arena_alloc(m->arena, end - pos + 1u);
    if (copy == NULL)
      return diag_fail(m->diag, "OOM", NULL, "out of memory") + 1;
    memcpy(copy, text + pos, end - pos);
    copy[end - pos] = '\0';
    line++;
    count = split_words(copy, word, ENTRY_PARAMS_MAX + 3u);
    if (count != 0u && strncmp(word[0], "--", 2) != 0)
      status = run_call(m, &r, line, word, count, out);
    pos = end + 1u;
  }
  if (status != 0)
    return status;
  m->def = NULL;
  if (!print_value(m, r.state, r.buf))
    return 1;
  fprintf(out, "state %s\n", r.buf);
  return 0;
}
