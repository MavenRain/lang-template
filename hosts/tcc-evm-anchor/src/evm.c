/* The EVM back end of langc (evm.h). The assembler writes bytecode
 * directly; lang_evm_write is the target interface.
 *
 * Every failure of a contract is REVERT with empty output: a short
 * calldata, an unknown selector, a call value and a failed guard. */
#include "evm.h"
#include "keccak.h"
#include <stdarg.h>
#include <string.h>

static int fail(FILE *err, const char *code, const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(err, "langc: %s: -: ", code);
  vfprintf(err, format, args);
  fputc('\n', err);
  va_end(args);
  return 1;
}

void evm_init(EvmAsm *a) { memset(a, 0, sizeof *a); }

void evm_byte(EvmAsm *a, unsigned value) {
  a->full = a->full || a->size >= EVM_CAPACITY;
  if (a->full)
    return;
  a->code[a->size] = (unsigned char)value;
  a->size++;
}

void evm_op(EvmAsm *a, EvmOp code) { evm_byte(a, (unsigned)code); }

void evm_push_word(EvmAsm *a, const unsigned char word[32]) {
  size_t lead = 0;
  while (lead < 32 && word[lead] == 0)
    lead++;
  evm_byte(a, (unsigned)OP_PUSH0 + (unsigned)(32 - lead));
  for (size_t i = lead; i < 32; i++)
    evm_byte(a, word[i]);
}

void evm_push(EvmAsm *a, unsigned long value) {
  unsigned char word[32] = {0};
  for (size_t i = 0; i < sizeof value && i < 32; i++)
    word[31 - i] = (unsigned char)(value >> (8 * i));
  evm_push_word(a, word);
}

void evm_push_label(EvmAsm *a, EvmLabel label) {
  a->broken = a->broken || label >= EVM_LABELS;
  a->full = a->full || a->sites >= EVM_FIXUPS;
  if (a->full || a->broken)
    return;
  evm_op(a, OP_PUSH2);
  a->site[a->sites] = a->size;
  a->target[a->sites] = label;
  a->sites++;
  evm_byte(a, 0);
  evm_byte(a, 0);
}

void evm_bind(EvmAsm *a, EvmLabel label) {
  a->broken = a->broken || label >= EVM_LABELS;
  if (a->broken)
    return;
  a->at[label] = a->size;
  a->bound[label] = 1;
}

void evm_jumpdest(EvmAsm *a, EvmLabel label) {
  evm_bind(a, label);
  evm_op(a, OP_JUMPDEST);
}

void evm_jump(EvmAsm *a, EvmLabel label) {
  evm_push_label(a, label);
  evm_op(a, OP_JUMP);
}

void evm_jump_if(EvmAsm *a, EvmLabel label) {
  evm_push_label(a, label);
  evm_op(a, OP_JUMPI);
}

void evm_revert_if(EvmAsm *a) { evm_jump_if(a, EVM_LABEL_REVERT); }

/* Each site holds two bytes below a->size, because evm_finish runs only
 * when no evm_byte was dropped. */
static int resolve(EvmAsm *a) {
  int ok = !a->full && !a->broken;
  for (size_t i = 0; ok && i < a->sites; i++) {
    EvmLabel label = a->target[i];
    size_t site = a->site[i];
    ok = label < EVM_LABELS && a->bound[label] && a->at[label] <= 0xffff && site + 1 < a->size;
    if (ok) {
      a->code[site] = (unsigned char)(a->at[label] >> 8);
      a->code[site + 1] = (unsigned char)a->at[label];
    }
  }
  return ok;
}

void evm_argument(EvmAsm *a, unsigned j) {
  evm_push(a, 4ul + 32ul * j);
  evm_op(a, OP_CALLDATALOAD);
}

void evm_dispatch_head(EvmAsm *a) {
  evm_push(a, 4);
  evm_op(a, OP_CALLDATASIZE);
  evm_op(a, OP_LT);
  evm_revert_if(a);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_CALLDATALOAD);
  evm_push(a, 0xe0);
  evm_op(a, OP_SHR);
}

void evm_dispatch(EvmAsm *a, const char *signature, EvmLabel label) {
  unsigned char digest[32] = {0};
  lang_keccak256((const unsigned char *)signature, strlen(signature), digest);
  evm_op(a, OP_DUP1);
  evm_op(a, OP_PUSH4);
  for (size_t i = 0; i < 4; i++)
    evm_byte(a, digest[i]);
  evm_op(a, OP_EQ);
  evm_jump_if(a, label);
}

