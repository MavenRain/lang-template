/* Test driver of the EVM back end, built by make as build/evmtool:
 *   evmtool creation|runtime N K  the contract of N members and K candidates
 *                                 (lang_evm_write)
 *                                 with C(N + K - 1, K - 1) none rows and K
 *                                 deny policies of schema 0
 *   evmtool keccak TEXT           keccak256 of the bytes of TEXT
 *   evmtool slot ADDR             keccak256 of the word of ADDR (40 hex
 *                                 digits), its member slot
 * Exit 0 ok, 1 refused by the back end, 2 usage. */
#include "../src/evm.h"
#include "../src/keccak.h"
#include <stdlib.h>
#include <string.h>

enum { TOOL_DIGITS = 9, TOOL_ROWS = 65536 };

static int usage(void) {
  fputs("usage: evmtool creation|runtime N K | evmtool keccak TEXT | evmtool slot ADDR\n", stderr);
  return 2;
}

/* A decimal number of at most TOOL_DIGITS digits, or -1. */
static long number(const char *text) {
  size_t size = strlen(text);
  int ok = size >= 1 && size <= TOOL_DIGITS && strspn(text, "0123456789") == size;
  return ok ? strtol(text, NULL, 10) : -1;
}

static int keccak(const unsigned char *bytes, size_t size) {
  unsigned char digest[32];
  lang_keccak256(bytes, size, digest);
  for (size_t i = 0; i < 32; i++)
    printf("%02x", digest[i]);
  putchar('\n');
  return 0;
}

static int slot(const char *text) {
  if (strlen(text) != 40 || strspn(text, "0123456789abcdef") != 40)
    return usage();
  unsigned char word[32] = {0};
  for (size_t i = 0; i < 20; i++) {
    unsigned byte = 0;
    sscanf(text + 2 * i, "%2x", &byte);
    word[12 + i] = (unsigned char)byte;
  }
  return keccak(word, sizeof word);
}

/* C(N + K - 1, K - 1), at most TOOL_ROWS: more rows are over EIP-170. */
static size_t tally_rows(unsigned long long n, unsigned long long k) {
  unsigned long long r = 1;
  if (n == 0)
    return 1;
  for (unsigned long long i = 1; i < k && r <= TOOL_ROWS; i++)
    r = r * (n + i) / i;
  return r > TOOL_ROWS ? TOOL_ROWS : (size_t)r;
}

static int part_of(const char *text, AnchorPart *part) {
  *part = strcmp(text, "creation") == 0 ? LANG_PART_CREATION : LANG_PART_RUNTIME;
  return strcmp(text, "creation") == 0 || strcmp(text, "runtime") == 0;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "keccak") == 0)
    return keccak((const unsigned char *)argv[2], strlen(argv[2]));
  if (argc == 3 && strcmp(argv[1], "slot") == 0)
    return slot(argv[2]);
  AnchorPart part;
  if (argc != 4 || !part_of(argv[1], &part))
    return usage();
  long members = number(argv[2]);
  long candidates = number(argv[3]);
  if (members < 0 || candidates < 0)
    return usage();
  size_t nrows = tally_rows((unsigned long long)members, (unsigned long long)candidates);
  size_t npolicies = (size_t)candidates < TOOL_ROWS ? (size_t)candidates : TOOL_ROWS;
  AnchorContractRow *rows = calloc(nrows + 1, sizeof *rows);
  AnchorContractPolicy *policies = calloc(npolicies + 1, sizeof *policies);
  AnchorContract contract = { (unsigned)members, (size_t)candidates, nrows, 1, rows, npolicies, policies, NULL };
  int status = lang_evm_write(&contract, part, stdout, stderr) == 0 ? 0 : 1;
  free(rows);
  free(policies);
  return status;
}
