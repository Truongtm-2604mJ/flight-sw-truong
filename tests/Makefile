# Flight software test architecture (Session 7, Lab 7.1).
#
#   make test             everything that runs on a host: unit, integration
#                         (host simulation of RTEMS) and validation (ground
#                         interface, host simulation). Exit code 0 = all pass.
#   make unit             unit suites only (Unity, no RTEMS dependency)
#   make integration      integration suite on the host simulation
#   make validation       validation campaign on the host simulation
#   make target           integration + validation on RTEMS 6 / LEON3 under SIS
#                         (needs RTEMS_PREFIX; nightly CI job)
#   make warnings         every flight source at the maximum warning set, -Werror
#   make coverage         gcov instrumented unit + integration run, gcovr reports
#   make mcdc             clang source based MC/DC measurement (llvm-cov)
#   make static           cppcheck + clang static analyser (scan-build)
#
# Pinned tools (CI checks these exact versions): GCC 13.3, clang/LLVM 18.1,
# cppcheck 2.13.0, gcovr 7.2, Unity 2.6.0 (vendored), RTEMS 6 (RSB commit in
# .github/workflows/nightly-target.yml), SIS 2.30.

ROOT   := ..
S4     := $(ROOT)/labs/session-04
S5     := $(ROOT)/labs/session-05
S6     := $(ROOT)/labs/session-06
OUT    ?= build
PYTHON ?= python3

UNITY  := support/unity
GEN    := $(S6)/lab-6.2-generation/generated

# ---- Flight sources, by module ------------------------------------------------
SP_SRC    := $(S4)/lab-4.1-space-packet/sp.c
PUS_SRC   := $(S4)/lab-4.2-pus-service17/crc16.c $(S4)/lab-4.2-pus-service17/pus.c \
             $(S4)/lab-4.2-pus-service17/tc_handler.c
MON_SRC   := $(S5)/lab-5.1-monitoring/monitor.c
LIVE_SRC  := $(S5)/lab-5.2-liveness/supervisor.c $(S5)/lab-5.2-liveness/watchdog.c \
             $(S5)/lab-5.2-liveness/nvstate.c
SCHED_SRC := $(S5)/lab-5.3-safe-mode/schedule.c
MODE_SRC  := $(GEN)/mode_sm.c
APP_SRC   := $(S5)/app/services.c $(S5)/app/tmtc5.c $(S5)/app/sensors5.c \
             $(S6)/app/fdir6.c $(S5)/lab-5.4-fault-injection/finj.c
CORE_SRC  := $(SP_SRC) $(PUS_SRC) $(MON_SRC) $(LIVE_SRC) $(SCHED_SRC) $(MODE_SRC)
FLIGHT_SRC := $(CORE_SRC) $(APP_SRC) $(S5)/app/lab5.c

INC := -I$(S4)/lab-4.1-space-packet -I$(S4)/lab-4.2-pus-service17 \
       -I$(S5)/lab-5.1-monitoring -I$(S5)/lab-5.2-liveness -I$(S5)/lab-5.3-safe-mode \
       -I$(S5)/lab-5.4-fault-injection -I$(S5)/app -I$(S6)/app -I$(GEN)
SIM := -I$(S5)/host-sim

# ---- Warning sets ---------------------------------------------------------------
# Maximum set used on flight code (Lab 7.2). -Wunused-macros is left out of the
# host build only: the RTEMS configuration macros in lab5.c are consumed by
# <rtems/confdefs.h>, which the host simulation does not have (findings log
# SA-03).
WARN_MAX := -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
  -Wshadow -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wswitch-enum \
  -Wundef -Wdouble-promotion -Wformat=2 -Wnull-dereference -Wduplicated-cond \
  -Wduplicated-branches -Wlogical-op -Wcast-align=strict -Wjump-misses-init \
  -Wvla -Wredundant-decls -Wnested-externs -Wold-style-definition \
  -Wbad-function-cast -Wwrite-strings -Wfloat-equal -Wstack-usage=2048 \
  -Walloca -Wformat-overflow=2 -Wformat-truncation=2 -Werror
# Test code: strict, but test files may use patterns flight code may not.
WARN_TEST := -std=c99 -Wall -Wextra -Wpedantic -Wshadow -Werror
CFLAGS_UNIT := -O1 -g $(WARN_TEST) -DUNITY_INCLUDE_DOUBLE=0 -I$(UNITY) $(INC) \
               -DMODE_SM_WITH_NAMES