void evm_entry(EvmAsm *a, EvmLabel label, unsigned words) {
  evm_jumpdest(a, label);
  evm_op(a, OP_POP);
  evm_op(a, OP_CALLVALUE);
  evm_revert_if(a);
  evm_push(a, 4ul + 32ul * words);
  evm_op(a, OP_CALLDATASIZE);
  evm_op(a, OP_LT);
  evm_revert_if(a);
}

void evm_return_top(EvmAsm *a) {
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_MSTORE);
  evm_push(a, 0x20);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_RETURN);
}

void evm_revert_block(EvmAsm *a) {
  evm_jumpdest(a, EVM_LABEL_REVERT);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_REVERT);
}

void evm_creation(EvmAsm *a, const EvmAsm *body) {
  evm_push(a, body->size);
  evm_op(a, OP_DUP1);
  evm_push_label(a, EVM_LABEL_RUNTIME);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_CODECOPY);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_RETURN);
  evm_revert_block(a);
  evm_bind(a, EVM_LABEL_RUNTIME);
  for (size_t i = 0; i < body->size && i < EVM_CAPACITY; i++)
    evm_byte(a, body->code[i]);
  evm_bind(a, EVM_LABEL_END);
}

int evm_finish(EvmAsm *a, FILE *err) {
  if (a->full)
    return fail(err, "EVM_SIZE", "the bytecode exceeds %d bytes or %d label sites", EVM_CAPACITY,
                EVM_FIXUPS);
  if (!resolve(a))
    return fail(err, "EVM_INTERNAL", "a jump label is out of range or unbound");
  return 0;
}

int evm_write_hex(const EvmAsm *a, FILE *out, FILE *err) {
  for (size_t i = 0; i < a->size && i < EVM_CAPACITY; i++)
    fprintf(out, "%02x", a->code[i]);
  fputc('\n', out);
  if (fflush(out) != 0 || ferror(out))
    return fail(err, "EVM_IO", "cannot write the bytecode");
  return 0;
}

/* ---- the target ---- */

/* Storage (SPEC section 7): the count of candidate c is slot c; the ballot
 * of member position i is slot K + i; the member slot of an address a is
 * keccak256(a), i + 1 for the member at position i and 0 for each other
 * address; the pair slot of (h, t) is keccak256(h . t), 1 when the log
 * holds the pair. When C > 1, slot K + M holds the current constitution
 * (the zero word is constitution 0). The two keccak inputs have different
 * sizes (32 and 64 bytes). Memory 0 to 0x3f is the keccak input of a slot,
 * and 0x40 to 0x5f is the word that a read of the outcome table copies. */
enum { MEM_ARGUMENTS = 0x20 };

enum {
  LABEL_ANCHOR = EVM_LABEL_TARGET, LABEL_VERIFY, LABEL_CAST, LABEL_MEMBER, LABEL_BINOMIAL,
  LABEL_ROWS, LABEL_POLICIES, LABEL_RANK, LABEL_RANK_LOOP, LABEL_RANK_DONE, LABEL_ANCHOR_BACK,
  LABEL_ANCHOR_HELD, LABEL_CAST_OLD, LABEL_CAST_NEW, LABEL_CAST_OK, LABEL_AMEND, LABEL_AMEND_BACK,
  LABEL_AMEND_OK, LABEL_AMEND_SAME, LABEL_DISPUTE, LABEL_DISPUTE_BACK
};

typedef struct {
  const char *signature;
  const char *inputs;
  const char *outputs;  /* "-": no output */
  EvmLabel label;
} Entry;

/* The entries of SPEC section 7, in the order of the dispatch. amend
 * exists only when C > 1 (O3 b8), and dispute only when some policy has a
 * window > 0 (O7). */
static const Entry ENTRIES[] = {
  {"anchor(bytes32)", "bytes32", "uint256", LABEL_ANCHOR},
  {"verify(bytes32,uint256)", "bytes32,uint256", "uint256", LABEL_VERIFY},
  {"cast(uint256)", "uint256", "-", LABEL_CAST},
  {"amend(uint256)", "uint256", "-", LABEL_AMEND},
  {"dispute(bytes32,uint256,bytes32)", "bytes32,uint256,bytes32", "-", LABEL_DISPUTE}
};

/* The Anchored log of anchor: topic 0 is keccak256 of EVENT, topic 1 is h
 * and the data is t. */
static const char EVENT[] = "Anchored(bytes32,uint256)";

/* The Amended log of amend (O3 b7): topic 0 is keccak256 of AMENDED and the
 * data is k. */
static const char AMENDED[] = "Amended(uint256)";

/* The Disputed log of dispute (O7): topic 0 is keccak256 of DISPUTED, topic
 * 1 is h and the data is t and the note. */
static const char DISPUTED[] = "Disputed(bytes32,uint256,bytes32)";

