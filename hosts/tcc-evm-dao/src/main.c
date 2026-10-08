/* langc, the {{LANG}} compiler (README.md):
 *   langc check PROG                    ok debreu | ok impossibility
 *   langc table PROG                    REGIME MEMBERS [CODES...]
 *   langc verdicts PROG NAME            one decision code per ballot vector
 *   langc eval PROG NAME                the normal form of NAME
 *   langc build PROG [--runtime] -o OUT the contract
 * Exit 0 ok, 1 refused, 2 usage or IO; errors go to stderr as
 * "langc: CODE: DEF: message". Each verb parses the embedded prelude and
 * PROG and checks them (src/check.h) first. */
#include "check.h"
#include "prelude.h"
#include <errno.h>
#include <string.h>

typedef enum { VERB_CHECK, VERB_TABLE, VERB_VERDICTS, VERB_EVAL, VERB_BUILD } VerbKind;

typedef struct {
  const char *name;
  VerbKind kind;
  int argc;  /* argc with the verb, PROG and NAME; build adds -o OUT */
} Verb;

static const Verb VERBS[] = {
  {"check", VERB_CHECK, 3}, {"table", VERB_TABLE, 3}, {"verdicts", VERB_VERDICTS, 4},
  {"eval", VERB_EVAL, 4}, {"build", VERB_BUILD, 5}
};

static int usage(void) {
  fputs("langc: USAGE: -: langc check|table PROG, langc verdicts|eval PROG NAME,"
        " langc build PROG [--runtime] -o OUT\n", stderr);
  return LANG_EXIT_USAGE;
}

static const Verb *find_verb(const char *name) {
  for (size_t i = 0; i < sizeof VERBS / sizeof VERBS[0]; i++)
    if (strcmp(VERBS[i].name, name) == 0)
      return &VERBS[i];
  return NULL;
}

/* build PROG -o OUT or build PROG --runtime -o OUT */
static int build_fits(int argc, char **argv) {
  int runtime = argc == 6 && strcmp(argv[3], "--runtime") == 0;
  return (argc == 5 || runtime) && strcmp(argv[runtime ? 4 : 3], "-o") == 0;
}

static int arguments_fit(const Verb *verb, int argc, char **argv) {
  if (verb->kind == VERB_BUILD)
    return build_fits(argc, argv);
  return argc == verb->argc;
}

static const char *regime_name(const LangChecked *checked) {
  return lang_regime(checked) == LANG_REGIME_DEBREU ? "debreu" : "impossibility";
}

static int verb_table(LangChecked *checked) {
  const unsigned char *codes = NULL;
  size_t count = 0;
  int status = lang_table(checked, &codes, &count);
  if (status != LANG_EXIT_OK)
    return status;
  printf("%s %u", regime_name(checked), lang_members(checked));
  for (size_t i = 0; i < count; i++)
    printf(" %u", (unsigned)codes[i]);
  putchar('\n');
  return LANG_EXIT_OK;
}

/* build PROG [--runtime] -o OUT: OUT is the last argument. */
static int verb_build(LangChecked *checked, Diag *diag, int argc, char **argv) {
  LangContract contract;
  int status = lang_table(checked, &contract.codes, &contract.count);
  if (status != LANG_EXIT_OK)
    return status;
  contract.members = lang_members(checked);
  contract.regime = lang_regime(checked);
  contract.decisions = lang_decisions(checked);
  const char *path = argv[argc - 1];
  FILE *out = fopen(path, "w");
  if (out == NULL) {
    diag_set(diag, "IO_WRITE", span_of("-"), "%s: %s", path, strerror(errno));
    return LANG_EXIT_USAGE;
  }
  LangPart part = argc == 6 ? LANG_PART_RUNTIME : LANG_PART_CREATION;
  int failed = lang_evm_write(&contract, part, out, stderr);
  int closed = fclose(out);
  if (failed)
    return LANG_EXIT_REFUSED;
  if (closed != 0) {
    diag_set(diag, "IO_WRITE", span_of("-"), "%s: %s", path, strerror(errno));
    return LANG_EXIT_USAGE;
  }
  return LANG_EXIT_OK;
}

static int run_verb(LangChecked *checked, const Verb *verb, Diag *diag, int argc, char **argv) {
  switch (verb->kind) {
  case VERB_CHECK:
    printf("ok %s\n", regime_name(checked));
    return LANG_EXIT_OK;
  case VERB_TABLE: return verb_table(checked);
  case VERB_VERDICTS: return lang_verdicts(checked, argv[3], stdout);
  case VERB_EVAL: return lang_eval(checked, argv[3], stdout);
  case VERB_BUILD: return verb_build(checked, diag, argc, argv);
  }
  return LANG_EXIT_USAGE;
}

static int run(Arena *arena, const Verb *verb, Diag *diag, int argc, char **argv) {
  const char *path = argv[2];
  Program prelude;
  const char *prelude_text = (const char *)domain_source;
  int status = lang_parse(arena, LANG_PRELUDE_NAME, prelude_text, domain_source_len, &prelude, diag);
  if (status != LANG_EXIT_OK)
    return status;
  const char *text = NULL;
  size_t size = 0;
  status = lang_read_source(arena, path, &text, &size, diag);
  if (status != LANG_EXIT_OK)
    return status;
  Program program;
  status = lang_parse(arena, path, text, size, &program, diag);
  if (status != LANG_EXIT_OK)
    return status;
  LangChecked *checked = NULL;
  status = lang_check(arena, &prelude, &program, &checked, diag);
  if (status != LANG_EXIT_OK)
    return status;
  return run_verb(checked, verb, diag, argc, argv);
}

int main(int argc, char **argv) {
  const Verb *verb = argc < 3 ? NULL : find_verb(argv[1]);
  if (verb == NULL || !arguments_fit(verb, argc, argv))
    return usage();
  Arena arena;
  Diag diag;
  arena_init(&arena, LANG_ARENA_MAX);
  diag_init(&diag);
  int status = run(&arena, verb, &diag, argc, argv);
  fflush(stdout);
  diag_print(&diag, stderr);
  arena_free(&arena);
  return status;
}
