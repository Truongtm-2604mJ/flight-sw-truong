# Lab 6.2 - Code generation (Path B).
#
#   make generate         regenerate generated/ and the Lab 6.1 completeness report
#   make check-generated  fail if generated/ differs from a fresh generation
#   make test             generator negative tests + flight warning set + static analysis
include ../rtems.mk

MODELGEN = $(PYTHON) modelgen.py
TIDY_CHECKS = -*,bugprone-*,cert-*,clang-analyzer-*,misc-*,readability-*,hicpp-multiway-paths-covered,hicpp-signed-bitwise,-readability-identifier-length,-readability-magic-numbers

.PHONY: generate check-generated test clean

generate:
	$(MODELGEN) $(MODEL) --out generated --report ../lab-6.1-model/completeness.md

check-generated:
	$(MODELGEN) $(MODEL) --out generated --check

test: check-generated
	$(PYTHON) tests/test_modelgen.py
	$(CC) $(GEN_CFLAGS) -c generated/mode_sm.c -Igenerated -o /tmp/mode_sm_flight.o
	@echo "OK: generated module compiles with the flight warning set, no warning"
	@if command -v clang-tidy >/dev/null; then \
	    clang-tidy generated/mode_sm.c -checks='$(TIDY_CHECKS)' -- -std=c99 -Igenerated \
	        > static-analysis/clang-tidy-after.txt 2>&1; \
	    n=$$(grep -c "warning:" static-analysis/clang-tidy-after.txt); \
	    echo "clang-tidy: $$n findings (static-analysis/clang-tidy-after.txt)"; \
	    [ "$$n" = 0 ]; \
	else echo "clang-tidy not installed: static analysis skipped"; fi

clean:
	rm -f /tmp/mode_sm_flight.o