/* 1 when some policy of CONTRACT has a window > 0: then the dispute entry,
 * the window of each policy record and the Disputed log exist (O7). */
static int disputes_of(const AnchorContract *contract) {
  int some = 0;
  for (size_t i = 0; contract->policies != NULL && i < contract->npolicies; i++)
    some = some || contract->policies[i].window > 0;
  return some;
}

/* 1 when the entry E of ENTRIES exists in CONTRACT: amend only when C > 1,
 * dispute only when some policy has a window > 0. */
static int entry_on(const AnchorContract *contract, size_t e) {
  int amend_on = ENTRIES[e].label != LABEL_AMEND || contract->constitutions > 1;
  int dispute_on = ENTRIES[e].label != LABEL_DISPUTE || disputes_of(contract);
  return amend_on && dispute_on;
}

/* The bytes of a policy record: admit and schema, then the window when some
 * policy has a window > 0 (O7), then the amend mask when C > 1. */
static unsigned record_of(const AnchorContract *contract) {
  return 9u + (disputes_of(contract) ? 8u : 0u) + (contract->constitutions > 1 ? 1u : 0u);
}

/* word -> keccak256(word), the member slot of an address. */
static void member_slot(EvmAsm *a) {
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_MSTORE);
  evm_push(a, 0x20);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_SHA3);
}

/* offset -> the N bytes of the code at offset, big-endian (N is 1 to 31).
 * The read copies one word to memory 0x40. */
static void code_read(EvmAsm *a, unsigned n) {
  evm_push(a, 0x20);
  evm_op(a, OP_SWAP1);
  evm_push(a, 0x40);
  evm_op(a, OP_CODECOPY);
  evm_push(a, 0x40);
  evm_op(a, OP_MLOAD);
  evm_push(a, 256ul - 8ul * n);
  evm_op(a, OP_SHR);
}

/* A call of the rank subroutine: -> rank. BACK is the return label. */
static void rank_call(EvmAsm *a, EvmLabel back) {
  evm_push_label(a, back);
  evm_jump(a, LABEL_RANK);
  evm_jumpdest(a, back);
}

/* rank -> the code offset of its row (5 bytes: fate, p, q). */
static void row_at(EvmAsm *a) {
  evm_push(a, 5);
  evm_op(a, OP_MUL);
  evm_push_label(a, LABEL_ROWS);
  evm_op(a, OP_ADD);
}

/* row -> row, 1 when the fate of the row is not one, else 0. */
static void not_one(EvmAsm *a) {
  evm_op(a, OP_DUP1);
  code_read(a, 1);
  evm_push(a, 1);
  evm_op(a, OP_EQ);
  evm_op(a, OP_ISZERO);
}

/* row -> the code offset of the record of its policy p (RECORD bytes:
 * admit, schema, the window when some policy has a window > 0, and the
 * amend mask when C > 1). */
static void policy_at(EvmAsm *a, unsigned record) {
  evm_push(a, 1);
  evm_op(a, OP_ADD);
  code_read(a, 2);
  evm_push(a, record);
  evm_op(a, OP_MUL);
  evm_push_label(a, LABEL_POLICIES);
  evm_op(a, OP_ADD);
}

/* row -> the schema of its policy p. */
static void schema_at(EvmAsm *a, unsigned record) {
  policy_at(a, record);
  evm_push(a, 1);
  evm_op(a, OP_ADD);
  code_read(a, 8);
}

/* rank -> c R + rank, the row number under the current constitution c
 * (slot K + M). No code when C = 1 (O3 b8). */
static void current_rank(EvmAsm *a, const AnchorContract *contract) {
  if (contract->constitutions < 2)
    return;
  evm_push(a, (unsigned long)(contract->candidates + contract->members));
  evm_op(a, OP_SLOAD);
  evm_push(a, contract->nrows);
  evm_op(a, OP_MUL);
  evm_op(a, OP_ADD);
}

/* anchor(bytes32 h) returns t. The guards: the caller is a member, h is
 * not 0, the row of the current tally has the fate one and its policy is
 * allow. Then t is TIMESTAMP, the pair slot of (h, t) becomes 1 and the
 * Anchored log is written. When the pair slot is set already (the same h
 * in the same block), anchor writes no log and returns t. */
