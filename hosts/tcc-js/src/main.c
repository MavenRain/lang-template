#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
/* The langc command line of the tcc-js kit (forked from tcc-json).
   Exit 0: ok. Exit 1: the program is refused. Exit 2: usage or IO. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "front/check.h"
#include "front/front.h"
#include "js.h"
#include "json.h"

#define ARENA_LIMIT_BYTES ((size_t)1 << 30)

typedef enum {
  CMD_CHECK,
  CMD_EVAL,
  CMD_BUILD
} Command;

typedef struct {
  Command command;
  const char *prog_path;
  const char *entry;
  char **args;
  int arg_count;
  const char *out_path; /* NULL: stdout */
  const char *js_path;   /* --js */
  const char *json_path; /* --json */
  int selftest;          /* --selftest */
} Options;

static const struct {
  const char *word;
  Command command;
} COMMANDS[] = {
  {"check", CMD_CHECK},
  {"eval", CMD_EVAL},
  {"build", CMD_BUILD},
};

static int usage(FILE *err) {
  fputs("usage: langc check PROG\n"
        "       langc eval PROG NAME [ARGS...]\n"
        "       langc build PROG [-o OUT]\n"
        "       langc build PROG [--js OUT.js [--selftest]] [--json OUT.json]\n", err);
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

/* Takes the path after the flag at argv[*i] into *slot, once. A path cannot
   start with '-', so a missing path is not read as the next flag. */
static int take_path(int argc, char **argv, int *i, const char **slot) {
  if (*slot != NULL || *i + 1 >= argc || argv[*i + 1][0] == '-') return 0;
  *slot = argv[*i + 1];
  *i += 1;
  return 1;
}

static int take_flag(int argc, char **argv, int *i, Options *opt) {
  if (strcmp(argv[*i], "--js") == 0) return take_path(argc, argv, i, &opt->js_path);
  if (strcmp(argv[*i], "--json") == 0) return take_path(argc, argv, i, &opt->json_path);
  if (strcmp(argv[*i], "--selftest") != 0 || opt->selftest) return 0;
  opt->selftest = 1;
  return 1;
}

/* Compare existing files by identity, and new destinations by their parent
   directory and final component. stat follows directory and file aliases. */
static int output_parent(const char *path, struct stat *info, Diag *diag) {
  const char *slash = strrchr(path, '/');
  size_t len = slash == NULL ? 1u : slash == path ? 1u : (size_t)(slash - path);
  char *parent = malloc(len + 1u);
  int found;
  if (parent == NULL) {
    diag_fail(diag, "OOM", NULL, "no memory for the output directory");
    return -1;
  }
  if (slash == NULL)
    parent[0] = '.';
  else
    memcpy(parent, path, len);
  parent[len] = '\0';
  found = stat(parent, info) == 0;
  free(parent);
  return found;
}

/* Resolve final symbolic links even when their target does not exist yet.
   Directory links are followed by stat when the parents are compared. */
static int output_resolve(const char *path, char **resolved, Diag *diag) {
  const char *current = path;
  char *owned = NULL;
  *resolved = NULL;
  for (unsigned depth = 0; depth <= 40u; depth++) {
    struct stat info;
    const char *slash;
    char *target;
    char *next;
    size_t size;
    size_t prefix;
    ssize_t count;
    if (lstat(current, &info) != 0 || !S_ISLNK(info.st_mode)) {
      *resolved = owned;
      return 1;
    }
    if (depth == 40u)
      break;
    size = info.st_size > 0 ? (size_t)info.st_size + 1u : 4096u;
    target = malloc(size);
    if (target == NULL) {
      free(owned);
      return diag_fail(diag, "OOM", NULL, "no memory for the output symbolic link");
    }
    count = readlink(current, target, size);
    if (count <= 0 || (size_t)count >= size) {
      free(target);
      free(owned);
      return diag_fail(diag, "IO", NULL, "cannot resolve output symbolic link %s", path);
    }
    slash = strrchr(current, '/');
    prefix = target[0] == '/' || slash == NULL ? 0u : (size_t)(slash - current) + 1u;
    next = malloc(prefix + (size_t)count + 1u);
    if (next == NULL) {
      free(target);
      free(owned);
      return diag_fail(diag, "OOM", NULL, "no memory for the output symbolic link");
    }
    memcpy(next, current, prefix);
    memcpy(next + prefix, target, (size_t)count);
    next[prefix + (size_t)count] = '\0';
    free(target);
    free(owned);
    owned = next;
    current = owned;
  }
  free(owned);
  return diag_fail(diag, "IO", NULL, "too many symbolic links in output path %s", path);
}

static int distinct_outputs(const Options *opt, Diag *diag) {
  const char *left = opt->js_path;
  const char *right = opt->json_path;
  const char *left_name;
  const char *right_name;
  char *resolved_left;
  char *resolved_right;
  struct stat a;
  struct stat b;
  int same;
  int found_a;
  int found_b;
  if (left == NULL || right == NULL)
    return 1;
  same = strcmp(left, right) == 0;
  if (!same && stat(left, &a) == 0 && stat(right, &b) == 0)
    same = a.st_dev == b.st_dev && a.st_ino == b.st_ino;
  if (same)
    return diag_fail(diag, "USAGE", NULL, "--js and --json need distinct output files");
  if (!output_resolve(left, &resolved_left, diag))
    return 0;
  if (!output_resolve(right, &resolved_right, diag)) {
    free(resolved_left);
    return 0;
  }
  left = resolved_left == NULL ? left : resolved_left;
  right = resolved_right == NULL ? right : resolved_right;
  left_name = strrchr(left, '/');
  right_name = strrchr(right, '/');
  left_name = left_name == NULL ? left : left_name + 1;
  right_name = right_name == NULL ? right : right_name + 1;
  if (!same && strcmp(left_name, right_name) == 0) {
    found_a = output_parent(left, &a, diag);
    if (found_a < 0) {
      free(resolved_left);
      free(resolved_right);
      return 0;
    }
    found_b = output_parent(right, &b, diag);
    if (found_b < 0) {
      free(resolved_left);
      free(resolved_right);
      return 0;
    }
    same = found_a && found_b && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
  }
  free(resolved_left);
  free(resolved_right);
  return !same || diag_fail(diag, "USAGE", NULL, "--js and --json need distinct output files");
}

/* build PROG [-o OUT], or build PROG with --js OUT.js, --json OUT.json and
   --selftest: each flag at most once, --js or --json at least, --selftest
   only with --js. -o does not mix with the other flags. */
static int parse_build_options(int argc, char **argv, Options *opt, Diag *diag) {
  if (argc == 3) return 1;
  if (argc == 5 && strcmp(argv[3], "-o") == 0) {
    opt->out_path = argv[4];
    return 1;
  }
  for (int i = 3; i < argc; i++) {
    if (!take_flag(argc, argv, &i, opt))
      return diag_fail(diag, "USAGE", NULL, "build takes PROG and an optional -o OUT, or --js OUT.js, --json OUT.json and --selftest");
  }
  if (opt->js_path == NULL && opt->json_path == NULL)
    return diag_fail(diag, "USAGE", NULL, "build needs --js OUT.js or --json OUT.json");
  if (!distinct_outputs(opt, diag))
    return 0;
  return !opt->selftest || opt->js_path != NULL ? 1 : diag_fail(diag, "USAGE", NULL, "--selftest needs --js OUT.js");
}

static int parse_options(int argc, char **argv, Options *opt, Diag *diag) {
  memset(opt, 0, sizeof *opt);
  if (argc < 3 || !find_command(argv[1], &opt->command)) {
    return diag_fail(diag, "USAGE", NULL, "expected a command and a program path");
  }
  opt->prog_path = argv[2];
  switch (opt->command) {
    case CMD_CHECK:
      return argc == 3 ? 1 : diag_fail(diag, "USAGE", NULL, "too many arguments");
    case CMD_EVAL:
      if (argc < 4) return diag_fail(diag, "USAGE", NULL, "eval needs a definition name");
      opt->entry = argv[3];
      opt->args = argv + 4;
      opt->arg_count = argc - 4;
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

/* Writes the document only after it is complete, so a failure leaves no file. */
static int write_output(const char *path, const char *text, size_t len, Diag *diag) {
  FILE *file = path == NULL ? stdout : fopen(path, "wb");
  if (file == NULL) return diag_fail(diag, "IO", NULL, "cannot open %s", path);
  size_t count = fwrite(text, 1, len, file);
  int closed = path == NULL ? fflush(file) : fclose(file);
  return count == len && closed == 0 ? 1 : diag_fail(diag, "IO", NULL, "cannot write %s", path == NULL ? "stdout" : path);
}

/* Builds the JSON document (it also refuses the instances that have no JSON
   form, for both targets) and the module before it writes any file, so a
   refused build writes no file. A failed second write removes the first. */
static int build_command(const Options *opt, Machine *m, Diag *diag) {
  const char *doc = NULL;
  const char *js = NULL;
  size_t doc_len = 0;
  size_t js_len = 0;
  if (opt->js_path != NULL && !js_module(m, opt->selftest, &js, &js_len)) return 1;
  if (!json_document(m, &doc, &doc_len)) return 1;
  if (opt->js_path == NULL)
    return write_output(opt->json_path != NULL ? opt->json_path : opt->out_path, doc, doc_len, diag) ? 0 : 2;
  if (!write_output(opt->js_path, js, js_len, diag)) return 2;
  if (opt->json_path == NULL || write_output(opt->json_path, doc, doc_len, diag)) return 0;
  remove(opt->js_path);
  return 2;
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
    case CMD_BUILD:
      return build_command(opt, &machine, diag);
  }
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
