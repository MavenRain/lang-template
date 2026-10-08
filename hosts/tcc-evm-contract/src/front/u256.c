/* 256-bit unsigned arithmetic for U256 and Addr (kit slice K2). The
   multiply works on 32-bit halves, so no product needs more than 64 bits. */
#include <stdio.h>

#include "front/u256.h"

static const U256 U256_ZERO = {{0, 0, 0, 0}};

/* Half I of X: half 0 is the low 32 bits of limb 0. */
static uint32_t half(const U256 *x, int i) {
  return (uint32_t)(x->limb[i / 2] >> (32 * (i % 2)));
}

static void join(U256 *out, const uint32_t *h) {
  int i;
  for (i = 0; i < 4; i++)
    out->limb[i] = (uint64_t)h[2 * i] | ((uint64_t)h[2 * i + 1] << 32);
}

void u256_from_u64(U256 *out, uint64_t n) {
  *out = U256_ZERO;
  out->limb[0] = n;
}

int u256_cmp(const U256 *a, const U256 *b) {
  int i;
  for (i = 3; i >= 0; i--) {
    if (a->limb[i] != b->limb[i])
      return a->limb[i] < b->limb[i] ? -1 : 1;
  }
  return 0;
}

int u256_add(U256 *out, const U256 *a, const U256 *b) {
  U256 r;
  uint64_t carry = 0;
  int i;
  for (i = 0; i < 4; i++) {
    uint64_t s = a->limb[i] + carry;
    uint64_t c1 = s < carry;
    r.limb[i] = s + b->limb[i];
    carry = c1 | (r.limb[i] < s);
  }
  *out = r;
  return carry == 0;
}

/* A - B modulo 2^256. */
static void sub_wrap(U256 *out, const U256 *a, const U256 *b) {
  U256 r;
  uint64_t borrow = 0;
  int i;
  for (i = 0; i < 4; i++) {
    uint64_t d = a->limb[i] - b->limb[i];
    uint64_t b1 = a->limb[i] < b->limb[i];
    r.limb[i] = d - borrow;
    borrow = b1 | (d < borrow);
  }
  *out = r;
}

int u256_sub(U256 *out, const U256 *a, const U256 *b) {
  if (u256_cmp(a, b) < 0)
    *out = U256_ZERO;
  else
    sub_wrap(out, a, b);
  return 1;
}

int u256_mul(U256 *out, const U256 *a, const U256 *b) {
  uint32_t r[16] = {0};
  int over = 0;
  int i;
  int j;
  for (i = 0; i < 8; i++) {
    uint64_t x = half(a, i);
    uint64_t carry = 0;
    for (j = 0; j < 8; j++) {
      /* At most (2^32 - 1)^2 + 2 (2^32 - 1) = 2^64 - 1. */
      uint64_t p = x * half(b, j) + r[i + j] + carry;
      r[i + j] = (uint32_t)p;
      carry = p >> 32;
    }
    r[i + 8] = (uint32_t)carry;
  }
  for (i = 8; i < 16; i++)
    over |= r[i] != 0;
  join(out, r);
  return !over;
}

/* X = X * 2 + BIT. The top bit goes out. */
static void shift_in(U256 *x, uint64_t bit) {
  int i;
  for (i = 3; i > 0; i--)
    x->limb[i] = (x->limb[i] << 1) | (x->limb[i - 1] >> 63);
  x->limb[0] = (x->limb[0] << 1) | bit;
}

/* Shift and subtract, one bit of A at a time. When the remainder has its top
   bit set, the shift takes it past 2^256, so it is larger than B. */
int u256_div(U256 *out, const U256 *a, const U256 *b) {
  U256 q = U256_ZERO;
  U256 r = U256_ZERO;
  int i;
  if (u256_cmp(b, &U256_ZERO) == 0)
    return 0;
  for (i = 255; i >= 0; i--) {
    uint64_t top = r.limb[3] >> 63;
    shift_in(&r, (a->limb[i / 64] >> (i % 64)) & 1u);
    shift_in(&q, 0);
    if (top != 0 || u256_cmp(&r, b) >= 0) {
      sub_wrap(&r, &r, b);
      q.limb[0] |= 1u;
    }
  }
  *out = q;
  return 1;
}

int u256_min(U256 *out, const U256 *a, const U256 *b) {
  *out = u256_cmp(a, b) <= 0 ? *a : *b;
  return 1;
}

int u256_scale_add(U256 *x, uint32_t mul, uint32_t add) {
  uint32_t h[8];
  uint64_t carry = add;
  int i;
  for (i = 0; i < 8; i++) {
    uint64_t p = (uint64_t)half(x, i) * mul + carry;
    h[i] = (uint32_t)p;
    carry = p >> 32;
  }
  join(x, h);
  return carry == 0;
}

/* X = X / D. Returns X % D. D is not 0. */
static uint32_t div_small(U256 *x, uint32_t d) {
  uint32_t h[8];
  uint64_t rem = 0;
  int i;
  for (i = 7; i >= 0; i--) {
    uint64_t cur = (rem << 32) | half(x, i);
    h[i] = (uint32_t)(cur / d);
    rem = cur % d;
  }
  join(x, h);
  return (uint32_t)rem;
}

void word_text(uint64_t sort, const U256 *x, char *buf, size_t cap) {
  /* 2^256 - 1 has 78 decimal digits. */
  char text[82];
  size_t at = sizeof text - 1;
  U256 v = *x;
  if (sort == WORD_ADDR) {
    snprintf(buf, cap, "0x%08llx%016llx%016llx", (unsigned long long)(x->limb[2] & 0xffffffffu),
             (unsigned long long)x->limb[1], (unsigned long long)x->limb[0]);
    return;
  }
  text[at] = '\0';
  text[--at] = 'u';
  do {
    text[--at] = (char)('0' + div_small(&v, 10u));
  } while (u256_cmp(&v, &U256_ZERO) != 0);
  snprintf(buf, cap, "%s", text + at);
}