static void anchor(EvmAsm *a, const AnchorContract *contract) {
  unsigned char topic[32] = {0};
  lang_keccak256((const unsigned char *)EVENT, strlen(EVENT), topic);
  evm_entry(a, LABEL_ANCHOR, 1);
  evm_op(a, OP_CALLER);
  member_slot(a);
  evm_op(a, OP_SLOAD);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* the caller is not a member */
  evm_argument(a, 0);
  evm_op(a, OP_DUP1);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* h is 0 */
  rank_call(a, LABEL_ANCHOR_BACK);   /* h, rank */
  current_rank(a, contract);
  row_at(a);
  not_one(a);
  evm_revert_if(a);                  /* h, row: the fate is not one */
  policy_at(a, record_of(contract));
  code_read(a, 1);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* h: the policy is deny */
  evm_op(a, OP_DUP1);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_MSTORE);
  evm_op(a, OP_TIMESTAMP);
  evm_op(a, OP_DUP1);
  evm_push(a, 0x20);
  evm_op(a, OP_MSTORE);
  evm_push(a, 0x40);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_SHA3);                /* h, t, pair slot */
  evm_op(a, OP_DUP1);
  evm_op(a, OP_SLOAD);
  evm_jump_if(a, LABEL_ANCHOR_HELD);
  evm_push(a, 1);
  evm_op(a, OP_SWAP1);
  evm_op(a, OP_SSTORE);              /* h, t */
  evm_op(a, OP_DUP2);
  evm_push_word(a, topic);
  evm_push(a, 0x20);
  evm_push(a, 0x20);
  evm_op(a, OP_LOG2);                /* topic 1 is h, the data is t at 0x20 */
  evm_op(a, OP_PUSH0);
  evm_jumpdest(a, LABEL_ANCHOR_HELD); /* h, t, a word */
  evm_op(a, OP_POP);
  evm_return_top(a);
}

/* verify(bytes32 h, uint256 t): 1 when the pair slot of (h, t) is set,
 * else 0. No guard. */
static void verify(EvmAsm *a) {
  evm_entry(a, LABEL_VERIFY, 2);
  evm_argument(a, 0);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_MSTORE);
  evm_argument(a, 1);
  evm_push(a, 0x20);
  evm_op(a, OP_MSTORE);
  evm_push(a, 0x40);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_SHA3);
  evm_op(a, OP_SLOAD);
  evm_op(a, OP_ISZERO);
  evm_op(a, OP_ISZERO);
  evm_return_top(a);
}

/* cast(uint256 c): the ballot of the caller moves from old to c, the count
 * of old goes down by 1 and the count of c goes up by 1. The guards: c is
 * less than K, and the caller is a member. The O6 guard: when the rows of
 * the tallies before and after the ballot moves both have the fate one,
 * the schema of the new policy is not less than the schema of the old
 * policy. */
static void cast(EvmAsm *a, const AnchorContract *contract) {
  size_t candidates = contract->candidates;
  unsigned record = record_of(contract);
  evm_entry(a, LABEL_CAST, 1);
  rank_call(a, LABEL_CAST_OLD);      /* the old rank under the code below */
  current_rank(a, contract);
  evm_argument(a, 0);                /* c */
  evm_push(a, candidates - 1);
  evm_op(a, OP_DUP2);
  evm_op(a, OP_GT);
  evm_revert_if(a);                  /* c > K - 1 */
  evm_op(a, OP_CALLER);
  member_slot(a);
  evm_op(a, OP_SLOAD);               /* c, i + 1 */
  evm_op(a, OP_DUP1);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* the caller is not a member */
  evm_push(a, candidates - 1);
  evm_op(a, OP_ADD);                 /* c, the ballot slot K + i */
  evm_op(a, OP_DUP1);
  evm_op(a, OP_SLOAD);               /* c, ballot slot, old */
  evm_op(a, OP_DUP1);
  evm_op(a, OP_SLOAD);
  evm_push(a, 1);
  evm_op(a, OP_SWAP1);
  evm_op(a, OP_SUB);                 /* c, ballot slot, old, count of old - 1 */
  evm_op(a, OP_SWAP1);
  evm_op(a, OP_SSTORE);              /* c, ballot slot */
  evm_op(a, OP_DUP2);
  evm_op(a, OP_SWAP1);
  evm_op(a, OP_SSTORE);              /* c */
  evm_op(a, OP_DUP1);
  evm_op(a, OP_SLOAD);
  evm_push(a, 1);
  evm_op(a, OP_ADD);
  evm_op(a, OP_SWAP1);
  evm_op(a, OP_SSTORE);              /* old rank; the count of c + 1 */
  rank_call(a, LABEL_CAST_NEW);      /* old rank, new rank */
  current_rank(a, contract);
  row_at(a);
  not_one(a);
  evm_jump_if(a, LABEL_CAST_OK);
  schema_at(a, record);              /* old rank, new schema */
  evm_op(a, OP_SWAP1);
  row_at(a);
  not_one(a);
  evm_jump_if(a, LABEL_CAST_OK);
  schema_at(a, record);              /* new schema, old schema */
  evm_op(a, OP_GT);
  evm_revert_if(a);                  /* O6: the schema goes down */
  evm_jumpdest(a, LABEL_CAST_OK);
  evm_op(a, OP_STOP);
}

