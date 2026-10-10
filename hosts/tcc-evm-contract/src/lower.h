/* The lowering of a checked program to EVM code (slice K4a). The file is
   apart from src/evm.c, because build/asmtool links src/evm.c without the
   front end. */
#ifndef LANG_LOWER_H
#define LANG_LOWER_H

#include <stdio.h>

#include "front/core.h"
#include "target.h"

/* Writes PART of the contract code of the checked program M on OUT as one
   hex line. Returns 0, 1 after a refusal (REFUSE_LOWER, EVM_SIZE, OOM) or 2
   after IO_WRITE; each failure writes a diagnostic on M->diag. */
int lower_build(Machine *m, TargetPart part, FILE *out);

/* Writes the `langc abi` lines of the checked program M on OUT (C-K4-16):
   one for each entry and view in source order, then one for each event in
   declaration order. Each line is the selector (4 bytes, or the 32 bytes of
   topic 0 for an event), the signature and the kind. Returns 0, or 1 or 2
   as lower_build. */
int lower_abi(Machine *m, FILE *out);

#endif
