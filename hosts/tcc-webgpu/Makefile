# Builds the tcc-js kit with TinyCC. See README.md.
TCC ?= tcc
CC ?= cc
DENO ?= deno
CFLAGS = -std=c99 -Wall -Werror -Isrc
CLANG_FLAGS = -std=c99 -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only -Isrc
FRONT = src/front/base.c src/front/lexer.c src/front/parser.c src/front/front.c src/front/eval.c src/front/check.c
TARGET = src/json.c src/js.c src/wgsl.c
SRC = src/main.c $(FRONT) $(TARGET)
HEADERS = $(wildcard src/*.h src/front/*.h)

.PHONY: build check check-clang clean gpu-check

build: build/langc

build/domain.c: domain/domain.lang gen/embed.c
	mkdir -p build
	$(TCC) -run gen/embed.c domain/domain.lang build/domain.c

build/langc: $(SRC) $(HEADERS) build/domain.c
	$(TCC) $(CFLAGS) -o build/langc $(SRC) build/domain.c

build/backend-regressions: test/backend-regressions.c $(SRC) $(HEADERS) build/domain.c
	$(TCC) $(CFLAGS) -o $@ test/backend-regressions.c $(FRONT) src/js.c src/wgsl.c build/domain.c

check-clang: build/domain.c
	$(CC) $(CLANG_FLAGS) $(SRC) build/domain.c
	$(CC) $(CLANG_FLAGS) test/backend-regressions.c

check: build/langc build/backend-regressions check-clang
	build/backend-regressions
	sh test/gate.sh
	TCC="$(TCC)" node test/regressions.js
	node test/printer-regressions.js

# The GPU selftest (K-C7, O8): each example, built with --selftest --gpu,
# runs under Deno on a real GPU. Not in check: the sandbox has no GPU.
gpu-check: build/langc
	mkdir -p build/gpu
	for p in examples/*.lang; do n=$$(basename $$p .lang); echo "gpu: $$n"; \
	  build/langc build $$p --js build/gpu/$$n.js --selftest --gpu && $(DENO) run --unstable-webgpu build/gpu/$$n.js || exit 1; done

clean:
	rm -rf build
