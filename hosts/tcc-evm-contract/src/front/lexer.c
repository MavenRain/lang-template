#include <string.h>

#include "front/lexer.h"

typedef struct {
  const char *text;
  size_t len;
  size_t pos;
  int line;
  int col;
  Token *out;
  size_t count;
  Diag *diag;
  const char *source;
} Lexer;

static const struct {
  const char *word;
  TokKind kind;
} KEYWORDS[] = {
  {"def", TOK_KW_DEF},
  {"rec", TOK_KW_REC},
  {"fun", TOK_KW_FUN},
  {"family", TOK_KW_FAMILY},
  {"axiom", TOK_KW_AXIOM},
  {"Type", TOK_KW_TYPE},
  {"Sigma", TOK_KW_SIGMA},
  {"state", TOK_KW_STATE},
};

const char *tok_kind_name(TokKind kind) {
  switch (kind) {
    case TOK_EOF: return "end of input";
    case TOK_IDENT: return "a name";
    case TOK_NAT: return "a Nat literal";
    case TOK_WORD: return "a U256 or Addr literal";
    case TOK_LPAREN: return "'('";
    case TOK_RPAREN: return "')'";
    case TOK_COLON: return "':'";
    case TOK_DEFINE: return "':='";
    case TOK_ARROW: return "'->'";
    case TOK_FATARROW: return "'=>'";
    case TOK_STAR: return "'*'";
    case TOK_BAR: return "'|'";
    case TOK_KW_DEF: return "'def'";
    case TOK_KW_REC: return "'rec'";
    case TOK_KW_FUN: return "'fun'";
    case TOK_KW_FAMILY: return "'family'";
    case TOK_KW_AXIOM: return "'axiom'";
    case TOK_KW_TYPE: return "'Type'";
    case TOK_KW_SIGMA: return "'Sigma'";
    case TOK_KW_STATE: return "'state'";
  }
  return "a token";
}

static int is_digit(char c) {
  return c >= '0' && c <= '9';
}

static int is_ident_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident_char(char c) {
  return is_ident_start(c) || is_digit(c) || c == '\'';
}

static char peek(const Lexer *lx, size_t ahead) {
  return lx->pos + ahead < lx->len ? lx->text[lx->pos + ahead] : '\0';
}

static void advance(Lexer *lx, size_t width) {
  for (size_t i = 0; i < width && lx->pos < lx->len; i++) {
    int newline = lx->text[lx->pos] == '\n';
    lx->line += newline;
    lx->col = newline ? 1 : lx->col + 1;
    lx->pos++;
  }
}

static Token *push(Lexer *lx, TokKind kind, size_t start, int line, int col) {
  Token *tok = &lx->out[lx->count];
  lx->count++;
  tok->kind = kind;
  tok->text = lx->text + start;
  tok->len = lx->pos - start;
  tok->nat = 0;
  tok->word = (U256){{0, 0, 0, 0}};
  tok->line = line;
  tok->col = col;
  return tok;
}

