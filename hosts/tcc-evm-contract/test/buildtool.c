/* Output regressions for runtime extraction and a closed stdout pipe. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "evm.h"
#include "keccak.h"

static int selector_collision(void) {
  const IrStmt stop = {.kind = IR_STMT_STOP};
  const IrStmt revert = {.kind = IR_STMT_REVERT};
  const IrStmt *stop_items[] = {&stop}, *revert_items[] = {&revert};
  IrFunc funcs[] = {
    {.name = "entry37557", .body = {stop_items, 1}},
    {.name = "entry9660", .body = {revert_items, 1}}
  };
  unsigned char left[32], right[32];
  static const char left_sig[] = "entry37557()", right_sig[] = "entry9660()";
  keccak256((const unsigned char *)left_sig, sizeof left_sig - 1u, left);
  keccak256((const unsigned char *)right_sig, sizeof right_sig - 1u, right);
  int ok = memcmp(left, right, 4) == 0;
  FILE *out = tmpfile();
  if (out == NULL) return 0;
  for (size_t order = 0; order < 2; order++) {
    const IrProgram program = {funcs, 2};
    const TargetPart parts[] = {TARGET_PART_MAIN, TARGET_PART_RUNTIME};
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) {
      EvmBuild result = evm_build(&program, NULL, 0, parts[i], out);
      ok = result == EVM_BUILD_SELECTOR && ftell(out) == 0 && ok;
    }
    IrFunc swap = funcs[0]; funcs[0] = funcs[1]; funcs[1] = swap;
  }
  fclose(out);
  if (!ok) fputs("FAIL selector collision: ambiguous entries must be refused before output\n", stderr);
  return ok;
}

static int long_event(void) {
  char name[131];
  IrScalar types[16];
  const IrExpr word = {.kind = IR_EXPR_CONST, .value = 7};
  const IrExpr *fields[16];
  memset(name, 'X', sizeof name - 1u);
  memcpy(name, "LongEvent", 9);
  name[sizeof name - 1u] = '\0';
  for (size_t i = 0; i < 16; i++) {
    types[i] = IR_SCALAR_U256;
    fields[i] = &word;
  }
  const IrStmt log = {.kind = IR_STMT_LOG, .name = name, .types = types,
                      .fields = fields, .field_count = 16};
  const IrStmt stop = {.kind = IR_STMT_STOP};
  const IrStmt *items[] = {&log, &stop};
  const IrFunc func = {.name = "fire", .body = {items, 2}};
  const IrProgram program = {&func, 1};
  /* Geth SHA3 of the 259-byte signature: the 130-byte name and 16 uint256 fields. */
  static const char topic[] = "7fb0a29f25fa17882045416d0db30634690efac21a991273249de567262a1747ff";
  const TargetPart parts[] = {TARGET_PART_MAIN, TARGET_PART_RUNTIME};
  int ok = 1;
  for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) {
    char hex[4096] = {0};
    FILE *out = tmpfile();
    if (out == NULL) return 0;
    EvmBuild result = evm_build(&program, NULL, 0, parts[i], out);
    rewind(out);
    size_t len = fread(hex, 1, sizeof hex - 1u, out);
    ok = result == EVM_BUILD_OK && len > 0 && !ferror(out) && strstr(hex, topic) != NULL && ok;
    fclose(out);
  }
  if (!ok) fputs("FAIL long event: valid event signatures must fit and hash in full\n", stderr);
  return ok;
}

