.PHONY: check doc-check test

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

# Fail on an em-dash or en-dash in a Markdown file, or on a malformed placeholder.
doc-check:
	@perl bin/doc-check.pl .

test:
	@python3 -B -m unittest discover -s tests -v
