/* Residual lowering regressions exercise checked source and the resulting IR. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

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
    ok = lower_entries(&machine, ci, &program, NULL);
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
    if (ok && traps > 0 && branches > 0) {
      /* The first IF holds the words. A flagIf in the Out list with effects
         gives a second IF after the state write (C-K4d-2). */
      const IrStmt *branch = NULL;
      for (size_t i = 0; i < f->body.count; i++)
        if (f->body.items[i]->kind == IR_STMT_IF && branch == NULL) branch = f->body.items[i];
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

/* C-K4d-2: each statement of the entry body that holds a LOG comes after the
   last SSTORE (if any), and the body ends with STOP. With LOCAL_COND, that
   statement is an IF on a local (the stores can change a storage word in the
   condition), and its LOG is in the else branch (the flagIf no branch). */
static int order_regression(const char *name, const char *defs, int local_cond) {
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
    const CtorInfo *ci = &machine.ctors[machine.families[machine.state_family].first_ctor];
    ok = lower_entries(&machine, ci, &program, NULL);
  }
  ok = ok && diag.code == NULL && program.func_count == 1;
  if (ok) {
    IrBlock body = program.funcs[0].body;
    size_t last_store = 0; /* 1 + the position of the last SSTORE */
    size_t first_log = 0;  /* 1 + the position of the first statement with a LOG */
    const IrStmt *holder = NULL;
    for (size_t i = 0; i < body.count; i++) {
      const IrStmt *s = body.items[i];
      unsigned logs = (s->kind == IR_STMT_LOG) + statements(s->body, IR_STMT_LOG)
        + statements(s->otherwise, IR_STMT_LOG);
      if (s->kind == IR_STMT_SSTORE) last_store = i + 1;
      if (logs > 0 && holder == NULL) {
        first_log = i + 1;
        holder = s;
      }
    }
    ok = holder != NULL && first_log > last_store
      && body.items[body.count - 1]->kind == IR_STMT_STOP
      && (!local_cond || (holder->kind == IR_STMT_IF && holder->expr->kind == IR_EXPR_LOCAL
                          && statements(holder->body, IR_STMT_LOG) == 0));
  }
  if (!ok) {
    fprintf(stderr, "FAIL lower %s (functions %zu)\n", name, program.func_count);
    if (diag.set) diag_print(&diag, stderr);
  }
  arena_release(&arena);
  return ok;
}

static int abi_mode_regression(const char *name, const char *source, const char *code, const char *expected, int json) {
  Arena arena;
  Diag diag;
  DeclList decls;
  Machine machine;
  FILE *out = tmpfile();
  if (out == NULL) return 0;
  arena_init(&arena, (size_t)1 << 25);
  diag_init(&diag);
  int ok = front_load(&arena, name, source, strlen(source), &decls, &diag)
    && check_program(&arena, &decls, &machine, &diag);
  if (ok) {
    int status = lower_abi(&machine, out, json);
    if (code != NULL) {
      ok = status == 1 && diag.code != NULL && strcmp(diag.code, code) == 0 && ftell(out) == 0;
    } else {
      char text[1024];
      rewind(out);
      size_t count = fread(text, 1, sizeof text - 1, out);
      text[count] = '\0';
      ok = status == 0 && !diag.set && !ferror(out) && feof(out) && strcmp(text, expected) == 0;
    }
  }
  if (!ok) {
    fprintf(stderr, "FAIL abi %s\n", name);
    if (diag.set) diag_print(&diag, stderr);
  }
  fclose(out);
  arena_release(&arena);
  return ok;
}

static int abi_regression(const char *name, const char *source, const char *code, const char *expected) {
  return abi_mode_regression(name, source, code, expected, 0);
}

#define ABI_STATE "state State := makeState (counter : Nat)\ndef init : State := makeState 0\n"
#define ABI_ENTRY_TYPE " : Env -> State -> Option (Prod State (List Out)) := fun env s => some (pair s nil)\n"

static int abi_size_regression(const char *name, const char *source, const char *code, long bytes) {
  Arena arena;
  Diag diag;
  DeclList decls;
  Machine machine;
  FILE *out = tmpfile();
  if (out == NULL) return 0;
  arena_init(&arena, (size_t)1 << 25);
  diag_init(&diag);
  int ok = front_load(&arena, name, source, strlen(source), &decls, &diag)
    && check_program(&arena, &decls, &machine, &diag);
  if (ok) {
    int result = lower_abi(&machine, out, 0);
    ok = (code == NULL ? result == 0 && diag.code == NULL
                      : result == 1 && diag.code != NULL && strcmp(diag.code, code) == 0)
      && ftell(out) == bytes;
  }
  if (!ok) {
    fprintf(stderr, "FAIL abi %s (bytes %ld)\n", name, ftell(out));
    if (diag.set) diag_print(&diag, stderr);
  }
  fclose(out);
  arena_release(&arena);
  return ok;
}

