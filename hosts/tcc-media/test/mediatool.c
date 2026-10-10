/* Regression checks for the media primitives, using the public front end. */
#include <stdio.h>
#include <string.h>

#include "front/check.h"
#include "front/front.h"
#include "json.h"

static unsigned checked;
static unsigned failed;

static void expect(const char *name, int ok, const Diag *diag) {
  checked++;
  if (!ok) {
    fprintf(stderr, "FAIL media %s\n", name);
    if (diag->set) diag_print(diag, stderr);
    failed++;
  }
}

static int load(Arena *arena, Diag *diag, Machine *m, const char *source) {
  DeclList decls;
  return front_load(arena, "media-test.lang", source, strlen(source), &decls, diag)
    && check_program(arena, &decls, m, diag);
}

static void json_case(const char *name, const char *source, const char *want, const char *code) {
  Arena arena;
  Diag diag;
  Machine m;
  const char *text = NULL;
  size_t len = 0;
  arena_init(&arena, (size_t)64 << 20);
  diag_init(&diag);
  int loaded = load(&arena, &diag, &m, source);
  int ok = loaded && json_document(&m, &text, &len);
  expect(name, code != NULL ? !ok && diag.code != NULL && strcmp(diag.code, code) == 0 && text == NULL
      && (strcmp(code, "INTERVAL_RANGE") != 0 || !loaded)
    : ok && len == strlen(text) && strstr(text, want) != NULL && !diag.set, &diag);
  arena_release(&arena);
}

static void dynamic_case(uint64_t lo, uint64_t hi, int valid) {
  Arena arena;
  Diag diag;
  Machine m;
  const Value *v = NULL;
  arena_init(&arena, (size_t)64 << 20);
  diag_init(&diag);
  int ok = load(&arena, &diag, &m,
    "def mk : (lo : Nat) -> (hi : Nat) -> Interval := fun (lo : Nat) (hi : Nat) => interval lo hi\n");
  if (ok) {
    uint32_t i;
    for (i = 0; i < m.def_count; i++) {
      if (strcmp(m.defs[i].name, "mk") == 0) {
        m.def = "mk";
        v = apply_value(&m, apply_value(&m, def_value(&m, i), val_nat(&m, lo)), val_nat(&m, hi));
        break;
      }
    }
  }
  expect(valid ? "dynamic valid bounds" : "dynamic reversed bounds", ok && (valid
    ? v != NULL && val_is(v, OP_MK_INTERVAL) && v->args[0]->nat == lo && v->args[1]->nat == hi && !diag.set
    : v == NULL && diag.code != NULL && strcmp(diag.code, "INTERVAL_RANGE") == 0), &diag);
  arena_release(&arena);
}

int main(void) {
  json_case("interval", "def result : Interval := interval 1 3\n",
    "\"value\":{\"lo\":1,\"hi\":3}", NULL);
  json_case("singleton", "def result : Interval := interval 4 4\n",
    "\"value\":{\"lo\":4,\"hi\":4}", NULL);
  json_case("max endpoint", "def result : Interval := interval 0 18446744073709551615\n",
    "\"value\":{\"lo\":0,\"hi\":18446744073709551615}", NULL);
  json_case("overlap", "def result : Option Interval := intersect (interval 1 3) (interval 2 4)\n",
    "\"value\":{\"lo\":2,\"hi\":3}", NULL);
  json_case("closed endpoints", "def result : Option Interval := intersect (interval 1 2) (interval 2 4)\n",
    "\"value\":{\"lo\":2,\"hi\":2}", NULL);
  json_case("disjoint", "def result : Option Interval := intersect (interval 1 2) (interval 3 4)\n",
    "\"value\":null", NULL);
  json_case("nested intervals", "def result : List Interval := cons (interval 1 3) nil\n",
    "\"value\":[{\"lo\":1,\"hi\":3}]", NULL);
  json_case("nested options", "def result : Option (Option Interval) := some (some (interval 1 3))\n",
    "\"value\":{\"some\":{\"lo\":1,\"hi\":3}}", NULL);
  json_case("literal reversed bounds", "def result : Interval := interval 3 1\n", NULL, "INTERVAL_RANGE");
  json_case("computed reversed bounds", "def result : Interval := interval (natAdd 1 2) 1\n", NULL, "INTERVAL_RANGE");
  json_case("endpoint overflow", "def result : Interval := interval 0 (natAdd 18446744073709551615 1)\n",
    NULL, "EVAL_OVERFLOW");
  dynamic_case(1, 3, 1);
  dynamic_case(3, 1, 0);
  printf("media regressions: %u checked, %u failed\n", checked, failed);
  return failed == 0 ? 0 : 1;
}