/* Skips white space and `--` comments to the end of the line. */
static void skip_trivia(Lexer *lx) {
  for (;;) {
    char c = peek(lx, 0);
    int space = lx->pos < lx->len && (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    int comment = c == '-' && peek(lx, 1) == '-';
    if (!space && !comment) return;
    if (space) {
      advance(lx, 1);
      continue;
    }
    while (lx->pos < lx->len && lx->text[lx->pos] != '\n') advance(lx, 1);
  }
}

static TokKind keyword_kind(const char *text, size_t len) {
  for (size_t i = 0; i < sizeof KEYWORDS / sizeof KEYWORDS[0]; i++) {
    if (strlen(KEYWORDS[i].word) == len && memcmp(KEYWORDS[i].word, text, len) == 0) return KEYWORDS[i].kind;
  }
  return TOK_IDENT;
}

static int lex_ident(Lexer *lx) {
  size_t start = lx->pos;
  int line = lx->line;
  int col = lx->col;
  while (is_ident_char(peek(lx, 0))) advance(lx, 1);
  push(lx, keyword_kind(lx->text + start, lx->pos - start), start, line, col);
  return 1;
}

static int hex_value(char c) {
  return is_digit(c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The U256 suffix: a `u` that no name character follows. */
static int at_suffix(const Lexer *lx) {
  return peek(lx, 0) == 'u' && !is_ident_char(peek(lx, 1));
}

static int push_word(Lexer *lx, uint64_t sort, U256 value, size_t start, int line, int col) {
  Token *tok = push(lx, TOK_WORD, start, line, col);
  tok->nat = sort;
  tok->word = value;
  return 1;
}

/* 0x, 1 to 64 hex digits and u is a U256. 0x and exactly 40 hex digits is an
   Addr. */
static int lex_hex(Lexer *lx) {
  size_t start = lx->pos;
  int line = lx->line;
  int col = lx->col;
  U256 value = {{0, 0, 0, 0}};
  size_t digits = 0;
  int suffix;
  advance(lx, 2);
  while (hex_value(peek(lx, 0)) >= 0) {
    u256_scale_add(&value, 16u, (uint32_t)hex_value(peek(lx, 0)));
    digits++;
    advance(lx, 1);
  }
  suffix = at_suffix(lx);
  advance(lx, suffix ? 1u : 0u);
  if (is_ident_char(peek(lx, 0))) {
    return diag_fail(lx->diag, "LEX_CHAR", NULL, "%s:%d:%d: a letter follows a hex literal", lx->source, lx->line, lx->col);
  }
  if (suffix ? digits == 0 || digits > 64 : digits != 40) {
    return diag_fail(lx->diag, "LEX_HEX", NULL, "%s:%d:%d: a hex literal is 0x, 1 to 64 hex digits and u (a U256), or 0x and 40 hex digits (an Addr); found %u hex digits", lx->source, line, col, (unsigned)digits);
  }
  return push_word(lx, suffix ? WORD_U256 : WORD_ADDR, value, start, line, col);
}

static int lex_nat(Lexer *lx) {
  size_t start = lx->pos;
  int line = lx->line;
  int col = lx->col;
  uint64_t value = 0;
  U256 wide = {{0, 0, 0, 0}};
  int overflow = 0;
  int wide_overflow = 0;
  if (peek(lx, 0) == '0' && peek(lx, 1) == 'x') return lex_hex(lx);
  while (is_digit(peek(lx, 0))) {
    uint64_t digit = (uint64_t)(peek(lx, 0) - '0');
    overflow |= value > (UINT64_MAX - digit) / 10u;
    value = value * 10u + digit;
    wide_overflow |= !u256_scale_add(&wide, 10u, (uint32_t)digit);
    advance(lx, 1);
  }
  if (at_suffix(lx)) {
    advance(lx, 1);
    if (wide_overflow) {
      return diag_fail(lx->diag, "LEX_U256_RANGE", NULL, "%s:%d:%d: a U256 literal is larger than 2^256 - 1", lx->source, line, col);
    }
    return push_word(lx, WORD_U256, wide, start, line, col);
  }
  if (overflow) {
    return diag_fail(lx->diag, "LEX_NAT_RANGE", NULL, "%s:%d:%d: a Nat literal is larger than 2^64 - 1", lx->source, line, col);
  }
  if (is_ident_start(peek(lx, 0))) {
    return diag_fail(lx->diag, "LEX_CHAR", NULL, "%s:%d:%d: a letter follows a Nat literal", lx->source, lx->line, lx->col);
  }
  push(lx, TOK_NAT, start, line, col)->nat = value;
  return 1;
}

static int lex_symbol(Lexer *lx) {
  size_t start = lx->pos;
  int line = lx->line;
  int col = lx->col;
  char c = peek(lx, 0);
  char next = peek(lx, 1);
  TokKind kind = TOK_EOF;
  size_t width = 1;
  switch (c) {
    case '(': kind = TOK_LPAREN; break;
    case ')': kind = TOK_RPAREN; break;
    case '*': kind = TOK_STAR; break;
    case '|': kind = TOK_BAR; break;
    case ':':
      kind = next == '=' ? TOK_DEFINE : TOK_COLON;
      width = next == '=' ? 2 : 1;
      break;
    case '-':
      kind = next == '>' ? TOK_ARROW : TOK_EOF;
      width = 2;
      break;
    case '=':
      kind = next == '>' ? TOK_FATARROW : TOK_EOF;
      width = 2;
      break;
    default: break;
  }
  if (kind == TOK_EOF) {
    return diag_fail(lx->diag, "LEX_CHAR", NULL, "%s:%d:%d: unexpected byte 0x%02x", lx->source, line, col, (unsigned)(unsigned char)c);
  }
  advance(lx, width);
  push(lx, kind, start, line, col);
  return 1;
}

int lex_source(Arena *arena, const char *source, const char *text, size_t len, TokenList *out, Diag *diag) {
  /* Each token takes at least one byte, and one EOF token follows. */
  Token *tokens = arena_alloc(arena, (len + 1) * sizeof(Token));
  if (tokens == NULL) return diag_fail(diag, "OOM", NULL, "%s: no memory for the tokens", source);
  Lexer lx = {
    .text = text, .len = len, .pos = 0, .line = 1, .col = 1,
    .out = tokens, .count = 0, .diag = diag, .source = source,
  };
  for (;;) {
    skip_trivia(&lx);
    if (lx.pos >= lx.len) break;
    char c = peek(&lx, 0);
    int ok = is_ident_start(c) ? lex_ident(&lx) : is_digit(c) ? lex_nat(&lx) : lex_symbol(&lx);
    if (!ok) return 0;
  }
  push(&lx, TOK_EOF, lx.pos, lx.line, lx.col);
  out->items = tokens;
  out->count = lx.count;
  return 1;
}
