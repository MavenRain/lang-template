.PHONY: check doc-check test

# The files that hosts/tcc-wasm and hosts/tcc-evm share. They must be the same
# in both kits.
TCC_SHARED = src/front src/ir.h src/target.h src/main.c gen domain examples \
  test/parse test/check test/eval test/ir test/grid.awk test/review.sh \
  test/fronttool.c .gitignore

# Run the gate of each host kit. Skip a host kit that is not present.
check: doc-check test
	@if [ -f hosts/mech/Makefile ]; then \
	  $(MAKE) -C hosts/mech check test; \
	else \
	  echo "check: skip hosts/mech (no Makefile)"; \
	fi
	@if [ -f hosts/assay/gate.sh ]; then \
	  bash hosts/assay/gate.sh; \
	else \
	  echo "check: skip hosts/assay (no gate.sh)"; \
	fi
	@if [ -f hosts/tcc-json/Makefile ]; then \
	  $(MAKE) -C hosts/tcc-json check; \
	else \
	  echo "check: skip hosts/tcc-json (no Makefile)"; \
	fi
	@if [ -f hosts/tcc-evm-contract/Makefile ]; then \
	  $(MAKE) -C hosts/tcc-evm-contract check; \
	else \
	  echo "check: skip hosts/tcc-evm-contract (no Makefile)"; \
	fi
	@if [ -f hosts/tcc-wasm/Makefile ]; then \
	  $(MAKE) -C hosts/tcc-wasm check; \
	else \
	  echo "check: skip hosts/tcc-wasm (no Makefile)"; \
	fi
	@if [ -f hosts/tcc-evm/Makefile ]; then \
	  $(MAKE) -C hosts/tcc-evm check; \
	else \
	  echo "check: skip hosts/tcc-evm (no Makefile)"; \
	fi
	@if [ -d hosts/tcc-wasm ] && [ -d hosts/tcc-evm ]; then \
	  for path in $(TCC_SHARED); do \
	    diff -r hosts/tcc-wasm/$$path hosts/tcc-evm/$$path || exit 1; \
	  done; \
	  echo "check: the tcc-wasm and tcc-evm shared files are the same"; \
	else \
	  echo "check: skip the tcc shared-file diff (a kit is missing)"; \
	fi

# Fail on an em-dash or en-dash in a Markdown file, or on a malformed placeholder.
doc-check:
	@perl bin/doc-check.pl .

test:
	@python3 -B -m unittest discover -s tests -v
