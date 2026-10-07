# MECH_BIN is the mechanism-lang binary (see PIN).
MECH_BIN ?= mech
export MECH_BIN

.PHONY: build check test

build:
	node bin/build.mjs

check:
	node bin/build.mjs --check

test: build
	@set -- test/*.test.mjs; test -f "$$1" || { echo "no compiler tests found" >&2; exit 1; }
	node --stack-size=7000 --max-old-space-size=1024 --test --test-concurrency=1 test/*.test.mjs