UNIT_SUITES := test_sp test_pus test_monitor test_liveness test_schedule test_mode_sm

test_sp_SRC       := $(SP_SRC)
test_pus_SRC      := $(PUS_SRC) $(SP_SRC)
test_monitor_SRC  := $(MON_SRC)
test_liveness_SRC := $(LIVE_SRC) $(S4)/lab-4.2-pus-service17/crc16.c
test_schedule_SRC := $(SCHED_SRC)
test_mode_sm_SRC  := $(MODE_SRC)

.PHONY: all test unit integration validation target warnings coverage coverage-full \
        mcdc static tools evidence matrix clean

all: test

test: warnings unit integration validation
	@echo "== all host test levels passed"

# ---- Unit level ---------------------------------------------------------------------
UNIT_BINS := $(addprefix $(OUT)/unit/,$(UNIT_SUITES))

define UNIT_RULE
$(OUT)/unit/$(1): unit/$(1).c $$($(1)_SRC) $(UNITY)/unity.c | $(OUT)/unit
	$$(CC) $$(CFLAGS_UNIT) $$(UNIT_EXTRA) -o $$@ unit/$(1).c $$($(1)_SRC) $(UNITY)/unity.c
endef
$(foreach s,$(UNIT_SUITES),$(eval $(call UNIT_RULE,$(s))))

$(OUT)/unit $(OUT)/integration $(OUT)/reports:
	mkdir -p $@

unit: $(UNIT_BINS)
	@fail=0; for t in $(UNIT_BINS); do \
	    echo "== $$t"; $$t > $$t.txt 2>&1; rc=$$?; tail -n 3 $$t.txt; \
	    [ $$rc -eq 0 ] || { fail=1; grep ":FAIL" $$t.txt; }; \
	done; exit $$fail

# ---- Integration level --------------------------------------------------------------
# The same integration program runs on the host simulation of the RTEMS API
# (here) and on RTEMS / SIS (make target). It prints a termination marker
# and exits; support/run_image.py turns the marker into an exit code.
INT_SRC := integration/int_tc_path.c $(CORE_SRC) $(APP_SRC)
INT_LDWRAP := -Wl,--wrap=tmtc_tm_sink

$(OUT)/integration/int_tc_path.host: $(INT_SRC) $(S5)/host-sim/rtems_shim.c | $(OUT)/integration
	$(CC) -O1 -g -std=gnu99 -Wall -Wextra -Werror -DFDIR_TEST_BUILD $(SIM) $(INC) \
	    $(INT_EXTRA) -o $@ $(INT_SRC) $(S5)/host-sim/rtems_shim.c -lpthread $(INT_LDWRAP)

integration: $(OUT)/integration/int_tc_path.host
	rm -f $(OUT)/integration/nv.bin
	FDIR_NV_FILE=$(OUT)/integration/nv.bin $(PYTHON) support/run_image.py --timeout 120 --log $(OUT)/integration/int_tc_path.host.log \
	    -- $(OUT)/integration/int_tc_path.host

# ---- Validation level ---------------------------------------------------------------
# Drives the complete application through the ground interface only.
APP_DIR := $(S6)/app
validation:
	$(MAKE) -C $(APP_DIR) host
	$(PYTHON) validation/run_campaign.py --target host-sim \
	    --spawn $(abspath $(APP_DIR))/lab6-test.host \
	    --spawn-flight $(abspath $(APP_DIR))/lab6-flight.host \
	    --out $(ROOT)/labs/session-07/validation/results-host-sim

# ---- Target level (RTEMS 6 / LEON3, SIS) --------------------------------------------------
RTEMS_PREFIX ?= $(HOME)/quick-start/rtems/6
RTEMS_CC     := $(RTEMS_PREFIX)/bin/sparc-rtems6-gcc
RTEMS_CFLAGS := -B$(RTEMS_PREFIX)/sparc-rtems6/leon3/lib -qrtems -mcpu=leon3 \
                -O2 -g -Wall -Wextra -ffunction-sections -fdata-sections
RTEMS_LDFLAGS := -Wl,--gc-sections
SIS ?= $(RTEMS_PREFIX)/bin/sparc-rtems6-sis
$(OUT)/integration/int_tc_path.exe: $(INT_SRC) | $(OUT)/integration
	$(RTEMS_CC) $(RTEMS_CFLAGS) -DFDIR_TEST_BUILD $(INC) -o $@ $(INT_SRC) \
	    $(RTEMS_LDFLAGS) $(INT_LDWRAP)

