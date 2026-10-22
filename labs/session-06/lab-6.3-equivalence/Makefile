# Lab 6.3 - Back-to-back equivalence and coverage.
#
#   make test       regenerate sequences, run back-to-back and requirement
#                   tests, measure coverage of the generated module
#   make clean
include ../rtems.mk

H     = $(S5)/lab-5.3-safe-mode
COV   = cov
RES   = results
SEQ   = sequences/suite_a.seq sequences/suite_b.seq sequences/soak_a.seq sequences/soak_b.seq

.PHONY: test sequences clean

test: $(COV)/b2b $(COV)/req_tests sequences
	rm -f $(COV)/*.gcda
	mkdir -p $(RES)
	$(COV)/b2b $(RES) $(SEQ)
	$(COV)/req_tests | tee $(RES)/req-tests.txt
	cd $(COV) && gcov -b -c -o . mode_sm.c > gcov-summary.txt
	cp $(COV)/mode_sm.c.gcov $(RES)/mode_sm.c.gcov
	$(PYTHON) coverage_report.py $(RES)/mode_sm.c.gcov $(GEN)/mode_sm_trace.json \
	    > $(RES)/coverage.md
	$(PYTHON) summarise.py $(RES)/divergences.csv > $(RES)/divergence-summary.md
	@cat $(RES)/b2b-summary.txt
	@head -12 $(RES)/coverage.md

sequences:
	$(PYTHON) test_gen.py

$(COV)/mode_sm.o: $(GEN)/mode_sm.c $(GEN)/mode_sm.h
	mkdir -p $(COV)
	cp $(GEN)/mode_sm.c $(GEN)/mode_sm.h $(COV)/
	cd $(COV) && $(CC) -std=c99 -O0 -g --coverage -DMODE_SM_WITH_NAMES -c mode_sm.c -o mode_sm.o

$(COV)/b2b: b2b.c $(COV)/mode_sm.o $(H)/modes.c $(H)/modes.h
	$(CC) $(HOST_CFLAGS) -O0 -DMODE_SM_WITH_NAMES -I$(COV) -I$(H) -o $@ b2b.c \
	    $(COV)/mode_sm.o $(H)/modes.c --coverage

$(COV)/req_tests: req_tests.c $(COV)/mode_sm.o
	$(CC) $(HOST_CFLAGS) -O0 -I$(COV) -o $@ req_tests.c $(COV)/mode_sm.o --coverage

clean:
	rm -rf $(COV)
