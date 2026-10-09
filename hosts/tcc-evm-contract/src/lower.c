/* The lowering of a checked program to EVM code (slice K4a). `langc build`
   writes the creation code: it stores each nonzero word of the `init` state
   in the storage layout below, then returns the runtime code. In K4a the
   runtime code reverts each call; slice K4b adds the dispatch. The evaluator
   is the reference: `init` is a closed term, so the build stores its normal
   form.

   The storage layout: field i of the state is at slot i. A word field (Nat,
   Flag, U256, Addr) is one word in its slot. A List of words has its length
   at slot s and element j at keccak256(s) + j. A Map of words has the value
   at key k at keccak256(k . s), with k and s each as a 32-byte word. Other
   fields give REFUSE_LOWER. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "evm.h"
#include "keccak.h"
#include "lower.h"

typedef unsigned char Word[32];

/* The storage form of a state field. */
typedef enum { FORM_WORD, FORM_LIST, FORM_MAP, FORM_NONE } Form;

static int is_word_type(const Value *t) {
  return val_is(t, OP_NAT) || val_is(t, OP_FLAG) || val_is(t, OP_U256) || val_is(t, OP_ADDR);
}

static Form form_of(const Value *t) {
  if (is_word_type(t)) return FORM_WORD;
  if (val_is(t, OP_LIST) && t->argc == 1 && is_word_type(t->args[0])) return FORM_LIST;
  if (val_is(t, OP_KMAP) && t->argc == 2 && is_word_type(t->args[0]) && is_word_type(t->args[1])) return FORM_MAP;
  return FORM_NONE;
}

/* The name of a field type that K4a does not lower, for REFUSE_LOWER. */
static const char *form_name(const Value *t) {
  return val_is(t, OP_OPTION) ? "an Option"
         : val_is(t, OP_PROD) ? "a Prod"
         : val_is(t, OP_LIST) ? "a List of a type that is not a word"
         : val_is(t, OP_KMAP) ? "a Map with a key or a value that is not a word"
         : "not a word, a List of words or a Map of words";
}

/* Big-endian words: Nat as an unsigned 64-bit value, U256 and Addr from the
   limbs (limb 0 is the low limb), Flag as 0 or 1. */
static void word_u64(Word w, uint64_t n) {
  memset(w, 0, sizeof(Word));
  for (int i = 0; i < 8; i++) w[31 - i] = (unsigned char)(n >> (8 * i));
}

static void word_u256(Word w, const U256 *x) {
  for (int i = 0; i < 32; i++) w[31 - i] = (unsigned char)(x->limb[i / 8] >> (8 * (i % 8)));
}

/* Returns 1 and the word of V, or 0 when V is not a closed word value. */
static int word_of(const Value *v, Word w) {
  if (v != NULL && v->kind == VAL_NAT) {
    word_u64(w, v->nat);
    return 1;
  }
  if (v != NULL && v->kind == VAL_WORD && v->word != NULL) {
    word_u256(w, v->word);
    return 1;
  }
  if (v != NULL && (val_is(v, OP_FLAG_YES) || val_is(v, OP_FLAG_NO))) {
    word_u64(w, (uint64_t)val_is(v, OP_FLAG_YES));
    return 1;
  }
  return 0;
}

/* OUT = keccak256(A . B), or keccak256(A) when B is NULL. */
static void word_hash(Word out, const Word a, const Word b) {
  unsigned char data[64];
  memcpy(data, a, 32);
  if (b != NULL) memcpy(data + 32, b, 32);
  keccak256(data, b != NULL ? 64u : 32u, out);
}

/* OUT = BASE + N modulo 2^256. */
static void word_add(Word out, const Word base, uint64_t n) {
  unsigned carry = 0;
  for (int i = 31; i >= 0; i--) {
    unsigned sum = base[i] + (unsigned)(n & 0xffu) + carry;
    out[i] = (unsigned char)sum;
    carry = sum >> 8;
    n >>= 8;
  }
}

/* The (slot, value) word pairs of the start state. AT is NULL in the first
   walk, which only counts the pairs. */
typedef struct {
  unsigned char (*at)[64];
  size_t count;
} Pairs;

static void pair_add(Pairs *p, const Word slot, const Word value) {
  if (p->at != NULL) {
    memcpy(p->at[p->count], slot, 32);
    memcpy(p->at[p->count] + 32, value, 32);
  }
  p->count++;
}

/* Adds the pairs of field I of FORM with the value V. Returns 1, or 0 when V
   is not a closed value of FORM. */
