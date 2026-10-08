# Builds the tcc-evm-anchor kit with TinyCC. See README.md. make build
# builds build/langc, build/parsetool and build/evmtool, make check-clang
# checks every C file with clang, and make check runs both and then
# test/gate.sh (each test file in the order of the anchor-lang make test).
TCC = tcc
CLANG = cc
TCCFLAGS = -std=c99 -Wall -Werror
CLANGFLAGS = -std=c99 -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only

FRONT = src/arena.c src/diag.c src/lexer.c src/parser.c src/printer.c
BACK = src/evm.c src/keccak.c
LANGC = $(FRONT) $(BACK) src/check.c src/main.c
HEADERS = src/arena.h src/check.h src/evm.h src/keccak.h src/prelude.h src/syntax.h
DOMAIN = domain/domain.lang

.PHONY: build check check-clang clean

build: build/langc build/parsetool build/evmtool

build/domain.c: gen/embed.c src/prelude.h $(DOMAIN)
	mkdir -p build
	$(TCC) $(TCCFLAGS) -run gen/embed.c $(DOMAIN) $@

build/langc: $(LANGC) $(HEADERS) build/domain.c
	$(TCC) $(TCCFLAGS) -o $@ $(LANGC) build/domain.c

build/parsetool: $(FRONT) test/parsetool.c $(HEADERS) build/domain.c
	$(TCC) $(TCCFLAGS) -o $@ $(FRONT) test/parsetool.c build/domain.c

build/evmtool: $(BACK) test/evmtool.c src/evm.h src/keccak.h
	mkdir -p build
	$(TCC) $(TCCFLAGS) -o $@ $(BACK) test/evmtool.c

check-clang: build/domain.c
	$(CLANG) $(CLANGFLAGS) src/*.c test/*.c gen/*.c build/domain.c

check: build check-clang
	sh test/gate.sh

clean:
	rm -rf build
