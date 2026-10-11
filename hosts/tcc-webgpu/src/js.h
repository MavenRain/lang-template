/* The JS target. The tcc-js kit only.

   `langc build PROG --js OUT.js` checks the program and writes one ES module.
   The module evaluates the program in JS: Nat is a BigInt (exact u64; an
   overflow gives a trap value, as in the C evaluator), Flag a boolean, Str a
   string, and each other value a small frozen object with a kind (see the
   runtime in js.c). Types have no runtime content (null); a proof is REFL.
   The module exports `encode(v)` (the JSON text of a value, the same bytes as
   the JSON target), `document` (the instance document of the JSON target,
   evaluated in JS) and `entries` (the definitions that `langc eval` runs, as
   JS functions). With SELFTEST, top-level code evaluates both sides of each
   Eq-typed definition and throws on a mismatch. */
#ifndef LANG_JS_H
#define LANG_JS_H

#include <stddef.h>

#include "wgsl.h"

/* Builds the module for the checked program M in the arena of M. Returns 1
   and sets TEXT and LEN, or 0 after a diagnostic (then no module). */
/* W is the WGSL module of M (D14). The module exports `wgsl`, its text, and
   `kernels`, one function for each entry (D13). With SELFTEST it also checks
   the input checks of each kernel. With GPU (only with SELFTEST) it also runs
   each kernel on a GPU with the inputs of each test whose left side applies
   that kernel, and compares the tile with the C value (K-C7). */
int js_module(Machine *m, int selftest, int gpu, const WgslModule *w, const char **text, size_t *len);

#endif
