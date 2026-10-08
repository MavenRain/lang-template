/* The langc checker (SPEC section 10, chunk 3; SPEC sections 2 to 6).
 *
 * lang_check checks the program's `def members : Nat := N` first, then
 * the embedded prelude, then the rest of the program. The program must
 * define `candidates : Candidates` and `rule : Tally -> Outcome`. Errors go
 * to the Diag of the run as "langc: CODE: DEF: message":
 *
 *   REFUSE_MEMBERS, REFUSE_DATA, REFUSE_REC, REFUSE_NAME, REFUSE_FORK
 *     the refusal list of SPEC section 2;
 *   TYPE_SCOPE, TYPE_DUPLICATE, TYPE_MISMATCH (with both normal forms),
 *   TYPE_SHAPE, TYPE_INFER, TYPE_UNIVERSE, TYPE_ERASED, TYPE_MATCH, TYPE_MU,
 *   TYPE_REC, TYPE_NAT, TYPE_FUEL, TYPE_INTERNAL, MEMORY
 *     the checker. */
#ifndef LANG_CHECK_H
#define LANG_CHECK_H
#include "syntax.h"

typedef struct AnchorChecked AnchorChecked;

/* Returns LANG_EXIT_OK and the checked program in *CHECKED, or
 * LANG_EXIT_REFUSED with the first error in DIAG. DIAG must stay alive
 * while *CHECKED is used. */
int lang_check(Arena *arena, const Program *prelude, const Program *program,
                 AnchorChecked **checked, Diag *diag);
unsigned lang_members(const AnchorChecked *checked);

/* The outcome table of a checked program (SPEC section 7). */
typedef enum { LANG_FATE_NONE = 0, LANG_FATE_ONE = 1, LANG_FATE_TWO = 2 } AnchorFate;

typedef struct {
  AnchorFate fate;
  size_t p;  /* LANG_FATE_ONE, LANG_FATE_TWO: a policy number */
  size_t q;  /* LANG_FATE_TWO: a policy number */
} AnchorRow;

typedef struct {
  unsigned members;
  size_t candidates;     /* K; policies 0 to K-1 are the candidates, in order */
  size_t npolicies;      /* the candidates, then each other policy of an outcome */
  const Ast **policies;  /* the closed normal form of each policy */
  size_t nrows;          /* one row for each tally */
  const unsigned *counts; /* nrows rows of K counts */
  const AnchorRow *rows;
} AnchorTable;

/* Tabulates rule over every tally: each count vector over the candidates
 * whose sum is members, (members, 0, ..., 0) first and (0, ..., 0,
 * members) last, in reverse lexicographic order. Each outcome must reduce
 * to none, one p or two p q with closed policies, and each side of a
 * two p q must be frozen (the full fork check of SPEC section 2). Returns
 * LANG_EXIT_OK, or LANG_EXIT_REFUSED with TABLE_LIMIT, TABLE_STUCK,
 * REFUSE_FORK, TYPE_FUEL or MEMORY. The table lives in the arena of
 * CHECKED. */
int lang_table(AnchorChecked *checked, AnchorTable *table);

/* The two fields of policy I of T that the contract reads (langc build
 * and abi): *ALLOW is 1 for allow and 0 for deny, *SCHEMA is the schema.
 * Policy I must be the normal form mkPolicy v d c window schema forkFreeze
 * with v allow or deny and schema a Nat. Returns 0, else nonzero. */
int lang_policy_fields(const AnchorTable *t, size_t i, int *allow, unsigned long long *schema);

/* The stable text form of SPEC section 7. */
void lang_print_table(FILE *out, const AnchorTable *table);

/* The fate report of SPEC section 6, in the text form of SPEC section 7:
 * the tallies of each fate of TABLE. */
void lang_print_report(FILE *out, const AnchorTable *table);

/* Prints the normal form of the def or def rec NAME (of the prelude or the program)
 * in the canonical form of the printer, then a newline. Returns
 * LANG_EXIT_OK, or LANG_EXIT_REFUSED with EVAL_NAME (NAME is not
 * declared or is not a def), TYPE_FUEL or MEMORY. */
int lang_eval(AnchorChecked *checked, const char *name, FILE *out);
#endif
