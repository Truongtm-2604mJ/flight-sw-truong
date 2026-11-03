# Session 8 - Security in flight software and ground integration.
#
#   make test          unit suites + system tests on the host simulation
#   make unit          Unity suites (key store, authentication, service 210, reporter)
#   make warnings      Session 8 modules at the flight warning set, -Werror
#   make system-test   lab 8.1/8.2 system tests against lab8-test.host (~3 min)
#   make regression    the Session 5 scenarios, unchanged, through the ground
#                      security unit (~10 min)
#   make rtems         RTEMS 6 / LEON3 images (RTEMS_PREFIX=...)
#   make mdb-check     the Yamcs MDB against the ground decoders (lab 8.3)
#   make timing        crypto cost on the host (comparison only; SIS: app/run-sis.sh)
include rtems.mk

OUT ?= build
UNIT_CFLAGS = -std=c99 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Werror \
              -fsanitize=address,undefined -fno-sanitize-recover=all \
              -I$(UNITY) $(SEC_INC)
UNIT_SUITES = test_keystore test_tc_auth
CORE = $(L42)/crc16.c $(L42)/pus.c $(L42)/tc_handler.c $(L41)/sp.c

.PHONY: test unit warnings app system-test regression rtems mdb-check timing clean

test: warnings unit mdb-check system-test

$(OUT):
	mkdir -p $(OUT)

$(OUT)/%: tests/unit/%.c $(SEC_SRC) $(TINYCRYPT_SRC) $(CORE) $(UNITY)/unity.c | $(OUT)
	@echo "CC $@"
	@$(CC) $(UNIT_CFLAGS) -o $@ $< $(SEC_SRC) $(TINYCRYPT_SRC) $(CORE) $(UNITY)/unity.c

unit: $(addprefix $(OUT)/,$(UNIT_SUITES))
	@set -e; for t in $^; do echo "== $$t"; $$t | tee $$t.txt | tail -3; done

# tinycrypt is third party code: compiled, not held to this warning set.
warnings: | $(OUT)
	@for f in $(SEC_SRC) app/sec_app.c; do \
	    $(CC) $(SEC_WARN) -I$(S5)/host-sim -I$(S5)/app -Iapp $(SEC_INC) -c $$f -o $(OUT)/w.o || exit 1; \
	done; echo "Session 8 modules: 0 warnings ($(words $(SEC_SRC) app/sec_app.c) files)"

app:
	@$(MAKE) -s -C app host host-flight check-wraps

system-test: app
	$(PYTHON) lab-8.1-authentication/tests/test_auth_system.py --spawn app/lab8-test.host \
	    --log lab-8.1-authentication/tests/system-test.log

regression: app
	./ground/run-s5-regression.sh

rtems:
	$(MAKE) -C app rtems

mdb-check:
	$(PYTHON) lab-8.3-ground-segment/tools/check_mdb.py

timing:
	$(MAKE) -C app timing-host && ./app/sec-timing.host

clean:
	rm -rf $(OUT)
	$(MAKE) -C app clean
