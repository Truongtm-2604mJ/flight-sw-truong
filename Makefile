# LOTUS-1 flight software - one application, one build (capstone).
#
#   make                  host simulation builds: test and flight
#   make rtems            RTEMS 6 / LEON3 images (RTEMS_PREFIX=...)
#   make test             every host test level (tests/Makefile)
#   make warnings         every flight source at the maximum warning set
#   make check-generated  src/fdir/mode_sm.* match model/mode_machine.yaml
#   make mdb-check        the Yamcs MDB agrees with the ground decoders
#   make scenario         the 7 step mission scenario, automated (host)
#   make demo             the mission demonstration through Yamcs (demo/README.md)
#   make sis              target evidence on SIS (scripts/run-sis.sh)
#
# The lab directories (labs/) are kept as the history of each session and
# are not built from here. See docs/architecture.md for the source map.

PYTHON ?= python3
CC     ?= gcc
OUT    ?= build

RTEMS_PREFIX ?= $(HOME)/quick-start/rtems/6
RTEMS_CC     := $(RTEMS_PREFIX)/bin/sparc-rtems6-gcc
RTEMS_CFLAGS := -B$(RTEMS_PREFIX)/sparc-rtems6/leon3/lib -qrtems -mcpu=leon3 \
                -O2 -g -Wall -Wextra -ffunction-sections -fdata-sections
RTEMS_LDFLAGS := -Wl,--gc-sections

include src/sources.mk

HOST_CFLAGS := -std=gnu99 -O1 -g -Wall -Wextra -Werror

.PHONY: all host rtems test warnings check-generated mdb-check scenario demo sis clean

all: host

host: $(OUT)/host/lotus1-test.host $(OUT)/host/lotus1-flight.host

$(OUT)/host $(OUT)/rtems:
	@mkdir -p $@

$(OUT)/host/lotus1-test.host: $(APP_SRC) $(TEST_SRC) $(SIM_SRC) | $(OUT)/host
	@echo "  CC    $@"
	@$(CC) $(HOST_CFLAGS) -DFDIR_TEST_BUILD $(SIM_INC) $(SRC_INC) -o $@ \
	    $(APP_SRC) $(TEST_SRC) $(SIM_SRC) -lpthread

$(OUT)/host/lotus1-flight.host: $(APP_SRC) $(SIM_SRC) | $(OUT)/host
	@echo "  CC    $@"
	@$(CC) $(HOST_CFLAGS) $(SIM_INC) $(SRC_INC) -o $@ $(APP_SRC) $(SIM_SRC) -lpthread

rtems: $(OUT)/rtems/lotus1-test.exe $(OUT)/rtems/lotus1-flight.exe \
       $(OUT)/rtems/lotus1-timing.exe $(OUT)/rtems/sec-timing.exe

$(OUT)/rtems/lotus1-test.exe: $(APP_SRC) $(TEST_SRC) | $(OUT)/rtems
	@echo "  RTEMS $@"
	@$(RTEMS_CC) $(RTEMS_CFLAGS) -DFDIR_TEST_BUILD $(SRC_INC) -o $@ $(APP_SRC) \
	    $(TEST_SRC) $(RTEMS_LDFLAGS)

$(OUT)/rtems/lotus1-flight.exe: $(APP_SRC) | $(OUT)/rtems
	@echo "  RTEMS $@"
	@$(RTEMS_CC) $(RTEMS_CFLAGS) $(SRC_INC) -o $@ $(APP_SRC) $(RTEMS_LDFLAGS)

# Flight build that runs 60 s and prints the rate monotonic statistics, with the
# payload enabled from boot so PL_LOG is measured at its worst case.
$(OUT)/rtems/lotus1-timing.exe: $(APP_SRC) | $(OUT)/rtems
	@echo "  RTEMS $@"
	@$(RTEMS_CC) $(RTEMS_CFLAGS) -DRUN_SECONDS=60 -DTIMING_PAYLOAD_ON $(SRC_INC) -o $@ $(APP_SRC) $(RTEMS_LDFLAGS)

# Cost of each security operation (timing budget).
$(OUT)/rtems/sec-timing.exe: tools/timing/sec_timing.c $(CORE_SRC) $(SEC_LIB_SRC) | $(OUT)/rtems
	@echo "  RTEMS $@"
	@$(RTEMS_CC) $(RTEMS_CFLAGS) $(SRC_INC) -o $@ tools/timing/sec_timing.c $(CORE_SRC) \
	    $(SEC_LIB_SRC) $(RTEMS_LDFLAGS)

test:
	$(MAKE) -C tests test

warnings:
	$(MAKE) -C tests warnings

check-generated:
	$(PYTHON) model/modelgen.py model/mode_machine.yaml --out src/fdir --check

mdb-check:
	$(PYTHON) mcs/tools/check_mdb.py

scenario: host
	rm -f $(OUT)/scenario-nv.bin*
	$(PYTHON) demo/scenario.py --spawn $(OUT)/host/lotus1-test.host --nv-file $(OUT)/scenario-nv.bin \
	    --log evidence/demo/scenario-host-sim.log --json evidence/demo/scenario-host-sim.json
	rm -f $(OUT)/scenario-nv.bin*

# Needs mcs/run-bridge.sh fresh and mcs/run-yamcs.sh running (demo/README.md).
demo:
	$(PYTHON) demo/run_demo.py --srt evidence/demo/captions.srt

sis:
	sh scripts/run-sis.sh $(STEPS)

clean:
	rm -rf $(OUT)
	$(MAKE) -C tests clean