/* amend(uint256 k): the current constitution c becomes k (O3 b2 to b7).
 * The guards: the caller is a member; when k is c, amend stops with no
 * slot and no log (b3); k is less than C; the row of the current tally
 * under c has the fate one, and bit k of the amend mask of its policy p is
 * set (amendTo p k is yes). The O6 guard (b6): when the row of the current
 * tally under k has the fate one too, its schema is not less than the
 * schema of the row under c. Then slot K + M becomes k and the Amended log
 * is written. */
static void amend(EvmAsm *a, const AnchorContract *contract) {
  unsigned char topic[32] = {0};
  lang_keccak256((const unsigned char *)AMENDED, strlen(AMENDED), topic);
  unsigned long slot = (unsigned long)(contract->candidates + contract->members);
  unsigned record = record_of(contract);
  evm_entry(a, LABEL_AMEND, 1);
  evm_op(a, OP_CALLER);
  member_slot(a);
  evm_op(a, OP_SLOAD);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* the caller is not a member */
  evm_argument(a, 0);                /* k */
  evm_op(a, OP_DUP1);
  evm_push(a, slot);
  evm_op(a, OP_SLOAD);
  evm_op(a, OP_EQ);
  evm_jump_if(a, LABEL_AMEND_SAME);  /* k is c (b3) */
  evm_push(a, contract->constitutions - 1);
  evm_op(a, OP_DUP2);
  evm_op(a, OP_GT);
  evm_revert_if(a);                  /* k > C - 1 */
  rank_call(a, LABEL_AMEND_BACK);    /* k, rank */
  evm_op(a, OP_DUP1);
  current_rank(a, contract);
  row_at(a);
  not_one(a);
  evm_revert_if(a);                  /* k, rank, old row: the fate is not one */
  evm_op(a, OP_DUP1);
  policy_at(a, record);
  evm_push(a, record - 1);
  evm_op(a, OP_ADD);
  code_read(a, 1);                   /* k, rank, old row, mask (the last byte of the record) */
  evm_op(a, OP_DUP4);
  evm_op(a, OP_SHR);
  evm_push(a, 1);
  evm_op(a, OP_AND);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* k, rank, old row: amendTo p k is not yes */
  evm_op(a, OP_SWAP1);
  evm_op(a, OP_DUP3);
  evm_push(a, contract->nrows);
  evm_op(a, OP_MUL);
  evm_op(a, OP_ADD);
  row_at(a);                         /* k, old row, new row */
  not_one(a);
  evm_jump_if(a, LABEL_AMEND_OK);
  evm_op(a, OP_DUP1);
  schema_at(a, record);
  evm_op(a, OP_DUP3);
  schema_at(a, record);              /* k, old row, new row, new schema, old schema */
  evm_op(a, OP_GT);
  evm_revert_if(a);                  /* O6: the schema goes down */
  evm_jumpdest(a, LABEL_AMEND_OK);
  evm_op(a, OP_POP);
  evm_op(a, OP_POP);
  evm_op(a, OP_DUP1);
  evm_push(a, slot);
  evm_op(a, OP_SSTORE);              /* k: slot K + M is k */
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_MSTORE);
  evm_push_word(a, topic);
  evm_push(a, 0x20);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_LOG1);                /* the data is k at 0 */
  evm_jumpdest(a, LABEL_AMEND_SAME);
  evm_op(a, OP_STOP);
}

/* dispute(bytes32 h, uint256 t, bytes32 note) annotates the pair (h, t)
 * with the note (O7). The guards: the caller is a member, the pair slot of
 * (h, t) is set, the row of the current tally has the fate one, and
 * TIMESTAMP < t + window, with the window of its policy p. Then the
 * Disputed log is written. No slot changes, so verify does not change. */
