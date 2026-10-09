/* The slot tool of test/build.sh (slice K4a): it prints keccak256 of the
   bytes in HEX with the kit keccak, so the test computes the List and Map
   slots. */
#include <stdio.h>
#include <string.h>

#include "keccak.h"

enum { SLOT_INPUT_MAX = 64 };

static int nibble(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

int main(int argc, char **argv) {
  unsigned char data[SLOT_INPUT_MAX];
  size_t len = argc == 2 ? strlen(argv[1]) : 1u;
  if (len % 2u != 0 || len / 2u > SLOT_INPUT_MAX) {
    fputs("usage: slottool HEX (an even count of lowercase hex digits, at most 128)\n", stderr);
    return 2;
  }
  for (size_t i = 0; i < len / 2u; i++) {
    int high = nibble(argv[1][2u * i]);
    int low = nibble(argv[1][2u * i + 1u]);
    if (high < 0 || low < 0) {
      fputs("slottool: HEX has a digit that is not lowercase hex\n", stderr);
      return 2;
    }
    data[i] = (unsigned char)(high * 16 + low);
  }
  unsigned char digest[32];
  keccak256(data, len / 2u, digest);
  for (int i = 0; i < 32; i++) printf("%02x", digest[i]);
  putchar('\n');
  return 0;
}
