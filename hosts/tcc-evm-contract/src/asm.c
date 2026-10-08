/* The EVM assembler. See asm.h. */
#include "asm.h"

#include <string.h>

void asm_init(Asm *a) { memset(a, 0, sizeof *a); }

Label asm_label(Asm *a) {
  a->full = a->full || a->labels >= ASM_LABELS;
  if (a->full) return 0;
  a->labels++;
  return (Label)(a->labels - 1);
}

void asm_put(Asm *a, unsigned value) {
  a->full = a->full || a->size >= ASM_CAPACITY;
  if (a->full) return;
  a->code[a->size] = (unsigned char)value;
  a->size++;
}

void asm_op(Asm *a, Op code) { asm_put(a, (unsigned)code); }

/* The shortest PUSH of a big-endian word: PUSH0 for zero. */
void asm_push_word(Asm *a, const unsigned char word[32]) {
  size_t lead = 0;
  while (lead < 32 && word[lead] == 0) lead++;
  asm_put(a, (unsigned)EVM_OP_PUSH0 + (unsigned)(32 - lead));
  for (size_t i = lead; i < 32; i++) asm_put(a, word[i]);
}

void asm_push(Asm *a, uint64_t value) {
  unsigned char word[32] = {0};
  for (size_t i = 0; i < 8; i++) word[31 - i] = (unsigned char)(value >> (8 * i));
  asm_push_word(a, word);
}

void asm_push_label(Asm *a, Label label) {
  a->full = a->full || a->sites >= ASM_FIXUPS;
  if (a->full) return;
  asm_op(a, EVM_OP_PUSH2);
  a->site[a->sites] = a->size;
  a->target[a->sites] = label;
  a->sites++;
  asm_put(a, 0);
  asm_put(a, 0);
}

void asm_bind(Asm *a, Label label) {
  a->stray = a->stray || label >= a->labels;
  if (a->stray) return;
  a->at[label] = a->size;
  a->bound[label] = 1;
}

void asm_jumpdest(Asm *a, Label label) {
  asm_bind(a, label);
  asm_op(a, EVM_OP_JUMPDEST);
}

void asm_jump(Asm *a, Label label) {
  asm_push_label(a, label);
  asm_op(a, EVM_OP_JUMP);
}

void asm_jump_if(Asm *a, Label label) {
  asm_push_label(a, label);
  asm_op(a, EVM_OP_JUMPI);
}

static int resolve(Asm *a) {
  int ok = !a->stray;
  for (size_t i = 0; ok && i < a->sites; i++) {
    Label label = a->target[i];
    ok = label < a->labels && a->bound[label] && a->at[label] <= 0xffff;
    size_t at = ok ? a->at[label] : 0;
    a->code[a->site[i]] = (unsigned char)(at >> 8);
    a->code[a->site[i] + 1] = (unsigned char)at;
  }
  return ok;
}

int asm_finish(Asm *a, FILE *err) {
  if (a->full) {
    fprintf(err, "langc: EVM_SIZE: -: the code needs more than %d bytes, %d labels or %d jumps\n",
            ASM_CAPACITY, ASM_LABELS, ASM_FIXUPS);
    return 0;
  }
  if (!resolve(a)) {
    fputs("langc: EVM_INTERNAL: -: a jump label is unknown or unbound\n", err);
    return 0;
  }
  return 1;
}

int asm_creation(Asm *a, const Asm *runtime, FILE *err) {
  if (runtime->size > ASM_RUNTIME_MAX) {
    fprintf(err, "langc: EVM_SIZE: -: the runtime has %zu bytes, the limit is %d\n", runtime->size, ASM_RUNTIME_MAX);
    return 0;
  }
  asm_init(a);
  Label revert = asm_label(a);
  Label body = asm_label(a);
  asm_op(a, EVM_OP_CALLVALUE);
  asm_jump_if(a, revert);
  asm_push(a, runtime->size);
  asm_op(a, EVM_OP_DUP1);
  asm_push_label(a, body);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_CODECOPY);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_RETURN);
  asm_jumpdest(a, revert);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_REVERT);
  asm_bind(a, body);
  for (size_t i = 0; i < runtime->size; i++) asm_put(a, runtime->code[i]);
  return asm_finish(a, err);
}

int asm_write_hex(const Asm *a, FILE *out, FILE *err) {
  for (size_t i = 0; i < a->size; i++) fprintf(out, "%02x", a->code[i]);
  fputc('\n', out);
  if (fflush(out) == 0 && !ferror(out)) return 1;
  fputs("langc: IO: -: cannot write the bytecode\n", err);
  return 0;
}