static void dispute(EvmAsm *a, const AnchorContract *contract) {
  unsigned char topic[32] = {0};
  lang_keccak256((const unsigned char *)DISPUTED, strlen(DISPUTED), topic);
  evm_entry(a, LABEL_DISPUTE, 3);
  evm_op(a, OP_CALLER);
  member_slot(a);
  evm_op(a, OP_SLOAD);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* the caller is not a member */
  evm_argument(a, 0);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_MSTORE);
  evm_argument(a, 1);
  evm_push(a, 0x20);
  evm_op(a, OP_MSTORE);
  evm_push(a, 0x40);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_SHA3);
  evm_op(a, OP_SLOAD);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* the pair slot of (h, t) is 0 */
  rank_call(a, LABEL_DISPUTE_BACK);  /* rank */
  current_rank(a, contract);
  row_at(a);
  not_one(a);
  evm_revert_if(a);                  /* row: the fate is not one */
  policy_at(a, record_of(contract));
  evm_push(a, 9);
  evm_op(a, OP_ADD);
  code_read(a, 8);                   /* window */
  evm_argument(a, 1);
  evm_op(a, OP_ADD);
  evm_op(a, OP_TIMESTAMP);
  evm_op(a, OP_LT);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* TIMESTAMP is not less than t + window */
  evm_argument(a, 1);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_MSTORE);
  evm_argument(a, 2);
  evm_push(a, 0x20);
  evm_op(a, OP_MSTORE);
  evm_argument(a, 0);
  evm_push_word(a, topic);
  evm_push(a, 0x40);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_LOG2);                /* topic 1 is h, the data is t and the note at 0 */
  evm_op(a, OP_STOP);
}

/* The rank subroutine: return label -> rank, the row of the current tally
 * in the outcome table. With S_d the sum of the last d counts, the rank is
 * the sum over d = 1 to K - 1 of C(S_d + d - 1, d), from the binomial part.
 * The loop stack is: return label, rank, S, base, j. */
static void rank(EvmAsm *a, const AnchorContract *contract) {
  evm_jumpdest(a, LABEL_RANK);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_PUSH0);
  evm_push_label(a, LABEL_BINOMIAL);
  evm_push(a, contract->candidates - 1);
  evm_jumpdest(a, LABEL_RANK_LOOP);
  evm_op(a, OP_DUP1);
  evm_op(a, OP_ISZERO);
  evm_jump_if(a, LABEL_RANK_DONE);   /* j is 0 */
  evm_op(a, OP_DUP1);
  evm_op(a, OP_SLOAD);
  evm_op(a, OP_DUP4);
  evm_op(a, OP_ADD);
  evm_op(a, OP_SWAP3);
  evm_op(a, OP_POP);                 /* S + the count of j */
  evm_op(a, OP_DUP3);
  evm_op(a, OP_DUP1);
  evm_op(a, OP_ADD);
  evm_op(a, OP_DUP3);
  evm_op(a, OP_ADD);
  code_read(a, 2);                   /* the term at base + 2S */
  evm_op(a, OP_DUP5);
  evm_op(a, OP_ADD);
  evm_op(a, OP_SWAP4);
  evm_op(a, OP_POP);                 /* rank + the term */
  evm_op(a, OP_SWAP1);
  evm_push(a, 2ul * ((unsigned long)contract->members + 1ul));
  evm_op(a, OP_ADD);
  evm_op(a, OP_SWAP1);               /* base + 2(M + 1) */
  evm_push(a, 1);
  evm_op(a, OP_SWAP1);
  evm_op(a, OP_SUB);                 /* j - 1 */
  evm_jump(a, LABEL_RANK_LOOP);
  evm_jumpdest(a, LABEL_RANK_DONE);
  evm_op(a, OP_POP);
  evm_op(a, OP_POP);
  evm_op(a, OP_POP);
  evm_op(a, OP_SWAP1);
  evm_op(a, OP_JUMP);
}

static void runtime(EvmAsm *a, const AnchorContract *contract) {
  evm_dispatch_head(a);
  for (size_t e = 0; e < sizeof ENTRIES / sizeof ENTRIES[0]; e++)
    if (entry_on(contract, e))
      evm_dispatch(a, ENTRIES[e].signature, ENTRIES[e].label);
  evm_revert_block(a);  /* an unknown selector falls through to here */
  anchor(a, contract);
  verify(a);
  cast(a, contract);
  if (contract->constitutions > 1)
    amend(a, contract);
  if (disputes_of(contract))
    dispute(a, contract);
  rank(a, contract);  /* the outcome table comes after this */
}

/* C(s + d - 1, d), or 0xffff when it is larger: c is C(s - 1 + j, j) after
 * step j, so each division is exact. */
static unsigned long long binomial(unsigned long long s, size_t d) {
  unsigned long long c = 1;
  for (size_t j = 1; j <= d && c <= 0xffff; j++)
    c = c * (s - 1 + j) / j;
  return c > 0xffff ? 0xffff : c;
}

/* The N low bytes of VALUE, big-endian. */
static void bytes_of(EvmAsm *a, unsigned long long value, unsigned n) {
  for (unsigned i = n; i > 0; i--)
    evm_byte(a, (unsigned)(value >> (8 * (i - 1))) & 0xff);
}

