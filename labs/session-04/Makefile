# Session 4 - run everything that can run on the host.
#   make test    all unit test suites
#   make demo    both ground station scenarios against the host simulation
LABS = lab-4.1-space-packet lab-4.2-pus-service17 lab-4.3-housekeeping

.PHONY: test demo clean
test:
	@for d in $(LABS); do $(MAKE) --no-print-directory -C $$d test || exit 1; done
demo:
	$(MAKE) -C lab-4.2-pus-service17 demo
	$(MAKE) -C lab-4.3-housekeeping demo
clean:
	@for d in $(LABS); do $(MAKE) --no-print-directory -C $$d clean; done
