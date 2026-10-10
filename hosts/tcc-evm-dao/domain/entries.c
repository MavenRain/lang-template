/* The domain entries of langc: the escrow storage, the entries that move
 * value and the entry list of each regime (host README, The sample domain). src/evm.c
 * writes the dispatcher and the head of each entry, then the bodies below.
 *
 * Storage: ledger (slot 0, address -> word), claimCount (slot 1), payer,
 * payee and amount (slots 2, 3 and 4, index -> word). A mapping entry
 * lives at keccak256(key . slot), as in Solidity. */
#include "asm.h"
#include "check.h"

enum { SLOT_LEDGER = 0, SLOT_COUNT = 1, SLOT_PAYER = 2, SLOT_PAYEE = 3, SLOT_AMOUNT = 4 };

enum { MEM_DECISION = 0x80, MEM_PAYER = 0xa0, MEM_AMOUNT = 0xc0, MEM_BALANCE = 0xe0 };

/* deposit p q n: guard n <= callvalue, credit p, append the claim
 * (p, q, n) and return its index. */
static void deposit(Asm *a, const EntryContext *c) {
  (void)c;
  asm_address_guard(a, 0);
  asm_address_guard(a, 1);
  asm_op(a, OP_CALLVALUE);
  asm_argument(a, 2);
  asm_op(a, OP_GT);
  asm_revert_if(a);
  asm_argument(a, 0);
  asm_slot(a, SLOT_LEDGER);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_argument(a, 2);
  asm_checked_add(a);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  asm_push(a, SLOT_COUNT);
  asm_op(a, OP_SLOAD);
  for (unsigned j = 0; j < 3; j++) {
    asm_argument(a, j);
    asm_op(a, OP_DUP2);
    asm_slot(a, SLOT_PAYER + j);
    asm_op(a, OP_SSTORE);
  }
  asm_op(a, OP_DUP1);
  asm_push(a, 1);
  asm_checked_add(a);
  asm_push(a, SLOT_COUNT);
  asm_op(a, OP_SSTORE);
  asm_return_top(a);
}

static void debit_payer(Asm *a) {
  asm_load(a, MEM_AMOUNT);
  asm_load(a, MEM_BALANCE);
  asm_op(a, OP_SUB);
  asm_load(a, MEM_PAYER);
  asm_slot(a, SLOT_LEDGER);
  asm_op(a, OP_SSTORE);
}

/* The payee balance is read after the debit, so a claim with payer =
 * payee leaves the balance unchanged. */
static void credit_payee(Asm *a) {
  asm_argument(a, 0);
  asm_slot(a, SLOT_PAYEE);
  asm_op(a, OP_SLOAD);
  asm_slot(a, SLOT_LEDGER);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_load(a, MEM_AMOUNT);
  asm_checked_add(a);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
}

/* settle c x: guard amount c <= balance (payer c) (the proof h), then the
 * release, refund or hold leg by the decision (host README): code 1
 * releases, code 2 refunds, any other code holds. */
static void settle(Asm *a, const EntryContext *c) {
  Label release = asm_label(a);
  Label refund = asm_label(a);
  Label done = asm_label(a);
  asm_tally(a, 1, c->members, c->decisions);
  asm_store(a, MEM_DECISION);
  asm_argument(a, 0);
  asm_slot(a, SLOT_PAYER);
  asm_op(a, OP_SLOAD);
  asm_store(a, MEM_PAYER);
  asm_argument(a, 0);
  asm_slot(a, SLOT_AMOUNT);
  asm_op(a, OP_SLOAD);
  asm_store(a, MEM_AMOUNT);
  asm_load(a, MEM_PAYER);
  asm_slot(a, SLOT_LEDGER);
  asm_op(a, OP_SLOAD);
  asm_store(a, MEM_BALANCE);
  asm_load(a, MEM_BALANCE);
  asm_load(a, MEM_AMOUNT);
  asm_op(a, OP_GT);
  asm_revert_if(a);
  asm_load(a, MEM_DECISION);
  asm_push(a, 2);
  asm_op(a, OP_GT);
  asm_jump_if(a, release);
  asm_load(a, MEM_DECISION);
  asm_push(a, 3);
  asm_op(a, OP_GT);
  asm_jump_if(a, refund);
  asm_jump(a, done);
  asm_jumpdest(a, release);
  debit_payer(a);
  credit_payee(a);
  asm_jump(a, done);
  asm_jumpdest(a, refund);
  debit_payer(a);
  asm_jumpdest(a, done);
  asm_load(a, MEM_DECISION);
  asm_return_top(a);
}

static const Entry impossibility[] = {
  {"deposit", 3, 0, ENTRY_PAYABLE, deposit, NULL},
};

static const Entry debreu[] = {
  {"deposit", 3, 0, ENTRY_PAYABLE, deposit, NULL},
  {"cast", 0, 1, ENTRY_NONPAYABLE, lang_entry_cast, NULL},
  {"settle", 1, 1, ENTRY_NONPAYABLE, settle, NULL},
  {"amend", 0, 0, ENTRY_NONPAYABLE, lang_entry_amend, NULL},
};

/* The sample has one table per regime: it does not read C. */
const Entry *lang_domain_entries(LangRegime regime, const EntryContext *c, size_t *count) {
  (void)c;
  switch (regime) {
    case LANG_REGIME_IMPOSSIBILITY:
      *count = sizeof impossibility / sizeof impossibility[0];
      return impossibility;
    case LANG_REGIME_DEBREU:
      *count = sizeof debreu / sizeof debreu[0];
      return debreu;
  }
  *count = 0;
  return NULL;
}

/* The sample domain writes no genesis storage. */
void lang_domain_genesis(Asm *a, const LangContract *contract) {
  (void)a;
  (void)contract;
}

/* The sample domain has no code data. */
void lang_domain_data(Asm *a, const EntryContext *c) {
  (void)a;
  (void)c;
}

/* The sample domain reads no program data: the contract gets NULL. */
int lang_domain_read(LangChecked *checked, const LangDomainData **data) {
  (void)checked;
  *data = NULL;
  return LANG_EXIT_OK;
}

/* The sample domain has no program data, so the `data` verb writes nothing. */
void lang_domain_print(const LangDomainData *data, FILE *out) {
  (void)data;
  (void)out;
}
