/* Run budgets are independent of the fuel left by checking or prior calls. */
#include <stdio.h>
#include <string.h>

#include "front/check.h"
#include "front/front.h"

static int run_fuel(int cached_init) {
  const char *program = "state State := makeState (counter : Nat)\n"
    "def init : State := makeState 0\n"
    "def readCounter : Env -> State -> Nat := fun env s => counter s\n";
  const char *script = "1 0x00000000000000000000000000000000000000aa readCounter\n";
  const char *want = "1 readCounter = 0\nstate makeState 0\n";
  Arena arena;
  Diag diag;
  DeclList decls;
  Machine machine;
  char got[128] = {0};
  FILE *out = tmpfile();
  int ok;
  uint32_t i;
  if (out == NULL) return 0;
  arena_init(&arena, (size_t)1 << 25);
  diag_init(&diag);
  ok = front_load(&arena, "run-fuel", program, strlen(program), &decls, &diag)
    && check_program(&arena, &decls, &machine, &diag);
  if (ok && cached_init) {
    for (i = 0; i < machine.def_count; i++) {
      if (strcmp(machine.defs[i].name, "init") == 0) {
        ok = def_value(&machine, i) != NULL;
        break;
      }
    }
  }
  if (ok) {
    machine.fuel = 1;
    ok = run_command(&machine, "fuel.script", script, strlen(script), out) == 0;
  }
  rewind(out);
  size_t count = fread(got, 1, sizeof got - 1u, out);
  ok = ok && !ferror(out) && count == strlen(want) && strcmp(got, want) == 0;
  if (!ok) {
    fprintf(stderr, "FAIL run fuel (%s init)\n", cached_init ? "cached" : "uncached");
    diag_print(&diag, stderr);
  }
  fclose(out);
  arena_release(&arena);
  return ok;
}

int main(void) {
  int ok = run_fuel(0);
  ok &= run_fuel(1);
  if (ok) puts("run fuel regressions: passed");
  return ok ? 0 : 1;
}
