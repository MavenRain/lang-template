#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define main langc_main
#include "../src/main.c"
#undef main
#include "../src/json.c"

#include <unistd.h>

static unsigned checked;
static unsigned failed;

static void expect(int condition, const char *message) {
  checked++;
  if (!condition) {
    failed++;
    fprintf(stderr, "FAIL %s\n", message);
  }
}

static void output_options(char *left, char *right, int accepted) {
  Options opt;
  Diag diag;
  char *args[] = {"langc", "build", "unused.lang", "--js", left, "--json", right};
  int result;
  diag_init(&diag);
  result = parse_options(7, args, &opt, &diag);
  expect(result == accepted && (accepted || strcmp(diag.code, "USAGE") == 0),
    accepted ? "distinct outputs were refused" : "overlapping outputs were accepted");
}

static int invoke(int argc, char **argv, const char *code) {
  FILE *out = tmpfile();
  FILE *err = tmpfile();
  int saved_out = dup(STDOUT_FILENO);
  int saved_err = dup(STDERR_FILENO);
  int status;
  char message[512] = {0};
  if (out == NULL || err == NULL || saved_out < 0 || saved_err < 0)
    return -1;
  fflush(stdout);
  fflush(stderr);
  if (dup2(fileno(out), STDOUT_FILENO) < 0 || dup2(fileno(err), STDERR_FILENO) < 0)
    return -1;
  status = langc_main(argc, argv);
  fflush(stdout);
  fflush(stderr);
  expect(ftell(out) == 0, "file build or refusal wrote to stdout");
  rewind(err);
  (void)fread(message, 1, sizeof message - 1, err);
  (void)dup2(saved_out, STDOUT_FILENO);
  (void)dup2(saved_err, STDERR_FILENO);
  close(saved_out);
  close(saved_err);
  fclose(out);
  fclose(err);
  expect(code == NULL ? message[0] == '\0' : strstr(message, code) != NULL,
    "build returned the wrong diagnostic");
  return status;
}

static int write_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  size_t len = strlen(text);
  int written;
  int closed;
  if (file == NULL)
    return 0;
  written = fwrite(text, 1, len, file) == len;
  closed = fclose(file) == 0;
  return written && closed;
}

static void writer_tests(void) {
  Diag diag;
  Arena arena;
  Machine machine;
  const char *bad[] = {"\200", "\301\201", "\342\202", "\355\240\200", "\364\220\200\200"};
  const size_t lengths[] = {1, 2, 2, 3, 4};
  const char valid[] = "\000\177\302\200\342\202\254\360\237\230\200";
  arena_init(&arena, (size_t)1 << 20);
  memset(&machine, 0, sizeof machine);
  machine.arena = &arena;
  machine.diag = &diag;
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    Out out;
    memset(&out, 0, sizeof out);
    out.m = &machine;
    out.def = "bad";
    diag_init(&diag);
    expect(!put_bytes(&out, bad[i], lengths[i]) && strcmp(diag.code, "JSON_UTF8") == 0,
      "invalid UTF-8 was accepted by the JSON writer");
  }
  {
    Out out;
    memset(&out, 0, sizeof out);
    out.m = &machine;
    diag_init(&diag);
    expect(put_bytes(&out, valid, sizeof valid - 1) && out.len == sizeof valid + 6
      && memcmp(out.data, "\"\\u0000", 7) == 0
      && memcmp(out.data + 7, valid + 1, sizeof valid - 2) == 0
      && out.data[out.len - 1] == '"', "valid UTF-8 lost its JSON bytes");
  }
  arena_release(&arena);
}

