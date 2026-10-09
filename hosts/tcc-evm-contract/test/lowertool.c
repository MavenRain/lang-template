/* Residual lowering regressions exercise checked source and the resulting IR. */
#include <stdio.h>
#include <string.h>

#include "front/check.h"
#include "front/front.h"
#include "../src/lower.c"

static unsigned statements(IrBlock b, IrStmtKind kind) {
  unsigned n = 0;
  for (size_t i = 0; i < b.count; i++) {
    const IrStmt *s = b.items[i];
    n += s->kind == kind;
    n += statements(s->body, kind) + statements(s->otherwise, kind);
  }
  return n;
}

static unsigned operations(const IrExpr *e, IrOp op) {
  if (e == NULL) return 0;
  return (e->kind == IR_EXPR_BINARY && e->op == op)
    + operations(e->left, op) + operations(e->right, op);
}

static unsigned block_operations(IrBlock b, IrOp op) {
  unsigned n = 0;
  for (size_t i = 0; i < b.count; i++) {
    const IrStmt *s = b.items[i];
    n += operations(s->expr, op) + operations(s->value, op);
    n += block_operations(s->body, op) + block_operations(s->otherwise, op);
  }
  return n;
}

static int regression(const char *name, const char *defs, size_t functions,
                      size_t params, unsigned traps, unsigned divisions,
                      unsigned branches, int empty_fuel) {
  const char *prefix = "state State := makeState (counter : Nat)\n"
    "def init : State := makeState 0\n";
  char source[4096];
  Arena arena;
  Diag diag;
  DeclList decls;
  Machine machine;
  IrProgram program = {NULL, 0};
  int ok;
  snprintf(source, sizeof source, "%s%s", prefix, defs);
  arena_init(&arena, (size_t)1 << 25);
  diag_init(&diag);
  ok = front_load(&arena, name, source, strlen(source), &decls, &diag)
    && check_program(&arena, &decls, &machine, &diag);
  if (ok) {
    if (empty_fuel) machine.fuel = 0;
    const CtorInfo *ci = &machine.ctors[machine.families[machine.state_family].first_ctor];
    ok = lower_entries(&machine, ci, &program);
  }
  ok = ok && diag.code == NULL && program.func_count == functions;
  if (ok && functions > 0) {
    const IrFunc *f = &program.funcs[0];
    ok = f->param_count == params
      && statements(f->body, IR_STMT_TRAP) == traps
      && block_operations(f->body, IR_OP_DIV) == divisions
      && statements(f->body, IR_STMT_IF) == branches;
    if (ok && traps > 0 && branches == 0) {
      size_t trap_at = f->body.count;
      size_t store_at = f->body.count;
      for (size_t i = 0; i < f->body.count; i++) {
        if (f->body.items[i]->kind == IR_STMT_TRAP && trap_at == f->body.count) trap_at = i;
        if (f->body.items[i]->kind == IR_STMT_SSTORE && store_at == f->body.count) store_at = i;
      }
      ok = trap_at < store_at;
    }
    if (ok && traps > 0 && branches == 1) {
      const IrStmt *branch = NULL;
      for (size_t i = 0; i < f->body.count; i++)
        if (f->body.items[i]->kind == IR_STMT_IF) branch = f->body.items[i];
      ok = branch != NULL && statements(branch->body, IR_STMT_TRAP) == 0
        && statements(branch->otherwise, IR_STMT_TRAP) == traps;
    }
  }
  if (!ok) {
    fprintf(stderr, "FAIL lower %s (functions %zu)\n", name, program.func_count);
    if (diag.set) diag_print(&diag, stderr);
  }
  arena_release(&arena);
  return ok;
}

#define ENTRY "def entry : Env -> State -> Option (Prod State (List Out)) := fun env s => "
#define TOKEN "0x00000000000000000000000000000000000000aa"

int main(void) {
  int ok = 1;
  ok &= regression("helper-pair",
    "def helper : Env -> State -> Option (Prod Nat Nat) := fun env s => some (pair 1 2)\n",
    0, 0, 0, 0, 0, 0);
  ok &= regression("helper-none",
    "def helper : Env -> State -> Option (Prod State (List Nat)) := fun env s => none\n",
    0, 0, 0, 0, 0, 0);
  ok &= regression("helper-output",
    "def helper : Env -> State -> Option (Prod State (List Nat)) := fun env s => some (pair s nil)\n",
    0, 0, 0, 0, 0, 0);
  ok &= regression("sixteen-arguments",
    "def entry : Env -> State -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Nat -> Option (Prod State (List Out)) :=\n"
    "fun env s a b c d e f g h i j k l m n o p => some (pair (makeState p) nil)\n",
    1, 16, 0, 0, 0, 0);
  ok &= regression("fresh-fuel", ENTRY "some (pair s nil)\n", 1, 0, 0, 0, 0, 1);
  ok &= regression("pay-ok", ENTRY
    "some (pair (makeState 9) (cons (pay " TOKEN " " TOKEN " 7u) nil))\n",
    1, 0, 0, 0, 0, 0);
  ok &= regression("pay-trap", ENTRY
    "some (pair (makeState 9) (cons (pay " TOKEN " " TOKEN " (u256Div 7u 0u)) nil))\n",
    1, 0, 1, 0, 0, 0);
  ok &= regression("emit-fields-trap", ENTRY
    "some (pair (makeState 9) (cons (emit 1 (cons (u256Div 7u 0u) nil)) nil))\n",
    1, 0, 1, 0, 0, 0);
  ok &= regression("pay-symbolic-div",
    "def entry : Env -> State -> U256 -> Option (Prod State (List Out)) := fun env s x => "
    "some (pair (makeState 9) (cons (pay " TOKEN " " TOKEN " (u256Div 7u x)) nil))\n",
    1, 1, 0, 1, 0, 0);
  ok &= regression("lazy-output-field",
    "def entry : Env -> State -> Flag -> Option (Prod State (List Out)) := fun env s f => "
    "some (pair (makeState 9) (cons (pay " TOKEN " " TOKEN " (flagIf f 7u (u256Div 7u 0u))) nil))\n",
    1, 1, 1, 0, 1, 0);
  ok &= regression("lazy-output-list",
    "def entry : Env -> State -> Flag -> Option (Prod State (List Out)) := fun env s f => "
    "some (pair (makeState 9) (flagIf f nil (cons (pay " TOKEN " " TOKEN " (u256Div 7u 0u)) nil)))\n",
    1, 1, 1, 0, 1, 0);
  if (ok) puts("lower regressions: passed");
  return ok ? 0 : 1;
}
