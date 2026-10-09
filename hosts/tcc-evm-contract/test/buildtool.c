/* Output regressions for runtime extraction and a closed stdout pipe. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "evm.h"

static int runtime_size(void) {
  unsigned char pairs[1000][64];
  char hex[16] = {0};
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
  ok = ok && !ferror(out) && len == 7u && strcmp(hex, "5f5ffd\n") == 0;
  fclose(out);
  if (!ok) fputs("FAIL runtime: creation size must not limit runtime output\n", stderr);
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
  ok &= closed_pipe();
  if (ok) puts("build output regressions: passed");
  return ok ? 0 : 1;
}