int main(void) {
  char directory[] = "/tmp/langc-backend-XXXXXX";
  char target[FILENAME_MAX];
  char alias[FILENAME_MAX];
  char hard[FILENAME_MAX];
  char symbolic[FILENAME_MAX];
  char other[FILENAME_MAX];
  char source[FILENAME_MAX];
  struct stat info;
  FILE *file;
  char contents[16] = {0};
  if (mkdtemp(directory) == NULL)
    return 2;
  snprintf(target, sizeof target, "%s/out", directory);
  snprintf(alias, sizeof alias, "%s/./out", directory);
  snprintf(hard, sizeof hard, "%s/hard", directory);
  snprintf(symbolic, sizeof symbolic, "%s/symbolic", directory);
  snprintf(other, sizeof other, "%s/other", directory);
  snprintf(source, sizeof source, "%s/source.lang", directory);
  output_options(target, target, 0);
  output_options(target, alias, 0);
  output_options(target, other, 1);
  expect(symlink(target, symbolic) == 0, "cannot prepare dangling symbolic-link alias");
  output_options(target, symbolic, 0);
  unlink(symbolic);
  expect(symlink("out", symbolic) == 0, "cannot prepare relative dangling alias");
  output_options(target, symbolic, 0);
  unlink(symbolic);
  expect(symlink("out", hard) == 0 && symlink("hard", symbolic) == 0,
    "cannot prepare chained dangling alias");
  output_options(target, symbolic, 0);
  unlink(hard);
  unlink(symbolic);
  expect(write_file(target, "preserve"), "cannot prepare existing output");
  expect(link(target, hard) == 0, "cannot prepare hard-link alias");
  expect(symlink(target, symbolic) == 0, "cannot prepare symbolic-link alias");
  output_options(target, hard, 0);
  output_options(target, symbolic, 0);
  expect(write_file(source, "def valid : Nat := 7\n"), "cannot prepare valid program");
  {
    char *args[] = {"langc", "build", source, "--js", target, "--json", hard};
    expect(invoke(7, args, "USAGE") == 2, "aliased output build did not exit 2");
  }
  file = fopen(target, "rb");
  if (file != NULL) {
    (void)fread(contents, 1, sizeof contents - 1, file);
    fclose(file);
  }
  expect(file != NULL && strcmp(contents, "preserve") == 0, "output collision changed an existing file");
  unlink(target);
  unlink(hard);
  unlink(symbolic);
  {
    char *args[] = {"langc", "build", source, "--js", target, "--json", other};
    expect(invoke(7, args, NULL) == 0, "distinct output build failed");
    expect(stat(target, &info) == 0 && stat(other, &info) == 0, "distinct build omitted an output");
  }
  unlink(target);
  unlink(other);
  expect(symlink("out", symbolic) == 0, "cannot prepare dangling build output");
  {
    char *args[] = {"langc", "build", source, "--js", symbolic, "--json", target};
    expect(invoke(7, args, "USAGE") == 2, "dangling output collision did not exit 2");
    expect(stat(target, &info) != 0, "dangling output collision created its target");
  }
  unlink(symbolic);
  expect(write_file(source, "def firstByte : Str := \"\200\"\ndef secondByte : Str := \"\201\"\n"),
    "cannot prepare invalid UTF-8 program");
  {
    char *args[] = {"langc", "build", source};
    expect(invoke(3, args, "JSON_UTF8") == 1, "JSON stdout build did not refuse invalid UTF-8");
  }
  {
    char *args[] = {"langc", "build", source, "-o", other};
    expect(invoke(5, args, "JSON_UTF8") == 1, "JSON-only build did not refuse invalid UTF-8");
    expect(stat(other, &info) != 0, "JSON UTF-8 refusal created an output");
  }
  {
    char *args[] = {"langc", "build", source, "--js", target, "--json", other};
    expect(invoke(7, args, "JS_UTF8") == 1, "JS build changed its UTF-8 diagnostic");
    expect(stat(target, &info) != 0 && stat(other, &info) != 0, "JS UTF-8 refusal created an output");
  }
  writer_tests();
  unlink(source);
  rmdir(directory);
  printf("backend regressions: %u checked, %u failed\n", checked, failed);
  return failed != 0;
}
