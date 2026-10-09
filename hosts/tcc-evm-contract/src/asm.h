/* The EVM assembler of the tcc-evm-contract kit. It comes from the escrowc
   back end (escrow-lang src/evm.c): two passes, jump labels as PUSH2 sites,
   the shortest PUSH of a word. The labels are numbers from asm_label, not a
   fixed enum. A full table sets `full`; asm_finish then refuses the code. */
#ifndef LANG_ASM_H
#define LANG_ASM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
  ASM_CAPACITY = 49152,    /* EIP-3860: the limit of the creation code */
  ASM_RUNTIME_MAX = 24576, /* EIP-170: the limit of the runtime code */
  ASM_LABELS = 1024,
  ASM_FIXUPS = 4096
};

typedef enum {
  EVM_OP_STOP = 0x00, EVM_OP_ADD = 0x01, EVM_OP_MUL = 0x02, EVM_OP_SUB = 0x03, EVM_OP_DIV = 0x04,
  EVM_OP_LT = 0x10, EVM_OP_GT = 0x11, EVM_OP_EQ = 0x14, EVM_OP_ISZERO = 0x15, EVM_OP_AND = 0x16,
  EVM_OP_OR = 0x17, EVM_OP_NOT = 0x19, EVM_OP_SHL = 0x1b, EVM_OP_SHR = 0x1c, EVM_OP_SHA3 = 0x20,
  EVM_OP_CALLER = 0x33, EVM_OP_CALLVALUE = 0x34, EVM_OP_CALLDATALOAD = 0x35,
  EVM_OP_CALLDATASIZE = 0x36, EVM_OP_CODECOPY = 0x39, EVM_OP_RETURNDATASIZE = 0x3d,
  EVM_OP_RETURNDATACOPY = 0x3e, EVM_OP_TIMESTAMP = 0x42, EVM_OP_POP = 0x50,
  EVM_OP_MLOAD = 0x51, EVM_OP_MSTORE = 0x52, EVM_OP_SLOAD = 0x54, EVM_OP_SSTORE = 0x55,
  EVM_OP_JUMP = 0x56, EVM_OP_JUMPI = 0x57, EVM_OP_GAS = 0x5a, EVM_OP_JUMPDEST = 0x5b,
  EVM_OP_PUSH0 = 0x5f, EVM_OP_PUSH1 = 0x60, EVM_OP_PUSH2 = 0x61, EVM_OP_PUSH4 = 0x63,
  EVM_OP_PUSH32 = 0x7f, EVM_OP_DUP1 = 0x80, EVM_OP_DUP2 = 0x81, EVM_OP_SWAP1 = 0x90,
  EVM_OP_SWAP2 = 0x91, EVM_OP_LOG0 = 0xa0, EVM_OP_LOG1 = 0xa1, EVM_OP_LOG2 = 0xa2,
  EVM_OP_LOG3 = 0xa3, EVM_OP_LOG4 = 0xa4, EVM_OP_CALL = 0xf1, EVM_OP_RETURN = 0xf3,
  EVM_OP_REVERT = 0xfd
} Op;

typedef unsigned Label;

/* Pass 1 appends code and records each PUSH2 label site. Pass 2
   (asm_finish) writes the label offsets into those sites. */
typedef struct {
  unsigned char code[ASM_CAPACITY];
  size_t size;
  size_t at[ASM_LABELS];
  int bound[ASM_LABELS];
  size_t labels;
  size_t site[ASM_FIXUPS];
  Label target[ASM_FIXUPS];
  size_t sites;
  int full;  /* a table is full */
  int stray; /* asm_bind got a label that asm_label did not give */
} Asm;

void asm_init(Asm *a);
Label asm_label(Asm *a);
void asm_put(Asm *a, unsigned value);
void asm_op(Asm *a, Op code);
void asm_push_word(Asm *a, const unsigned char word[32]);
void asm_push(Asm *a, uint64_t value);
void asm_push_label(Asm *a, Label label);
void asm_bind(Asm *a, Label label);
void asm_jumpdest(Asm *a, Label label);
void asm_jump(Asm *a, Label label);
void asm_jump_if(Asm *a, Label label);

/* Pass 2. Returns 1, or 0 after a message on ERR. */
int asm_finish(Asm *a, FILE *err);

/* A writes the creation code of RUNTIME (which passed asm_finish): it
   reverts on a call value, copies RUNTIME to memory and returns it.
   Returns 1, or 0 after a message on ERR. */
int asm_creation(Asm *a, const Asm *runtime, FILE *err);

/* As asm_creation, but the creation code first runs STORE, code with no
   labels (slice K4a: the SSTOREs of the start state). STORE can be NULL. */
int asm_creation_store(Asm *a, const Asm *store, const Asm *runtime, FILE *err);

/* Lowercase hex, no 0x, one newline. Returns 1, or 0 after a message on ERR. */
int asm_write_hex(const Asm *a, FILE *out, FILE *err);

#endif
