/* The langc command line. SHARED by the tcc kits.
   Exit 0: ok. Exit 1: the program is refused. Exit 2: usage or IO. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "front/check.h"
#include "front/front.h"
#include "lower.h"
#include "target.h"

#define ARENA_LIMIT_BYTES ((size_t)1 << 30)

typedef enum {
  CMD_CHECK,
  CMD_EVAL,
  CMD_RUN,
  CMD_IR,
  CMD_ABI,
  CMD_BUILD
} Command;

typedef struct {
  Command command;
  const char *prog_path;
  const char *entry;
  char **args;
  int arg_count;
  const char *script_path;
  const char *out_path;
  int runtime;
} Options;

static const struct {
  const char *word;
  Command command;
} COMMANDS[] = {
  {"check", CMD_CHECK},
  {"eval", CMD_EVAL},
  {"run", CMD_RUN},
  {"ir", CMD_IR},
  {"abi", CMD_ABI},
  {"build", CMD_BUILD},
};

static int usage(FILE *err) {
  fputs("usage: langc check PROG\n"
        "       langc eval PROG NAME [ARGS...]\n"
        "       langc run PROG SCRIPT\n"
        "       langc ir PROG\n"
        "       langc abi PROG\n"
        "       langc build PROG [-o OUT] [--runtime]\n", err);
  return 2;
}

static int find_command(const char *word, Command *out) {
  for (size_t i = 0; i < sizeof COMMANDS / sizeof COMMANDS[0]; i++) {
    if (strcmp(COMMANDS[i].word, word) == 0) {
      *out = COMMANDS[i].command;
      return 1;
    }
  }
  return 0;
}

static int parse_build_options(int argc, char **argv, Options *opt, Diag *diag) {
  for (int i = 3; i < argc; i++) {
    int is_out = strcmp(argv[i], "-o") == 0 && i + 1 < argc;
    int is_runtime = strcmp(argv[i], "--runtime") == 0;
    if (!is_out && !is_runtime) return diag_fail(diag, "USAGE", NULL, "unknown build argument '%s'", argv[i]);
    opt->out_path = is_out ? argv[i + 1] : opt->out_path;
    opt->runtime |= is_runtime;
    i += is_out;
  }
  return 1;
}

static int parse_options(int argc, char **argv, Options *opt, Diag *diag) {
  memset(opt, 0, sizeof *opt);
  if (argc < 3 || !find_command(argv[1], &opt->command)) {
    return diag_fail(diag, "USAGE", NULL, "expected a command and a program path");
  }
  opt->prog_path = argv[2];
  switch (opt->command) {
    case CMD_CHECK:
    case CMD_IR:
    case CMD_ABI:
      return argc == 3 ? 1 : diag_fail(diag, "USAGE", NULL, "too many arguments");
    case CMD_EVAL:
      if (argc < 4) return diag_fail(diag, "USAGE", NULL, "eval needs a definition name");
      opt->entry = argv[3];
      opt->args = argv + 4;
      opt->arg_count = argc - 4;
      return 1;
    case CMD_RUN:
      if (argc != 4) return diag_fail(diag, "USAGE", NULL, "run needs a program and a script");
      opt->script_path = argv[3];
      return 1;
    case CMD_BUILD:
      return parse_build_options(argc, argv, opt, diag);
  }
  return 0;
}

static int read_source(Arena *arena, const char *path, char **out, size_t *len, Diag *diag) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return diag_fail(diag, "IO", NULL, "cannot open %s", path);
  char *buf = arena_alloc(arena, SOURCE_MAX_BYTES + 1);
  size_t count = buf == NULL ? 0 : fread(buf, 1, SOURCE_MAX_BYTES + 1, file);
  int read_error = ferror(file);
  fclose(file);
  if (buf == NULL) return diag_fail(diag, "OOM", NULL, "no memory for %s", path);
  if (read_error) return diag_fail(diag, "IO", NULL, "cannot read %s", path);
  if (count > SOURCE_MAX_BYTES) return diag_fail(diag, "IO_SIZE", NULL, "%s is larger than %zu bytes", path, SOURCE_MAX_BYTES);
  *out = buf;
  *len = count;
  return 1;
}

static const char *command_word(Command command) {
  switch (command) {
    case CMD_CHECK: return "check";
    case CMD_EVAL: return "eval";
    case CMD_RUN: return "run";
    case CMD_IR: return "ir";
    case CMD_ABI: return "abi";
    case CMD_BUILD: return "build";
  }
  return "?";
}

/* `langc build` (slice K4a): the code goes to a tmpfile first, so a refused
   or failed build writes nothing on stdout or on OUT. Returns 0, 1 after a
   refusal, or 2 after an IO_WRITE diagnostic. */
