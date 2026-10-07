# MECH_BIN is the mechanism-lang binary (see PIN).
MECH_BIN ?= mech
export MECH_BIN

.PHONY: build check plans test

build:
	node bin/build.mjs

check:
	node bin/gen-domain.mjs --check
	node bin/build.mjs --check

plans:
	node bin/gen-domain.mjs

test: build
	@set -- test/*.test.mjs; test -f "$$1" || { echo "no compiler tests found" >&2; exit 1; }
	node --stack-size=7000 --max-old-space-size=1024 --test --test-concurrency=1 test/*.test.mjs
