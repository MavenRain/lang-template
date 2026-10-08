/* langc, the {{LANG}} compiler (SPEC section 10):
 *   langc check PROG                    the fate report (chunk 4b)
 *   langc table PROG                    the outcome of each tally (chunk 4a)
 *   langc eval PROG NAME                the normal form of NAME (chunk 4b)
 *   langc build PROG [--runtime] -o OUT the contract as hex (chunk 5a)
 *   langc abi PROG                      the entries of the contract (chunk 5a)
 * Exit 0 ok, 1 refused, 2 usage or IO; errors go to stderr as
 * "langc: CODE: DEF: message". Each verb parses the embedded prelude and
 * PROG and checks them (src/check.h), then prints its result and exits 0.
 * build and abi tabulate, and a table refusal stops them with its code. */
#include "check.h"
#include "evm.h"
#include "prelude.h"
#include "syntax.h"
#include <stdlib.h>
#include <string.h>

/* Tabulates the checked program once and prints the table with PRINT. */
static int tabulate(AnchorChecked *checked, void (*print)(FILE *, const AnchorTable *)) {
  AnchorTable table;
  int status = lang_table(checked, &table);
  if (status == LANG_EXIT_OK)
    print(stdout, &table);
  return status;
}

static int check_verb(AnchorChecked *checked, char **argv) {
  (void)argv;
  return tabulate(checked, lang_print_report);
}

static int table_verb(AnchorChecked *checked, char **argv) {
  (void)argv;
  return tabulate(checked, lang_print_table);
}

static int eval_verb(AnchorChecked *checked, char **argv) {
  return lang_eval(checked, argv[3], stdout);
}

/* The contract of the checked program (src/evm.h): the members, the
 * candidates, and the rows and policy fields of its outcome table. A table
 * refusal returns its code. contract_free frees the rows and the policies. */
static int contract_of(AnchorChecked *checked, AnchorContract *contract) {
  memset(contract, 0, sizeof *contract);
  AnchorTable table;
  int status = lang_table(checked, &table);
  if (status != LANG_EXIT_OK)
    return status;
  AnchorContractRow *rows = calloc(table.nrows + 1, sizeof *rows);
  AnchorContractPolicy *policies = calloc(table.npolicies + 1, sizeof *policies);
  contract->rows = rows;
  contract->policies = policies;
  if (rows == NULL || policies == NULL) {
    fputs("langc: MEMORY: -: cannot allocate the outcome table of the contract\n", stderr);
    return LANG_EXIT_REFUSED;
  }
  contract->members = table.members;
  contract->candidates = table.candidates;
  contract->nrows = table.nrows;
  contract->npolicies = table.npolicies;
  for (size_t r = 0; r < table.nrows; r++) {
    rows[r].fate = (unsigned)table.rows[r].fate;
    rows[r].p = table.rows[r].p;
    rows[r].q = table.rows[r].q;
  }
  for (size_t i = 0; i < table.npolicies; i++)
    if (lang_policy_fields(&table, i, &policies[i].allow, &policies[i].schema) != 0) {
      fprintf(stderr, "langc: TYPE_INTERNAL: -: policy %lu is not mkPolicy allow or deny with a Nat schema\n",
              (unsigned long)i);
      return LANG_EXIT_REFUSED;
    }
  return LANG_EXIT_OK;
}

static void contract_free(AnchorContract *contract) {
  free((void *)contract->rows);
  free((void *)contract->policies);
}

static int abi_verb(AnchorChecked *checked, char **argv) {
  (void)argv;
  AnchorContract contract;
  int status = contract_of(checked, &contract);
  if (status == LANG_EXIT_OK)
    status = lang_abi_write(&contract, stdout, stderr) == 0 ? LANG_EXIT_OK : LANG_EXIT_USAGE;
  contract_free(&contract);
  return status;
}

/* Writes the hex of CONTRACT to PATH; a refusal of the back end removes PATH. */
static int write_contract(const AnchorContract *contract, int runtime, const char *path) {
  FILE *out = fopen(path, "w");
  if (out == NULL) {
    fprintf(stderr, "langc: IO: -: cannot write %s\n", path);
    return LANG_EXIT_USAGE;
  }
  AnchorPart part = runtime ? LANG_PART_RUNTIME : LANG_PART_CREATION;
  int bad = lang_evm_write(contract, part, out, stderr);
  int write_failed = ferror(out);
  int closed = fclose(out) == 0;
  if (bad) {
    remove(path);
    return write_failed ? LANG_EXIT_USAGE : LANG_EXIT_REFUSED;
  }
  if (!closed) {
    fprintf(stderr, "langc: IO: -: cannot write %s\n", path);
    return LANG_EXIT_USAGE;
  }
  return LANG_EXIT_OK;
}

/* build PROG [--runtime] -o OUT. */
static int build_verb(AnchorChecked *checked, char **argv) {
  int runtime = strcmp(argv[3], "--runtime") == 0;
  const char *path = argv[runtime ? 5 : 4];
  AnchorContract contract;
  int status = contract_of(checked, &contract);
  if (status == LANG_EXIT_OK)
    status = write_contract(&contract, runtime, path);
  contract_free(&contract);
  return status;
}

typedef struct {
  const char *name;
  int argc;  /* argc with the verb, PROG and NAME; build adds -o OUT */
  int (*back)(AnchorChecked *checked, char **argv);
} Verb;

static const Verb VERBS[] = {
  {"check", 3, check_verb}, {"table", 3, table_verb}, {"eval", 4, eval_verb},
  {"build", 5, build_verb}, {"abi", 3, abi_verb}
};

static int usage(void) {
  fputs("langc: USAGE: -: langc check|table|abi PROG, langc eval PROG NAME,"
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
  if (strcmp(verb->name, "build") == 0)
    return build_fits(argc, argv);
  return argc == verb->argc;
}

static int run(Arena *arena, const Verb *verb, char **argv, Diag *diag) {
  const char *path = argv[2];
  Program prelude;
  const char *prelude_text = (const char *)lang_prelude_text;
  int status = lang_parse(arena, lang_prelude_name, prelude_text, lang_prelude_size, &prelude, diag);
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
  AnchorChecked *checked = NULL;
  status = lang_check(arena, &prelude, &program, &checked, diag);
  if (status != LANG_EXIT_OK)
    return status;
  return verb->back(checked, argv);
}

int main(int argc, char **argv) {
  const Verb *verb = argc < 3 ? NULL : find_verb(argv[1]);
  if (verb == NULL || !arguments_fit(verb, argc, argv))
    return usage();
  Arena arena;
  Diag diag;
  arena_init(&arena, LANG_ARENA_MAX);
  diag_init(&diag);
  int status = run(&arena, verb, argv, &diag);
  diag_print(&diag, stderr);
  arena_free(&arena);
  return status;
}