static int build_command(Machine *m, const Options *opt, Diag *diag) {
  FILE *code = tmpfile();
  if (code == NULL) return diag_fail(diag, "IO_WRITE", NULL, "tmpfile: %s", strerror(errno)) + 2;
  int status = lower_build(m, opt->runtime ? TARGET_PART_RUNTIME : TARGET_PART_MAIN, code);
  if (status != 0) {
    fclose(code);
    return status;
  }
  rewind(code);
  const char *name = opt->out_path != NULL ? opt->out_path : "stdout";
  FILE *out = opt->out_path != NULL ? fopen(opt->out_path, "w") : stdout;
  if (out == NULL) {
    int error = errno;
    fclose(code);
    return diag_fail(diag, "IO_WRITE", NULL, "%s: %s", name, strerror(error)) + 2;
  }
  /* Report broken pipes through IO_WRITE along with other write failures. */
  signal(SIGPIPE, SIG_IGN);
  unsigned char buffer[1024];
  size_t size = 0;
  int failed = 0;
  while (!failed && (size = fread(buffer, 1, sizeof buffer, code)) > 0) failed = fwrite(buffer, 1, size, out) != size;
  failed = failed || ferror(code) || fflush(out) != 0 || ferror(out);
  int error = errno;
  fclose(code);
  if (out != stdout && fclose(out) != 0) failed = 1;
  return failed ? diag_fail(diag, "IO_WRITE", NULL, "%s: %s", name, strerror(error)) + 2 : 0;
}

static int run(const Options *opt, Arena *arena, Diag *diag) {
  char *text = NULL;
  size_t len = 0;
  if (!read_source(arena, opt->prog_path, &text, &len, diag)) return 2;
  DeclList decls;
  if (!front_load(arena, opt->prog_path, text, len, &decls, diag)) return 1;
  Machine machine;
  if (!check_program(arena, &decls, &machine, diag)) return 1;
  switch (opt->command) {
    case CMD_CHECK:
      puts("ok");
      return 0;
    case CMD_EVAL:
      return eval_command(&machine, opt->entry, opt->args, opt->arg_count, stdout, stderr);
    case CMD_RUN: {
      char *script = NULL;
      size_t script_len = 0;
      if (!read_source(arena, opt->script_path, &script, &script_len, diag)) return 2;
      return run_command(&machine, opt->script_path, script, script_len, stdout);
    }
    case CMD_BUILD:
      return build_command(&machine, opt, diag);
    case CMD_IR:
    case CMD_ABI:
      break;
  }
  diag_fail(diag, "PLANNED", NULL, "the %s command is not built yet (target %s)", command_word(opt->command), target_name);
  return 1;
}

int main(int argc, char **argv) {
  Diag diag;
  diag_init(&diag);
  Options opt;
  if (!parse_options(argc, argv, &opt, &diag)) {
    diag_print(&diag, stderr);
    return usage(stderr);
  }
  Arena arena;
  arena_init(&arena, ARENA_LIMIT_BYTES);
  int status = run(&opt, &arena, &diag);
  arena_release(&arena);
  if (status != 0) diag_print(&diag, stderr);
  return status;
}
