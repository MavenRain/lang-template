/* The EVM back end of langc: an assembler (a byte emitter, the shortest
 * PUSH of each constant, PUSH2 jump labels that a second pass resolves) and
 * the target interface, lang_evm_write. */
#ifndef LANG_EVM_H
#define LANG_EVM_H
#include <stddef.h>
#include <stdio.h>

enum {
  EVM_CAPACITY = 49152,    /* bytes of one code buffer: EVM_INITCODE_MAX */
  EVM_FIXUPS = 512,        /* PUSH2 label sites of one code buffer */
  EVM_LABELS = 64,         /* labels of one code buffer */
  EVM_RUNTIME_MAX = 24576, /* EIP-170 */
  EVM_INITCODE_MAX = 49152 /* EIP-3860: the creation code and the constructor arguments */
};

typedef enum {
  OP_STOP = 0x00, OP_ADD = 0x01, OP_MUL = 0x02, OP_SUB = 0x03, OP_LT = 0x10,
  OP_GT = 0x11, OP_EQ = 0x14, OP_ISZERO = 0x15, OP_AND = 0x16, OP_SHR = 0x1c, OP_SHA3 = 0x20,
  OP_CALLER = 0x33, OP_CALLVALUE = 0x34, OP_CALLDATALOAD = 0x35,
  OP_CALLDATASIZE = 0x36, OP_CODESIZE = 0x38, OP_CODECOPY = 0x39,
  OP_TIMESTAMP = 0x42,   OP_POP = 0x50, OP_MLOAD = 0x51, OP_MSTORE = 0x52, OP_SLOAD = 0x54,
  OP_SSTORE = 0x55, OP_JUMP = 0x56, OP_JUMPI = 0x57, OP_JUMPDEST = 0x5b,
  OP_PUSH0 = 0x5f, OP_PUSH1 = 0x60, OP_PUSH2 = 0x61, OP_PUSH4 = 0x63,
  OP_DUP1 = 0x80, OP_DUP2 = 0x81, OP_DUP3 = 0x82, OP_DUP4 = 0x83, OP_DUP5 = 0x84,
  OP_SWAP1 = 0x90, OP_SWAP2 = 0x91, OP_SWAP3 = 0x92, OP_SWAP4 = 0x93, OP_LOG1 = 0xa1, OP_LOG2 = 0xa2,
  OP_RETURN = 0xf3, OP_REVERT = 0xfd
} EvmOp;

/* A jump label. The assembler owns the first three; a target numbers its own
 * labels from EVM_LABEL_TARGET to EVM_LABELS - 1. */
typedef size_t EvmLabel;
enum { EVM_LABEL_REVERT = 0, EVM_LABEL_RUNTIME = 1, EVM_LABEL_END = 2, EVM_LABEL_TARGET = 3 };

/* One code buffer. Only the evm_ functions read or write the fields. Pass 1
 * appends code and records each PUSH2 label site; evm_finish (pass 2)
 * writes the label offsets into those sites. */
typedef struct {
  unsigned char code[EVM_CAPACITY];
  size_t size;
  size_t at[EVM_LABELS];
  int bound[EVM_LABELS];
  size_t site[EVM_FIXUPS];
  EvmLabel target[EVM_FIXUPS];
  size_t sites;
  int full;    /* the code or the sites overflowed */
  int broken;  /* a label was EVM_LABELS or more */
} EvmAsm;

