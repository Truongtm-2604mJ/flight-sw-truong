# Lab 6.2 step 5 - the Session 5 FDIR application with the GENERATED mode
# manager in place of the hand written Lab 5.3 one.
#
#   make host           lab6-test.host   (test build, fault injection)
#   make host-flight    lab6-flight.host
#   make check-no-hand  the hand written modes.c is not linked
#   make system-test    Session 5 system scenarios against the generated build
#   make rtems          lab6-test.exe / lab6-flight.exe for RTEMS 6 / LEON3
include ../rtems.mk

A5   = $(S5)/app
L51  = $(S5)/lab-5.1-monitoring
L52  = $(S5)/lab-5.2-liveness
L53  = $(S5)/lab-5.3-safe-mode
L54  = $(S5)/lab-5.4-fault-injection
S4   = ../../session-04
L41  = $(S4)/lab-4.1-space-packet
L42  = $(S4)/lab-4.2-pus-service17
SIM  = $(S5)/host-sim
G54  = $(L54)/ground

# modes.h is included for its type definitions only; modes.c is NOT built.
INC  = -I. -I$(GEN) -I$(A5) -I$(L41) -I$(L42) -I$(L51) -I$(L52) -I$(L53) -I$(L54)
SRC  = $(A5)/lab5.c $(A5)/services.c $(A5)/tmtc5.c $(A5)/sensors5.c fdir6.c \
       $(GEN)/mode_sm.c \
       $(L51)/monitor.c $(L52)/supervisor.c $(L52)/watchdog.c $(L52)/nvstate.c \
       $(L53)/schedule.c \
       $(L41)/sp.c $(L42)/crc16.c $(L42)/pus.c $(L42)/tc_handler.c
TEST_SRC = $(SRC) $(L54)/finj.c
LOGS = ../lab-6.2-generation/logs

.PHONY: host host-flight check-no-hand system-test rtems clean

host: lab6-test.host
host-flight: lab6-flight.host

lab6-test.host: $(TEST_SRC) $(SIM)/rtems_shim.c
	$(CC) $(HOST_CFLAGS) -DFDIR_TEST_BUILD -I$(SIM) $(INC) -o $@ $(TEST_SRC) $(SIM)/rtems_shim.c -lpthread

lab6-flight.host: $(SRC) $(SIM)/rtems_shim.c
	$(CC) $(HOST_CFLAGS) -I$(SIM) $(INC) -o $@ $(SRC) $(SIM)/rtems_shim.c -lpthread

check-no-hand: lab6-test.host lab6-flight.host
	@for f in lab6-test.host lab6-flight.host; do \
	    if nm $$f | grep -E ' T (mode_request|mode_init|mode_set_inhibit|mode_tick)$$'; then \
	        echo "FAIL: hand written mode manager linked into $$f"; exit 1; fi; \
	    nm $$f | grep -q ' T mode_sm_step$$' || { echo "FAIL: no mode_sm_step in $$f"; exit 1; }; \
	    echo "OK: $$f uses the generated mode_sm_step, no hand written modes.c"; done

# The Session 5 system scenarios, unchanged, against this build.
system-test: lab6-test.host lab6-flight.host
	mkdir -p $(LOGS)
	$(PYTHON) $(G54)/gs_modes.py --spawn ./lab6-test.host \
	    --nv-file /tmp/fdir6_nv_modes.bin --log $(LOGS)/host-sim-modes.log
	$(PYTHON) $(G54)/campaign.py --spawn ./lab6-test.host --label host-sim-generated \
	    --results ../lab-6.2-generation/logs/campaign-host-sim-generated.md \
	    --nv-file /tmp/fdir6_nv_c.bin --log $(LOGS)/host-sim-campaign.log
	$(PYTHON) $(G54)/check_flight.py --spawn ./lab6-flight.host \
	    --nv-file /tmp/fdir6_nv_f.bin --log $(LOGS)/host-sim-check-flight.log

rtems: lab6-test.exe lab6-flight.exe

lab6-test.exe: $(TEST_SRC)
	$(RTEMS_CC) $(RTEMS_CFLAGS) -DFDIR_TEST_BUILD $(INC) -o $@ $(TEST_SRC) $(RTEMS_LDFLAGS)

lab6-flight.exe: $(SRC)
	$(RTEMS_CC) $(RTEMS_CFLAGS) $(INC) -o $@ $(SRC) $(RTEMS_LDFLAGS)

clean:
	rm -f lab6-test.host lab6-flight.host lab6-test.exe lab6-flight.exe fdir_nv.bin
