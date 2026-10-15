# Session 5 - everything that runs on the host.
#   make test          unit tests of Labs 5.1, 5.2, 5.3
#   make check-flight  the flight build contains no fault injection code
#   make demo          Lab 5.3 scenario and Lab 5.4 campaign on the host
#                      simulation (about 8 minutes), logs and results
LABS = lab-5.1-monitoring lab-5.2-liveness lab-5.3-safe-mode
G    = ../lab-5.4-fault-injection/ground
L    = ../lab-5.4-fault-injection/logs

.PHONY: test check-flight demo clean
test:
	@for d in $(LABS); do $(MAKE) --no-print-directory -C $$d test || exit 1; done
check-flight:
	$(MAKE) --no-print-directory -C app check-flight
demo:
	$(MAKE) -C app host
	cd app && python3 $(G)/gs_modes.py --spawn ./lab5-test.host --log $(L)/host-sim-modes.log
	cd app && python3 $(G)/campaign.py --spawn ./lab5-test.host --label host-sim --log $(L)/host-sim-campaign.log
clean:
	@for d in $(LABS) app; do $(MAKE) --no-print-directory -C $$d clean; done