static int abi_boundaries(void) {
  char name[255], source[1024];
  memset(name, 'a', sizeof name);
  name[253] = '\0';
  snprintf(source, sizeof source, ABI_STATE "def %s" ABI_ENTRY_TYPE, name);
  int ok = abi_size_regression("signature-255", source, NULL, 273);
  name[253] = 'a'; name[254] = '\0';
  snprintf(source, sizeof source, ABI_STATE "def good" ABI_ENTRY_TYPE "def %s" ABI_ENTRY_TYPE, name);
  ok &= abi_size_regression("signature-256", source, "EVM_SIGNATURE", 0);
  snprintf(source, sizeof source, ABI_STATE "event %s\n", name);
  ok &= abi_size_regression("event-signature-256", source, NULL, 330);
  return ok;
}

#define ENTRY "def entry : Env -> State -> Option (Prod State (List Out)) := fun env s => "
#define LOGGED "event Logged (x : U256)\n"

/* Compare a closed reference fold and the build boundary for its open view. */
static int fold_regression(const char *name, const char *step, const char *seed, int expected, int refused) {
  char source[4096];
  snprintf(source, sizeof source,
    "state State := makeState (items : List U256)\n"
    "def init : State := makeState (cons 7u (cons 0u nil))\n"
    "def d : U256 := 0u\n"
    "def value : Env -> State -> U256 -> U256 := fun env s d => fold (%s) (%s) (items s)\n"
    "def answer : U256 := fold (%s) (%s) (items init)\n", step, seed, step, seed);
  Arena arena;
  Diag diag;
  DeclList decls;
  Machine machine;
  FILE *out = tmpfile();
  if (out == NULL) return 0;
  arena_init(&arena, (size_t)1 << 25);
  diag_init(&diag);
  int ok = front_load(&arena, name, source, strlen(source), &decls, &diag)
    && check_program(&arena, &decls, &machine, &diag);
  if (ok) {
    const Value *answer = NULL;
    machine.fuel = EVAL_FUEL_STEPS;
    for (uint32_t i = 0; i < machine.def_count; i++)
      if (strcmp(machine.defs[i].name, "answer") == 0) answer = def_value(&machine, i);
    Word got, want;
    word_u64(want, (uint64_t)expected);
    ok = expected < 0 ? answer != NULL && answer->kind == VAL_TRAP
      : word_of(answer, got) && memcmp(got, want, sizeof got) == 0;
    if (ok) {
      int status = lower_build(&machine, TARGET_PART_MAIN, out);
      ok = refused ? status == 1 && diag.code != NULL && strcmp(diag.code, "REFUSE_LOWER") == 0 && ftell(out) == 0
        : status == 0 && !diag.set && ftell(out) > 0;
    }
  }
  if (!ok) {
    fprintf(stderr, "FAIL fold %s\n", name);
    if (diag.set) diag_print(&diag, stderr);
  }
  fclose(out);
  arena_release(&arena);
  return ok;
}

