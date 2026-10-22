# Session 6 - Model-based development of the mode manager
#   make test          generator checks, back-to-back + coverage, traceability matrix
#   make system-test   Session 5 system scenarios against the generated build (host simulation)
#   make rtems         RTEMS 6 / LEON3 images (lab6-test.exe, lab6-flight.exe)
.PHONY: all test system-test rtems clean

all: test

test:
	$(MAKE) -C lab-6.2-generation test
	$(MAKE) -C lab-6.3-equivalence test
	cd lab-6.4-traceability && python3 build_matrix.py

system-test:
	$(MAKE) -C app host host-flight check-no-hand system-test

rtems:
	$(MAKE) -C app rtems

clean:
	$(MAKE) -C lab-6.2-generation clean
	$(MAKE) -C lab-6.3-equivalence clean
	$(MAKE) -C app clean