static int runtime_size(void) {
  /* A program with no entry: the dispatcher head, the REVERT block and the Trap() block. */
  static const char want[] = "346100125760043610610012575f3560e01c5b5f5ffd5b63ae96083a60e01b5f5260045ffd\n";
  unsigned char pairs[1000][64];
  char hex[sizeof want + 1u] = {0};
  FILE *out = tmpfile();
  if (out == NULL) return 0;
  memset(pairs, 255, sizeof pairs);
  for (size_t i = 0; i < 1000; i++) {
    pairs[i][30] = (unsigned char)(i >> 8);
    pairs[i][31] = (unsigned char)i;
  }
  int ok = evm_build(&(IrProgram){NULL, 0}, (const unsigned char (*)[64])pairs, 1000, TARGET_PART_MAIN, out) == EVM_BUILD_SIZE
    && ftell(out) == 0;
  ok = ok && evm_build(&(IrProgram){NULL, 0}, (const unsigned char (*)[64])pairs, 1000, TARGET_PART_RUNTIME, out) == EVM_BUILD_OK;
  rewind(out);
  size_t len = fread(hex, 1, sizeof hex - 1u, out);
  ok = ok && !ferror(out) && len == sizeof want - 1u && strcmp(hex, want) == 0;
  fclose(out);
  if (!ok) fputs("FAIL runtime: creation size must not limit runtime output\n", stderr);
  return ok;
}

static int runtime_limit(void) {
  const IrExpr slot = {.kind = IR_EXPR_CONST, .value = 0};
  const IrExpr value = {.kind = IR_EXPR_CONST, .value = 1};
  const IrStmt store = {.kind = IR_STMT_SSTORE, .expr = &slot, .value = &value};
  const IrStmt stop = {.kind = IR_STMT_STOP};
  const IrStmt **items = calloc(7001, sizeof *items);
  if (items == NULL) return 0;
  for (size_t i = 0; i < 7000; i++) items[i] = &store;
  items[7000] = &stop;
  IrFunc func = {.name = "large", .body = {items, 7001}};
  const IrProgram program = {&func, 1};
  const TargetPart parts[] = {TARGET_PART_MAIN, TARGET_PART_RUNTIME};
  int ok = 1;
  for (size_t round = 0; round < 2; round++) {
    EvmBuild want = round == 0 ? EVM_BUILD_SIZE : EVM_BUILD_OK;
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) {
      FILE *out = tmpfile();
      if (out == NULL) { free(items); return 0; }
      EvmBuild result = evm_build(&program, NULL, 0, parts[i], out);
      ok = result == want && (want == EVM_BUILD_OK ? ftell(out) > 0 : ftell(out) == 0) && ok;
      fclose(out);
    }
    items[6000] = &stop;
    func.body.count = 6001;
  }
  free(items);
  if (!ok) fputs("FAIL runtime limit: both output modes must enforce EIP-170 and accept smaller code\n", stderr);
  return ok;
}

static int closed_pipe(void) {
  int fds[2];
  FILE *err = tmpfile();
  if (err == NULL) return 0;
  if (pipe(fds) != 0) {
    fclose(err);
    return 0;
  }
  close(fds[0]);
  pid_t child = fork();
  if (child == 0) {
    signal(SIGPIPE, SIG_DFL);
    if (dup2(fds[1], STDOUT_FILENO) < 0 || dup2(fileno(err), STDERR_FILENO) < 0) _exit(127);
    close(fds[1]);
    fclose(err);
    execl("build/langc", "langc", "build", "examples/storage.lang", (char *)NULL);
    _exit(127);
  }
  close(fds[1]);
  int status = 0;
  pid_t waited;
  do {
    waited = child < 0 ? -1 : waitpid(child, &status, 0);
  } while (child >= 0 && waited < 0 && errno == EINTR);
  char diagnostic[256] = {0};
  rewind(err);
  size_t len = fread(diagnostic, 1, sizeof diagnostic - 1u, err);
  int ok = waited == child && child >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 2
    && len > 0 && !ferror(err) && strstr(diagnostic, "langc: IO_WRITE: -: stdout: ") != NULL;
  fclose(err);
  if (!ok) fputs("FAIL stdout pipe: want exit 2 and IO_WRITE\n", stderr);
  return ok;
}

int main(void) {
  int ok = runtime_size();
  ok &= runtime_limit();
  ok &= selector_collision();
  ok &= long_event();
  ok &= closed_pipe();
  if (ok) puts("build output regressions: passed");
  return ok ? 0 : 1;
}
