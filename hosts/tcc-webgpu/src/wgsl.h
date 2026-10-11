/* The WGSL target. The tcc-webgpu kit only.

   `langc build PROG --wgsl OUT.wgsl` checks the program and writes one WGSL
   module with one compute shader for each entry. An entry is a program
   definition whose type is a function of 1 to 7 tiles and U32 values to a
   tile, with closed shapes (D8). The other definitions give no WGSL, so a
   program without entries gives only the header comment.

   The writer applies the entry to neutral parameters in the symbolic mode of
   the evaluator (D7): tabulate, sumTo and foldTo stay stuck. It writes the
   cell (i, j) of the result as one WGSL expression; `at` of a tabulate fuses
   (D11), and sumTo and foldTo become loops (D10). A value that has no WGSL
   form is refused with WGSL_FORM.

   Every build makes the module first (D14): the JSON manifest (D12) and the
   JS kernels (D13) describe it, so a refused entry refuses the build for
   every target. */
#ifndef LANG_WGSL_H
#define LANG_WGSL_H

#include <stddef.h>
#include <stdint.h>

#include "front/core.h"

/* The module: the WGSL text, the manifest of its entries (the JSON array of
   the "kernels" member, D12; empty when COUNT is 0) and COUNT, the number of
   entries. */
typedef struct {
  const char *text;
  size_t len;
  const char *manifest;
  size_t manifest_len;
  uint32_t count;
} WgslModule;

/* Builds the module for the checked program M in the arena of M. Returns 1
   and fills OUT, or 0 after a diagnostic (then no module). */
int wgsl_module(Machine *m, WgslModule *out);

#endif