target: $(OUT)/integration/int_tc_path.exe
	$(PYTHON) support/run_image.py --timeout 300 --log $(OUT)/integration/int_tc_path.sis.log \
	    -- $(SIS) -leon3 -r $(OUT)/integration/int_tc_path.exe
	$(MAKE) -C $(APP_DIR) rtems
	$(PYTHON) validation/run_campaign.py --target sis --pty --nv-volatile \
	    --spawn "$(SIS) -leon3 -rt -r $(abspath $(APP_DIR))/lab6-test.exe" \
	    --spawn-flight "$(SIS) -leon3 -rt -r $(abspath $(APP_DIR))/lab6-flight.exe" \
	    --out $(ROOT)/labs/session-07/validation/results-sis

# ---- Static checks ------------------------------------------------------------------------
warnings:
	@for d in -DFDIR_TEST_BUILD -UFDIR_TEST_BUILD; do for f in $(FLIGHT_SRC); do \
	    $(CC) -O2 $(WARN_MAX) $$d $(SIM) $(INC) -c $$f -o /dev/null || exit 1; \
	done; done; echo "== warnings: $(words $(FLIGHT_SRC)) flight sources x 2 builds, 0 warnings"

static: | $(OUT)/reports
	$(PYTHON) support/static_analysis.py --out $(ROOT)/labs/session-07/static-analysis \
	    -- $(SIM) $(INC) -- $(FLIGHT_SRC)

# ---- Coverage -------------------------------------------------------------------------------
# coverage       unit + integration (fast CI job, a few seconds); report in
#                build/coverage-report, unclassified lines listed, not enforced
# coverage-full  + the validation campaign on instrumented host builds of the
#                application (about 10 min; release job and the committed report)
COV := $(OUT)/cov
COV_APP_SRC := $(S5)/app/lab5.c $(APP_SRC) $(CORE_SRC) $(S5)/host-sim/rtems_shim.c \
               support/gcov_periodic.c
COV_APP_FLAGS := --coverage -fprofile-update=atomic -O0 -g -std=gnu99 $(SIM) $(INC) \
                 -Wl,--wrap=_exit -lpthread

coverage:
	rm -rf $(COV)/unit $(COV)/integration
	$(MAKE) OUT=$(COV) UNIT_EXTRA="--coverage -O0" INT_EXTRA="--coverage -fprofile-update=atomic -O0 -DINT_COVERAGE" unit integration
	$(PYTHON) support/coverage_report.py --objdir $(COV) --root $(ROOT) --no-enforce \
	    --justifications $(ROOT)/labs/session-07/coverage/justifications.yaml \
	    --out $(OUT)/coverage-report

coverage-full:
	rm -rf $(COV)
	$(MAKE) OUT=$(COV) UNIT_EXTRA="--coverage -O0" INT_EXTRA="--coverage -fprofile-update=atomic -O0 -DINT_COVERAGE" unit integration
	mkdir -p $(COV)/validation
	$(CC) -DFDIR_TEST_BUILD -o $(COV)/validation/lab6-test.host $(COV_APP_SRC) $(COV_APP_FLAGS)
	$(CC) -o $(COV)/validation/lab6-flight.host $(COV_APP_SRC) $(COV_APP_FLAGS)
	-$(PYTHON) validation/run_campaign.py --target host-sim-coverage --fp-seconds 300 \
	    --spawn $(abspath $(COV))/validation/lab6-test.host \
	    --spawn-flight $(abspath $(COV))/validation/lab6-flight.host \
	    --out $(COV)/validation/campaign
	$(PYTHON) support/coverage_report.py --objdir $(COV) --root $(ROOT) \
	    --out $(ROOT)/labs/session-07/coverage

mcdc:
	$(PYTHON) support/mcdc_clang.py --root $(ROOT) --out $(ROOT)/labs/session-07/coverage

# Copy the host test outputs into the committed evidence, then rebuild the
# traceability matrix from them (Lab 7.4).
EVID := $(ROOT)/labs/session-07/test-runs
evidence: unit integration
	mkdir -p $(EVID)
	for t in $(UNIT_BINS); do echo "### $$t"; cat $$t.txt; done > $(EVID)/unit-results.txt
	cp $(OUT)/integration/int_tc_path.host.log $(EVID)/integration-host-sim.log
	$(PYTHON) validation/build_matrix.py

matrix:
	$(PYTHON) validation/build_matrix.py

tools:
	sh support/check_tools.sh

clean:
	rm -rf $(OUT)
