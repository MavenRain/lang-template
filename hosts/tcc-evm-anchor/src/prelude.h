/* The prelude, embedded in langc by gen/embed.c (build/domain.c), so
 * langc reads no prelude file at run time. */
#ifndef LANG_PRELUDE_H
#define LANG_PRELUDE_H
#include <stddef.h>

extern const char lang_prelude_name[];        /* domain/domain.lang */
extern const unsigned char lang_prelude_text[]; /* the bytes, then one NUL */
extern const size_t lang_prelude_size;        /* the bytes without the NUL */
#endif