int main(void) {
  int ok = 1;
  const char *head = "fun (x : U256) (acc : U256) => x";
  const char *sum = "fun (x : U256) (acc : U256) => u256Add x acc";
  ok &= fold_regression("discard-static-seed-trap", head, "u256Div 1u 0u", 7, 1);
  ok &= fold_regression("discard-dynamic-seed-trap", head, "u256Div 1u d", 7, 1);
  ok &= fold_regression("discard-conditional-seed-trap", head,
    "flagIf (u256Eq d 0u) (u256Div 1u 0u) 0u", 7, 1);
  ok &= fold_regression("discard-intermediate-trap",
    "fun (x : U256) (acc : U256) => flagIf (u256Eq x 7u) x (u256Div 1u 0u)", "0u", 7, 1);
  ok &= fold_regression("discard-dynamic-step-trap",
    "fun (x : U256) (acc : U256) => flagIf (u256Eq x 7u) x (u256Div 1u x)", "0u", 7, 1);
  ok &= fold_regression("discard-trap-in-one-branch",
    "fun (x : U256) (acc : U256) => flagIf (u256Eq x 7u) x (u256Add acc 1u)", "u256Div 1u 0u", 7, 1);
  ok &= fold_regression("safe-head", head, "0u", 7, 0);
  ok &= fold_regression("strict-sum", sum, "0u", 7, 0);
  ok &= fold_regression("strict-trap-seed", sum, "u256Div 1u 0u", -1, 0);
  ok &= fold_regression("strict-conditional",
    "fun (x : U256) (acc : U256) => flagIf (u256Eq x 7u) (u256Add x acc) (u256Mul x acc)", "0u", 7, 0);
  ok &= abi_regression("list-cons-other-field",
    "state State := makeState (items : List U256) (other : List U256)\n"
    "def init : State := makeState nil nil\n"
    ENTRY "some (pair (makeState (cons 1u (other s)) (other s)) nil)\n", "REFUSE_LOWER", NULL);
  ok &= abi_regression("init-trap",
    "state State := makeState (n : U256)\n"
    "def init : State := makeState (u256Div 1u 0u)\n"
    "def value : Env -> State -> U256 := fun env s => n s\n", "REFUSE_LOWER", NULL);
  ok &= abi_regression("selector-collision",
    "state State := makeState (n : Nat)\n"
    "def init : State := makeState 0\n"
    "def collision39027 : Env -> State -> Nat := fun env s => n s\n"
    "def collision109357 : Env -> State -> Option (Prod State (List Out)) := "
    "fun env s => some (pair s nil)\n", "EVM_SELECTOR", NULL);
  ok &= abi_regression("no-functions",
    "event Opened\nstate State := makeState (n : Nat)\n"
    "def init : State := makeState 0\n", NULL,
    "0xd1dcd00534373f20882b79e6ab6875a5c358c5bd576448757ed50e63069ab518 Opened() event\n");
  ok &= abi_size_regression("entry-collision", ABI_STATE
    "def entry37557" ABI_ENTRY_TYPE "def entry9660" ABI_ENTRY_TYPE,
    "EVM_SELECTOR", 0);
  ok &= abi_size_regression("reverse-entry-collision", ABI_STATE
    "def entry9660" ABI_ENTRY_TYPE "def entry37557" ABI_ENTRY_TYPE,
    "EVM_SELECTOR", 0);
  ok &= abi_size_regression("entry-view-collision", ABI_STATE
    "def entry37557" ABI_ENTRY_TYPE "def entry9660 : Env -> State -> Nat := fun env s => 0\n",
    "EVM_SELECTOR", 0);
  ok &= abi_mode_regression("json",
    "event Paid (amount : U256)\nstate State := makeState (n : Nat)\n"
    "def init : State := makeState 0\n"
    "def readout : Env -> State -> Nat := fun env s => n s\n"
    ENTRY "some (pair s nil)\n", NULL,
    "[\n"
    "{\"type\":\"function\",\"name\":\"readout\",\"inputs\":[],\"outputs\":[{\"name\":\"\",\"type\":\"uint64\"}],\"stateMutability\":\"view\"},\n"
    "{\"type\":\"function\",\"name\":\"entry\",\"inputs\":[],\"outputs\":[],\"stateMutability\":\"nonpayable\"},\n"
    "{\"type\":\"event\",\"name\":\"Paid\",\"inputs\":[{\"name\":\"amount\",\"type\":\"uint256\",\"indexed\":false}],\"anonymous\":false}\n"
    "]\n", 1);
  ok &= abi_boundaries();
  int output_status = system("build/langc abi test/contract-nocall.lang 1</dev/null 2>/dev/null");
  if (output_status == -1 || !WIFEXITED(output_status) || WEXITSTATUS(output_status) != 2) {
    fprintf(stderr, "FAIL abi buffered-write (status %d)\n", output_status);
    ok = 0;
  }
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
  ok &= regression("out-ok", LOGGED ENTRY
    "some (pair (makeState 9) (cons (Logged 7u) nil))\n",
    1, 0, 0, 0, 0, 0);
  ok &= regression("out-trap-second", LOGGED ENTRY
    "some (pair (makeState 9) (cons (Logged 7u) (cons (Logged (u256Div 7u 0u)) nil)))\n",
    1, 0, 1, 0, 0, 0);
  ok &= regression("event-fields-trap", LOGGED ENTRY
    "some (pair (makeState 9) (cons (Logged (u256Div 7u 0u)) nil))\n",
    1, 0, 1, 0, 0, 0);
  ok &= regression("out-symbolic-div", LOGGED
    "def entry : Env -> State -> U256 -> Option (Prod State (List Out)) := fun env s x => "
    "some (pair (makeState 9) (cons (Logged (u256Div 7u x)) nil))\n",
    1, 1, 0, 1, 0, 0);
  ok &= regression("lazy-output-field", LOGGED
    "def entry : Env -> State -> Flag -> Option (Prod State (List Out)) := fun env s f => "
    "some (pair (makeState 9) (cons (Logged (flagIf f 7u (u256Div 7u 0u))) nil))\n",
    1, 1, 1, 0, 1, 0);
  ok &= regression("lazy-output-list", LOGGED
    "def entry : Env -> State -> Flag -> Option (Prod State (List Out)) := fun env s f => "
    "some (pair (makeState 9) (flagIf f nil (cons (Logged (u256Div 7u 0u)) nil)))\n",
    1, 1, 1, 0, 2, 0);
  ok &= order_regression("effect-order", LOGGED ENTRY
    "some (pair (makeState 9) (cons (Logged 7u) nil))\n", 0);
  ok &= order_regression("effect-order-same-state", LOGGED ENTRY
    "some (pair s (cons (Logged 7u) nil))\n", 0);
  ok &= order_regression("effect-order-stored-flag", LOGGED ENTRY
    "some (pair (makeState 9) (flagIf (natEq (counter s) 0) nil (cons (Logged 7u) nil)))\n", 1);
  if (ok) puts("lower regressions: passed");
  return ok ? 0 : 1;
}
