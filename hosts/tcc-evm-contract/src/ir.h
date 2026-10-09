/* The first-order IR between the front end and a target. SHARED by the tcc
   kits.

   The IR word is the word of the target: Wasm 64 bits, EVM 256 bits (in
   this kit, the EVM word). IrScalar keeps the range of each value: a Nat is
   below 2^64, a Flag is 0 or 1, an Addr is below 2^160. Locals are
   numbered from 0. The parameters of an entry are locals 0 to
   param_count - 1. A heap block is a run of words. A target resets its bump
   allocator at each call.

   Traps (C-K4b-7): the trapping ops (ADD64, MUL64, ADD_TRAP, MUL_TRAP, DIV)
   stop the call with TRAP when the result is out of range. The lowering puts
   an expression only in a strict position (a stored field, a map key or
   value, an IF condition, an operand of one of these), and the branches of
   an IF are statements, thus an eager trap gives the evaluator result. The
   Wasm lowering of the H3 ruling uses the lazy ops ADD_CARRY and MUL_CARRY
   instead. */
#ifndef LANG_IR_H
#define LANG_IR_H

#include <stddef.h>
#include <stdint.h>

/* The ABI type of a parameter or a result. U256 and ADDR are for the EVM
   kit only (slice K4b): there a word is 32 bytes. */
typedef enum {
  IR_SCALAR_NAT,  /* uint64 */
  IR_SCALAR_FLAG, /* bool */
  IR_SCALAR_U256, /* uint256 */
  IR_SCALAR_ADDR  /* address */
} IrScalar;

typedef enum {
  IR_OP_ADD,       /* the sum modulo 2^64 */
  IR_OP_SUB,       /* truncates at 0 */
  IR_OP_MUL,       /* the product modulo 2^64 */
  IR_OP_EQ,        /* 1 when equal, else 0 */
  IR_OP_LE,        /* 1 when left <= right, else 0 */
  IR_OP_ADD_CARRY, /* 1 when the sum is larger than 2^64 - 1, else 0 */
  IR_OP_MUL_CARRY, /* 1 when the product is larger than 2^64 - 1, else 0 */
  IR_OP_OR,        /* bitwise or (the lowering gives it flags only) */
  IR_OP_LT,        /* 1 when left < right, else 0 */
  IR_OP_DIV,       /* the floor of left / right; traps when right is 0 */
  IR_OP_ADD64,     /* the sum; traps when it is 2^64 or more (Nat) */
  IR_OP_MUL64,     /* the product; traps when it is 2^64 or more (Nat) */
  IR_OP_ADD_TRAP,  /* the sum; traps on a word overflow (U256) */
  IR_OP_MUL_TRAP,  /* the product; traps on a word overflow (U256) */
  IR_OP_MIN,       /* the smaller operand */
  IR_OP_KECCAK     /* keccak256(left . right), each a 32-byte word */
} IrOp;

/* The call data that IR_EXPR_ENV reads (C-K4-12). */
typedef enum {
  IR_ENV_NOW,   /* the block time: EVM TIMESTAMP */
  IR_ENV_CALLER /* the sender: EVM CALLER */
} IrEnv;

typedef enum {
  IR_EXPR_CONST,  /* value */
  IR_EXPR_LOCAL,  /* local */
  IR_EXPR_BINARY, /* op left right */
  IR_EXPR_LOAD,   /* word number field of the block at address left */
  IR_EXPR_WORD,   /* the 32-byte big-endian word at word (a U256 or Addr constant) */
  IR_EXPR_ENV,    /* the IrEnv value field */
  IR_EXPR_SLOAD   /* the storage word at slot left (EVM SLOAD) */
} IrExprKind;

typedef struct IrExpr IrExpr;
struct IrExpr {
  IrExprKind kind;
  uint64_t value;
  uint32_t local;
  IrOp op;
  uint32_t field;
  const IrExpr *left;
  const IrExpr *right;
  const unsigned char *word;
};

typedef struct IrStmt IrStmt;

typedef struct {
  const IrStmt *const *items;
  size_t count;
} IrBlock;

typedef enum {
  IR_STMT_SET,    /* local := expr */
  IR_STMT_ALLOC,  /* local := address of a new block; word k := fields[k] */
  IR_STMT_IF,     /* expr != 0: body, else: otherwise */
  IR_STMT_SWITCH, /* run arms[expr]; a value outside 0 to arm_count - 1 traps */
  IR_STMT_REPEAT, /* counter := expr, then run body counter times */
  IR_STMT_WHILE,  /* counter := expr; run body while counter > 0 and local != 0,
                     counter decreasing by 1 at each run; body sets local */
  IR_STMT_TRAP,   /* stop the call: Wasm unreachable, EVM REVERT with Trap() */
  IR_STMT_RETURN, /* return expr */
  IR_STMT_SSTORE, /* storage word at slot expr := value (EVM SSTORE) */
  IR_STMT_REVERT, /* stop the call, undo the stores: EVM REVERT with empty data */
  IR_STMT_STOP    /* stop the call, keep the stores: EVM STOP */
} IrStmtKind;

struct IrStmt {
  IrStmtKind kind;
  uint32_t local;
  uint32_t counter;
  const IrExpr *expr;
  const IrExpr *value;
  const IrExpr *const *fields;
  size_t field_count;
  IrBlock body;
  IrBlock otherwise;
  const IrBlock *arms;
  size_t arm_count;
};

/* One exported entry. The body block ends in RETURN, TRAP, REVERT or STOP on
   each path. An EVM entry has no result: STOP ends it. */
typedef struct {
  const char *name;
  const IrScalar *params;
  size_t param_count;
  IrScalar result;
  uint32_t local_count; /* includes the parameters */
  IrBlock body;
} IrFunc;

typedef struct {
  const IrFunc *funcs;
  size_t func_count;
} IrProgram;

#endif