static int lower_field(Pairs *p, uint64_t i, Form form, const Value *v) {
  Word slot, value, at, key;
  word_u64(slot, i);
  if (form == FORM_WORD) {
    if (!word_of(v, value)) return 0;
    pair_add(p, slot, value);
    return 1;
  }
  if (form == FORM_LIST) {
    Word base;
    uint64_t n = 0;
    word_hash(base, slot, NULL);
    for (; val_is(v, OP_CONS) && v->argc == 2; v = v->args[1], n++) {
      if (!word_of(v->args[0], value)) return 0;
      word_add(at, base, n);
      pair_add(p, at, value);
    }
    if (!val_is(v, OP_NIL)) return 0;
    word_u64(value, n);
    pair_add(p, slot, value);
    return 1;
  }
  if (!val_is(v, OP_KMAP_OF) || v->argc % 2u != 0) return 0;
  for (uint32_t j = 0; j < v->argc; j += 2u) {
    if (!word_of(v->args[j], key) || !word_of(v->args[j + 1u], value)) return 0;
    word_hash(at, key, slot);
    pair_add(p, at, value);
  }
  return 1;
}

static const Value *field_type(Machine *m, const CtorInfo *ci, uint32_t i) {
  return eval_core(m, NULL, ci->fields[i].type);
}

/* Walks the fields of STATE into P. Returns 1, or 0 after a diagnostic. */
static int lower_state(Machine *m, const CtorInfo *ci, const Value *state, Pairs *p) {
  for (uint32_t i = 0; i < ci->field_count; i++)
    if (!lower_field(p, i, form_of(field_type(m, ci, i)), state->args[i]))
      return diag_fail(m->diag, "REFUSE_LOWER", ci->fields[i].name,
                       "the init value of the field is not a closed value (a trap)");
  return 1;
}

static int build_status(Machine *m, EvmBuild result) {
  switch (result) {
    case EVM_BUILD_OK: return 0;
    case EVM_BUILD_SIZE:
      return diag_fail(m->diag, "EVM_SIZE", NULL, "the contract code is longer than the EIP-3860 or EIP-170 limit") + 1;
    case EVM_BUILD_OOM: return diag_fail(m->diag, "OOM", NULL, "out of memory") + 1;
    case EVM_BUILD_WRITE: return diag_fail(m->diag, "IO_WRITE", NULL, "the code buffer write failed") + 2;
  }
  return 1;
}

int lower_build(Machine *m, TargetPart part, FILE *out) {
  uint32_t init = m->def_count;
  for (uint32_t i = 0; i < m->def_count; i++)
    if (strcmp(m->defs[i].name, "init") == 0) init = i;
  m->def = "init";
  m->fuel = EVAL_FUEL_STEPS;
  if (!m->has_state || init == m->def_count)
    return diag_fail(m->diag, "REFUSE_LOWER", NULL, "a build needs a state and def init : State") + 1;
  const Value *type = m->defs[init].type;
  if (type != NULL && type->kind == VAL_PI)
    return diag_fail(m->diag, "REFUSE_LOWER", "init", "init has arguments; K4a does not read constructor arguments") + 1;
  if (!val_is(type, OP_FAMILY) || type->inst != m->state_family)
    return diag_fail(m->diag, "REFUSE_LOWER", "init", "the type of init is not the state") + 1;
  const CtorInfo *ci = &m->ctors[m->families[m->state_family].first_ctor];
  for (uint32_t i = 0; i < ci->field_count; i++) {
    const Value *t = field_type(m, ci, i);
    if (t == NULL) return 1;
    if (form_of(t) == FORM_NONE)
      return diag_fail(m->diag, "REFUSE_LOWER", ci->fields[i].name,
                       "K4a stores only words (Nat, Flag, U256, Addr), Lists of words and Maps of words; the field is %s",
                       form_name(t)) + 1;
  }
  const Value *state = def_value(m, init);
  if (state == NULL) return 1;
  if (!val_is(state, OP_CTOR) || state->argc != ci->field_count)
    return diag_fail(m->diag, "REFUSE_LOWER", "init", "the value of init is not a closed state") + 1;
  Pairs pairs = {NULL, 0};
  if (!lower_state(m, ci, state, &pairs)) return 1;
  pairs.at = pairs.count > 0 ? arena_alloc(m->arena, pairs.count * 64u) : NULL;
  if (pairs.count > 0 && pairs.at == NULL) return diag_fail(m->diag, "OOM", NULL, "out of memory") + 1;
  pairs.count = 0;
  if (!lower_state(m, ci, state, &pairs)) return 1;
  return build_status(m, evm_build((const unsigned char (*)[64])pairs.at, pairs.count, part, out));
}
