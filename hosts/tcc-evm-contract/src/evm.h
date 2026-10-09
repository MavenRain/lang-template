/* The contract code of slice K4a, apart from target.h (a SHARED file). The
   header has no front-end type and no Op, so src/lower.c (front/core.h) and
   src/evm.c (asm.h) both include it. */
#ifndef LANG_EVM_H
#define LANG_EVM_H

#include <stddef.h>
#include <stdio.h>

#include "target.h"

/* EVM_BUILD_OOM: calloc or the fopen of the /dev/null sink failed.
   EVM_BUILD_IR: an entry has an IR form that the EVM back end does not
   lower (a heap block, a loop, a switch or a RETURN). */
typedef enum { EVM_BUILD_OK, EVM_BUILD_SIZE, EVM_BUILD_OOM, EVM_BUILD_WRITE, EVM_BUILD_IR } EvmBuild;

/* Lowers each entry of PROGRAM to EVM code (C-K4b-1, C-K4b-2). Until the
   dispatcher lands (K4b2), the entry code is assembled and checked but not
   written. Writes PART of the contract code on OUT as one hex line. PAIRS holds COUNT
   storage words: bytes 0 to 31 the slot, bytes 32 to 63 the value, both
   big-endian. The creation code stores each pair with a value that is not
   zero (PUSH value, PUSH slot, SSTORE), then returns the runtime code. The
   K4a runtime code is PUSH0 PUSH0 REVERT. TARGET_PART_RUNTIME writes only
   that runtime, without assembling or applying size limits to creation code. */
EvmBuild evm_build(const IrProgram *prog, const unsigned char (*pairs)[64], size_t count, TargetPart part,
                   FILE *out);

#endif
