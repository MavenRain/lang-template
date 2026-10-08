# Builds the tcc-evm-contract kit with TinyCC. See README.md.
TCC ?= tcc
CC ?= cc
CFLAGS = -std=c99 -Wall -Werror -Isrc
CLANG_FLAGS = -std=c99 -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only -Isrc
FRONT = src/front/base.c src/front/u256.c src/front/lexer.c src/front/parser.c src/front/front.c src/front/contract.c src/front/eval.c src/front/check.c
TARGET = src/evm.c src/asm.c src/keccak.c
SRC = src/main.c $(FRONT) $(TARGET)
TOOL = test/asmtool.c $(TARGET)
FRONT_TOOL = test/fronttool.c $(filter-out src/front/front.c,$(FRONT))
HEADERS = $(wildcard src/*.h src/front/*.h)

.PHONY: build check check-clang clean

build: build/langc build/asmtool build/fronttool

build/domain.c: domain/domain.lang gen/embed.c
	mkdir -p build
	$(TCC) -run gen/embed.c domain/domain.lang build/domain.c

build/langc: $(SRC) $(HEADERS) build/domain.c
	$(TCC) $(CFLAGS) -o build/langc $(SRC) build/domain.c

build/asmtool: $(TOOL) $(HEADERS)
	mkdir -p build
	$(TCC) $(CFLAGS) -o build/asmtool $(TOOL)

build/fronttool: $(FRONT_TOOL) $(HEADERS)
	mkdir -p build
	$(TCC) $(CFLAGS) -o build/fronttool $(FRONT_TOOL)

check-clang: build/domain.c
	$(CC) $(CLANG_FLAGS) $(SRC) build/domain.c
	$(CC) $(CLANG_FLAGS) $(TOOL)
	$(CC) $(CLANG_FLAGS) $(FRONT_TOOL)

check: build/langc build/asmtool build/fronttool check-clang
	sh test/gate.sh
	sh test/asm.sh
	sh test/review.sh
	build/fronttool

clean:
	rm -rf build