/* The outcome table (SPEC section 7) after the last instruction, big-endian.
 * The binomial part: for d = 1 to K - 1 and S = 0 to members, 2 bytes of
 * C(S + d - 1, d). The rows: for each constitution c and each tally, 1
 * byte of fate, 2 bytes of p and 2 bytes of q (row c R + rank). The policy
 * records: for each policy, 1 byte of admit (1 allow, 0 deny), 8 bytes of
 * schema, 8 bytes of window when some policy has a window > 0 (O7) and,
 * when C > 1, 1 byte of the amend mask. TABLE_LIMIT (4096 rows
 * of C R) keeps each value in its width: a policy number is less than K +
 * 2 C R, at most 12288, and a binomial entry that the runtime reads is at
 * most a row number. */
static void table(EvmAsm *a, const AnchorContract *contract) {
  evm_bind(a, LABEL_BINOMIAL);
  for (size_t d = 1; d < contract->candidates; d++)
    for (unsigned long long s = 0; s <= contract->members; s++)
      bytes_of(a, binomial(s, d), 2);
  evm_bind(a, LABEL_ROWS);
  for (size_t r = 0; r < contract->constitutions * contract->nrows; r++) {
    bytes_of(a, contract->rows[r].fate, 1);
    bytes_of(a, contract->rows[r].p, 2);
    bytes_of(a, contract->rows[r].q, 2);
  }
  evm_bind(a, LABEL_POLICIES);
  int disputes = disputes_of(contract);
  for (size_t i = 0; i < contract->npolicies; i++) {
    bytes_of(a, contract->policies[i].allow != 0, 1);
    bytes_of(a, contract->policies[i].schema, 8);
    if (disputes)
      bytes_of(a, contract->policies[i].window, 8);
    if (contract->constitutions > 1)
      bytes_of(a, contract->amend[i], 1);
  }
}

/* The bytes of the outcome table. */
static unsigned long long table_size(const AnchorContract *contract) {
  unsigned long long binomials = (unsigned long long)(contract->candidates - 1) * (contract->members + 1ull);
  unsigned long long rows = (unsigned long long)contract->constitutions * contract->nrows;
  return 2 * binomials + 5ull * rows + (unsigned long long)record_of(contract) * contract->npolicies;
}

/* 1 when each row has a fate of 0 to 2 and names policies that exist. */
static int rows_ok(const AnchorContract *contract) {
  int ok = contract->npolicies <= 0xffff;
  for (size_t r = 0; ok && r < contract->constitutions * contract->nrows; r++) {
    const AnchorContractRow *row = &contract->rows[r];
    int p_ok = row->fate < 1 || row->p < contract->npolicies;
    int q_ok = row->fate < 2 || row->q < contract->npolicies;
    int named = p_ok && q_ok;
    ok = row->fate <= 2 && named;
  }
  return ok;
}

/* The constructor (O5, O10). The creation code ends with one address word
 * for each member position, and nothing after them. Each word must be a
 * nonzero address that no earlier word repeats. The member slot of each
 * address gets its position + 1, and the count of candidate 0 gets
 * members. Each ballot stays the zero word, which is candidate 0. */
static void constructor(EvmAsm *a, unsigned members) {
  unsigned long bytes = 32ul * members;
  evm_op(a, OP_CALLVALUE);
  evm_revert_if(a);
  evm_push(a, bytes);
  evm_push_label(a, EVM_LABEL_END);
  evm_op(a, OP_ADD);
  evm_op(a, OP_CODESIZE);
  evm_op(a, OP_EQ);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* not members argument words */
  evm_push(a, bytes);
  evm_push_label(a, EVM_LABEL_END);
  evm_push(a, MEM_ARGUMENTS);
  evm_op(a, OP_CODECOPY);
  evm_op(a, OP_PUSH0);               /* i */
  evm_jumpdest(a, LABEL_MEMBER);
  evm_op(a, OP_DUP1);
  evm_push(a, 0x20);
  evm_op(a, OP_MUL);
  evm_push(a, MEM_ARGUMENTS);
  evm_op(a, OP_ADD);
  evm_op(a, OP_MLOAD);               /* i, word */
  evm_op(a, OP_DUP1);
  evm_push(a, 0xa0);
  evm_op(a, OP_SHR);
  evm_revert_if(a);                  /* not an address */
  evm_op(a, OP_DUP1);
  evm_op(a, OP_ISZERO);
  evm_revert_if(a);                  /* the zero address */
  member_slot(a);                    /* i, member slot */
  evm_op(a, OP_DUP1);
  evm_op(a, OP_SLOAD);
  evm_revert_if(a);                  /* an address that an earlier word gave */
  evm_op(a, OP_DUP2);
  evm_push(a, 1);
  evm_op(a, OP_ADD);
  evm_op(a, OP_SWAP1);
  evm_op(a, OP_SSTORE);              /* i */
  evm_push(a, 1);
  evm_op(a, OP_ADD);
  evm_op(a, OP_DUP1);
  evm_push(a, members);
  evm_op(a, OP_GT);
  evm_jump_if(a, LABEL_MEMBER);      /* while i + 1 < members */
  evm_op(a, OP_POP);
  evm_push(a, members);
  evm_op(a, OP_PUSH0);
  evm_op(a, OP_SSTORE);              /* the tally (members, 0, ..., 0) */
}

