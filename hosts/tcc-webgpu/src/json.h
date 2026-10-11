/* The JSON target. The tcc-json kit only (not shared with tcc-wasm).

   `langc build` checks the program, evaluates each instance and gives one
   JSON document: {"LANG":1,"instances":[{"name","type","value"}]}. An
   instance is a PROGRAM definition whose type is data: not a function type,
   not a universe (types and families) and not an equality (proofs). Values
   carry no types, so the printer walks the type and the value together. */
#ifndef LANG_JSON_H
#define LANG_JSON_H

#include <stddef.h>

#include "wgsl.h"

#define JSON_DEPTH_LIMIT 2000u

/* The first key of the document: the language name. */
extern const char json_lang_name[];

/* Builds the document for the checked program M in the arena of M. Returns
   1 and sets TEXT and LEN, or 0 after a diagnostic (then no document). */
/* W is the WGSL module of M (D14). When it has entries, the document gets
   the "kernels" member: the manifest of the entries (D12). */
int json_document(Machine *m, const WgslModule *w, const char **text, size_t *len);
/* 1 when D is an instance (the JS target writes the same document). */
int json_is_instance(const DefInfo *d);

#endif