void evm_init(EvmAsm *a);
void evm_byte(EvmAsm *a, unsigned value);
void evm_op(EvmAsm *a, EvmOp code);
/* The shortest PUSH of a big-endian word: PUSH0 for zero. */
void evm_push_word(EvmAsm *a, const unsigned char word[32]);
void evm_push(EvmAsm *a, unsigned long value);
void evm_push_label(EvmAsm *a, EvmLabel label);
void evm_bind(EvmAsm *a, EvmLabel label);
void evm_jumpdest(EvmAsm *a, EvmLabel label);
void evm_jump(EvmAsm *a, EvmLabel label);
void evm_jump_if(EvmAsm *a, EvmLabel label);
void evm_revert_if(EvmAsm *a);
/* Calldata word j of the arguments, at byte 4 + 32 j. */
void evm_argument(EvmAsm *a, unsigned j);
/* The selector of the call on the stack; reverts on calldata shorter than 4 bytes. */
void evm_dispatch_head(EvmAsm *a);
/* With the selector on the stack: jump to LABEL when it is the selector of
 * SIGNATURE, such as "verify(bytes32,uint256)". */
void evm_dispatch(EvmAsm *a, const char *signature, EvmLabel label);
/* An entry of WORDS argument words: pops the selector, reverts on a call
 * value or on short calldata. */
void evm_entry(EvmAsm *a, EvmLabel label, unsigned words);
void evm_return_top(EvmAsm *a);
/* Binds EVM_LABEL_REVERT: REVERT with empty output. */
void evm_revert_block(EvmAsm *a);
/* The end of the creation code, after the constructor that the target wrote
 * into A: copies BODY to memory and returns it. Binds EVM_LABEL_END after
 * BODY, so a PUSH2 of EVM_LABEL_END is the size of the creation code, where
 * the constructor arguments start. */
void evm_creation(EvmAsm *a, const EvmAsm *body);
/* Pass 2. Returns 0, or nonzero after writing "langc: EVM_<CODE>: message\n" to err. */
int evm_finish(EvmAsm *a, FILE *err);
/* Lowercase hex, no 0x, one trailing newline. */
int evm_write_hex(const EvmAsm *a, FILE *out, FILE *err);

/* ---- the target interface ---- */

typedef enum { LANG_PART_CREATION, LANG_PART_RUNTIME } AnchorPart;

/* A row of the outcome table: the fate of one tally and its policies. */
typedef struct {
  unsigned fate;  /* 0 none, 1 one, 2 two */
  size_t p;       /* fate 1 or 2: a policy number, else 0 */
  size_t q;       /* fate 2: a policy number, else 0 */
} AnchorContractRow;

/* The fields of a policy that a guard reads: hashDom and clock have one
 * value (O4, O1) and forkFreeze is forced (O2). Only dispute reads the
 * window (O7). */
typedef struct {
  int allow;                  /* 1 allow, 0 deny */
  unsigned long long schema;
  unsigned long long window;  /* dispute(h, t, note) runs while TIMESTAMP < t + window */
} AnchorContractPolicy;

/* The anchor contract of SPEC section 7 with its outcome table. */
typedef struct {
  unsigned members;   /* n >= 1, one address per member position (O5) */
  size_t candidates;  /* K >= 1, so a ballot is 0 to K - 1 */
  size_t nrows;       /* R = C(n + K - 1, K - 1), one row for each tally, in the order of langc table */
  size_t constitutions; /* C >= 1 (O3): rule, then the constitutions of amendments */
  const AnchorContractRow *rows; /* C R rows: the R rows of constitution c start at rows + c R */
  size_t npolicies;   /* the candidates, then each other policy of an outcome */
  const AnchorContractPolicy *policies;
  const unsigned char *amend; /* C > 1: one mask for each policy, bit k set when amendTo p k is yes; else NULL */
} AnchorContract;

/* Writes lowercase hex, no 0x, one trailing newline. Returns 0, or nonzero
 * after writing "langc: EVM_<CODE>: message\n" to err. */
int lang_evm_write(const AnchorContract *contract, AnchorPart part, FILE *out, FILE *err);

/* Writes the entries of the contract in the text form of SPEC section 7:
 * the constructor, then one line for each entry in the order of the
 * dispatch. Returns 0, or nonzero after an EVM_ message to err. */
int lang_abi_write(const AnchorContract *contract, FILE *out, FILE *err);
#endif