int lang_evm_write(const AnchorContract *contract, AnchorPart part, FILE *out, FILE *err) {
  if (contract == NULL)
    return fail(err, "EVM_USAGE", "no contract");
  if (contract->members < 1)
    return fail(err, "EVM_LIMIT", "a contract needs at least 1 member, got 0");
  if (contract->candidates < 1)
    return fail(err, "EVM_LIMIT", "a contract needs at least 1 candidate, got 0");
  if (contract->constitutions < 1 || contract->constitutions > 8)
    return fail(err, "EVM_LIMIT", "a contract needs 1 to 8 constitutions, got %lu",
                (unsigned long)contract->constitutions);
  if (contract->rows == NULL || contract->policies == NULL || (contract->constitutions > 1 && contract->amend == NULL))
    return fail(err, "EVM_USAGE", "no outcome table");
  if (!rows_ok(contract))
    return fail(err, "EVM_INTERNAL", "a row of the outcome table names no policy");
  EvmAsm body;
  EvmAsm creation;
  evm_init(&body);
  evm_init(&creation);
  runtime(&body, contract);
  unsigned long long bytes = body.size + table_size(contract);
  if (bytes > EVM_RUNTIME_MAX)
    return fail(err, "EVM_SIZE", "the runtime has %llu bytes, the limit is %d", bytes, EVM_RUNTIME_MAX);
  table(&body, contract);
  int bad = evm_finish(&body, err);
  if (bad)
    return bad;
  constructor(&creation, contract->members);
  evm_creation(&creation, &body);
  bad = evm_finish(&creation, err);
  if (bad)
    return bad;
  unsigned long initcode = creation.size + 32ul * contract->members;
  if (initcode > EVM_INITCODE_MAX)
    return fail(err, "EVM_SIZE", "the creation code and %u member words have %lu bytes, the limit is %d (EIP-3860)",
                contract->members, initcode, EVM_INITCODE_MAX);
  return evm_write_hex(part == LANG_PART_RUNTIME ? &body : &creation, out, err);
}

int lang_abi_write(const AnchorContract *contract, FILE *out, FILE *err) {
  if (contract == NULL)
    return fail(err, "EVM_USAGE", "no contract");
  fprintf(out, "constructor inputs address[%u]\n", contract->members);
  for (size_t e = 0; e < sizeof ENTRIES / sizeof ENTRIES[0]; e++) {
    if (!entry_on(contract, e))
      continue;
    unsigned char digest[32] = {0};
    lang_keccak256((const unsigned char *)ENTRIES[e].signature, strlen(ENTRIES[e].signature), digest);
    fprintf(out, "entry %s selector %02x%02x%02x%02x inputs %s outputs %s\n", ENTRIES[e].signature,
            digest[0], digest[1], digest[2], digest[3], ENTRIES[e].inputs, ENTRIES[e].outputs);
  }
  unsigned char topic[32] = {0};
  lang_keccak256((const unsigned char *)EVENT, strlen(EVENT), topic);
  fprintf(out, "event %s topic ", EVENT);
  for (size_t i = 0; i < 32; i++)
    fprintf(out, "%02x", topic[i]);
  fputs(" indexed bytes32 data uint256\n", out);
  if (contract->constitutions > 1) {
    lang_keccak256((const unsigned char *)AMENDED, strlen(AMENDED), topic);
    fprintf(out, "event %s topic ", AMENDED);
    for (size_t i = 0; i < 32; i++)
      fprintf(out, "%02x", topic[i]);
    fputs(" data uint256\n", out);
  }
  if (disputes_of(contract)) {
    lang_keccak256((const unsigned char *)DISPUTED, strlen(DISPUTED), topic);
    fprintf(out, "event %s topic ", DISPUTED);
    for (size_t i = 0; i < 32; i++)
      fprintf(out, "%02x", topic[i]);
    fputs(" indexed bytes32 data uint256,bytes32\n", out);
  }
  if (fflush(out) != 0 || ferror(out))
    return fail(err, "EVM_IO", "cannot write the entries");
  return 0;
}
