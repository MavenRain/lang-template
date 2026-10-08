/* From escrowc (escrow-lang src/keccak.h); the names are changed. */
#ifndef LANG_KECCAK_H
#define LANG_KECCAK_H
#include <stddef.h>
/* Keccak-256 as the EVM uses it (pad 0x01, not the SHA3-256 pad 0x06). */
void keccak256(const unsigned char *data, size_t size, unsigned char digest[32]);
#endif
